// GPU dump files (.gpd), as pcsx-redux's GPU dump recorder writes them, and
// the GP0 / GP1 command list they hold.
//
// The format (https://github.com/ps1dev/standards/blob/main/GPUDUMP.md): the
// 16-byte magic "PSXGPUDUMPv1r1\0\0", then packets, each a little-endian word
// (type << 24) | length (24 bits) followed by `length` words. Everything up to
// the TraceBegin packet restores the GPU (a reset, a full VRAM upload, the
// draw and display state); the live traffic follows, cut into frames by
// VSync packets. A GP0 command may continue in a later GP0 packet.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gpudump {

enum PacketType : uint8_t {
    GP0 = 0x00,
    GP1 = 0x01,
    VSync = 0x02,
    ThrowAway = 0x03,
    Readback = 0x04,
    TraceBegin = 0x05,
    GPUVersion = 0x06,
    GameID = 0x10,
    VideoFormat = 0x11,
    Comment = 0x12,
};

struct Packet {
    uint8_t type = 0;
    uint32_t offset = 0;  // index in Dump::words of its first payload word
    uint32_t length = 0;
};

struct Dump {
    std::vector<uint32_t> words;  // the whole file as words, the magic included
    std::vector<Packet> packets;
    uint32_t version = 0;
    std::string gameId, videoFormat, comment;
    bool truncated = false;  // the last packet runs past the end of the file (dropped)
};

// Splits a file's bytes into packets. False, with `error` set, when the magic
// does not match.
bool parse(const std::vector<uint8_t>& bytes, Dump& out, std::string* error);
bool load(const std::filesystem::path& file, Dump& out, std::string* error);
// Writes `dump` back as a file, byte for byte what was loaded.
bool save(const std::filesystem::path& file, const Dump& dump, std::string* error);

// The draw state GP0(E1h..E6h) sets, and the display state from GP1(05h..08h).
struct GpuState {
    uint32_t texpage = 0;  // E1 bits 0-13: page x/y, blend mode, depth, dither, draw to display, flips
    uint32_t texWindow = 0;  // E2 bits 0-19
    int areaX1 = 0, areaY1 = 0, areaX2 = 0, areaY2 = 0;  // E3 / E4, inclusive
    int offsetX = 0, offsetY = 0;                         // E5, signed
    bool setMask = false, checkMask = false;              // E6
    uint32_t displayStart = 0, hRange = 0, vRange = 0, displayMode = 0;  // GP1 05 / 06 / 07 / 08

    int pageX() const { return int(texpage & 0xf) * 64; }
    int pageY() const { return int((texpage >> 4) & 1) * 256; }
    int blendMode() const { return int((texpage >> 5) & 3); }
    int depth() const { return int((texpage >> 7) & 3); }  // 0 4-bit, 1 8-bit, 2 (and 3) 15-bit
    bool dither() const { return (texpage >> 9) & 1; }
    // Display area in VRAM: start, and the size the display mode shows.
    int displayX() const { return int(displayStart & 0x3ff); }
    int displayY() const { return int((displayStart >> 10) & 0x1ff); }
    int displayWidth() const;
    int displayHeight() const;
};

enum class Kind : uint8_t {
    Nop,
    Fill,       // GP0(02h)
    Polygon,    // GP0(20h..3Fh)
    Line,       // GP0(40h..5Fh), single or poly
    Rect,       // GP0(60h..7Fh)
    CopyVram,   // GP0(80h..9Fh) VRAM to VRAM
    CopyIn,     // GP0(A0h..BFh) CPU to VRAM
    CopyOut,    // GP0(C0h..DFh) VRAM to CPU
    DrawMode,   // E1
    TexWindow,  // E2
    AreaTopLeft,      // E3
    AreaBottomRight,  // E4
    DrawOffset,       // E5
    MaskBits,         // E6
    ClearCache,       // 01
    Irq,              // 1F
    Gp1,              // any GP1 word
    VSync,            // a VSync packet (frame boundary)
};

struct Vertex {
    int16_t x = 0, y = 0;  // as sent: signed 11-bit, before the drawing offset
    uint8_t r = 0, g = 0, b = 0;
    uint8_t u = 0, v = 0;
};

// A psxsplash capture tag (psxsplash src/capturetag.hh): two GP0(00h) NOP
// words the engine appends to a mesh triangle's packet when the frame
// debugger switches tagging on. Word 1 is 0x00A00000 | kind << 18 |
// (obj >> 10) << 16 | tri, word 2 is (obj & 0x3ff) << 14 | slot.
struct PrimTag {
    bool present = false;
    uint8_t kind = 0;   // kTagMain or kTagFog
    uint16_t obj = 0;   // game object index in the splashpack, kTagNoObject when it did not fit
    uint16_t tri = 0;   // triangle index in that object's list (the unsplit source triangle)
    uint16_t slot = 0;  // ordering-table bucket, kTagNoSlot when it did not fit
};
constexpr uint8_t kTagMain = 0, kTagFog = 1;
constexpr uint16_t kTagNoObject = 0xfff, kTagNoSlot = 0x3fff;
// Word 1 of a tag: a GP0(00h) with bits 23-20 = 0xA.
inline bool isTagWord1(uint32_t w) { return (w & 0xfff00000u) == 0x00a00000u; }
inline PrimTag tagOf(uint32_t w1, uint32_t w2) {
    PrimTag t;
    t.present = true;
    t.kind = uint8_t((w1 >> 18) & 3);
    t.obj = uint16_t(((w1 >> 16) & 3) << 10 | ((w2 >> 14) & 0x3ff));
    t.tri = uint16_t(w1 & 0xffff);
    t.slot = uint16_t(w2 & 0x3fff);
    return t;
}

struct Command {
    Kind kind = Kind::Nop;
    uint32_t op = 0;               // the first word (for Gp1, the GP1 word)
    std::vector<uint32_t> words;   // every word of the command, data included
    uint32_t firstWord = 0, lastWord = 0;  // where its first and last words sit in Dump::words
    bool live = false;             // after TraceBegin: part of the captured traffic
    int frame = 0;                 // VSync packets before it (after TraceBegin)
    GpuState state;               // in effect when it runs (a textured polygon's page applied)
    // Drawing commands, decoded.
    std::vector<Vertex> vertices;
    uint16_t clut = 0;              // textured: CLUT attribute
    int width = 0, height = 0;      // rects, fills, copies (masked as the GPU masks them)
    int srcX = 0, srcY = 0, dstX = 0, dstY = 0;  // fills and copies (dst only for fills and CopyIn)
    // Polygons: the capture tag that followed it in the stream (its two words
    // are not commands of their own).
    PrimTag tag;

    bool textured() const { return (kind == Kind::Polygon || kind == Kind::Rect) && (op >> 26 & 1); }
    bool raw() const { return textured() && (op >> 24 & 1); }
    bool semiTransparent() const { return (kind == Kind::Polygon || kind == Kind::Rect || kind == Kind::Line) && (op >> 25 & 1); }
    bool gouraud() const { return (kind == Kind::Polygon || kind == Kind::Line) && (op >> 28 & 1); }
    bool quad() const { return kind == Kind::Polygon && (op >> 27 & 1); }
    // Writes pixels: polygons, lines, rects, fills and the copies into VRAM.
    bool draws() const {
        return kind == Kind::Fill || kind == Kind::Polygon || kind == Kind::Line || kind == Kind::Rect || kind == Kind::CopyVram ||
               kind == Kind::CopyIn;
    }
    int clutX() const { return (clut & 0x3f) * 16; }
    int clutY() const { return (clut >> 6) & 0x1ff; }
};

// "tri-flat", "quad-gouraud-tex-semi", "rect16-raw", "line", "polyline-gouraud", "fill", "copy-vram", "copy-in", ...
std::string typeName(const Command& c);

struct CommandList {
    std::vector<Command> commands;
    std::vector<int> drawn;  // indices of the live commands that draw, in order: the primitives stepped through
    int frames = 0;          // VSync packets after TraceBegin
    bool incomplete = false; // the dump ends inside a GP0 command (dropped)
    int tagged = 0;          // polygons carrying a capture tag
};

// Decodes the GP0 and GP1 traffic of `dump` in order.
CommandList decode(const Dump& dump);

// What a capture tag's object and triangle indices refer to: the game objects
// of the splashpack the captured scene was loaded from (Play writes
// scene_<i>.splashpack into its directory), read from the object table and
// the object name table (docs/splashpack-format.md 2.2, 2.13).
struct TagScene {
    struct Object {
        std::string name;        // as the pack stores it (24 UTF-16 units at most)
        uint16_t triangles = 0;  // 0 for an object whose geometry streams (.geo)
        bool streamed = false;
        std::vector<uint32_t> looks;  // per triangle: clut << 16 | (tpage & 0x19f)
    };
    std::filesystem::path pack;
    std::vector<Object> objects;
};
bool parseTagScene(const std::vector<uint8_t>& bytes, TagScene& out, std::string* error);
bool loadTagScene(const std::filesystem::path& pack, TagScene& out, std::string* error);
// Which of `scenes` the tagged polygons of `list` came from: every tag's
// object (and, for objects in the pack, triangle) in range, and the most
// textured triangles whose CLUT and texture page match the tagged one. -1
// when none fits, or several fit equally well.
int matchTagScene(const CommandList& list, const std::vector<TagScene>& scenes);
// "object <name> (#obj), triangle <tri>, OT slot <slot>" plus ", fog pass";
// `scene` null (or the index past its objects) leaves the name out. Empty
// for an untagged command.
std::string tagText(const PrimTag& tag, const TagScene* scene);

}  // namespace gpudump
