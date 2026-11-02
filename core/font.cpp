#include "font.hh"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "stb_truetype.h"
#include "texture.hh"

namespace splash {

namespace {

constexpr int kSheetW = 256;
constexpr int kFirst = 0x20, kGlyphs = 95;  // 0x20..0x7E
constexpr int kInk = 96;  // coverage out of 255 that counts as ink

[[noreturn]] void fail(const UIFont& f, const std::string& what) {
    throw std::runtime_error("font '" + f.name + "': " + what);
}

void layout(FontSheet& s, const UIFont& f) {
    if (s.glyphWidth < 1 || s.glyphWidth > kSheetW) fail(f, "glyph cells must be 1..256 wide");
    int perRow = kSheetW / s.glyphWidth;
    int rows = (kGlyphs + perRow - 1) / perRow;
    s.height = rows * s.glyphHeight;
    // The sheet must sit inside one texture page: V is a byte.
    if (s.height > 256)
        fail(f, std::to_string(s.glyphWidth) + "x" + std::to_string(s.glyphHeight) + " cells need " +
                    std::to_string(s.height) + " rows, a texture page has 256");
    s.texels.assign(size_t(kSheetW) * s.height, 0);
}

int cellX(const FontSheet& s, int i) { return (i % (kSheetW / s.glyphWidth)) * s.glyphWidth; }
int cellY(const FontSheet& s, int i) { return (i / (kSheetW / s.glyphWidth)) * s.glyphHeight; }

FontSheet fromTrueType(const UIFont& f, const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) fail(f, "cannot open " + file.string());
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    stbtt_fontinfo info;
    int offset = stbtt_GetFontOffsetForIndex(data.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&info, data.data(), offset)) fail(f, file.string() + " is not a TrueType/OpenType font");
    float scale = stbtt_ScaleForMappingEmToPixels(&info, float(f.size));

    // A 1-bit glyph is sharpest when its strokes sit on whole pixels, so
    // each glyph is rendered at the horizontal subpixel offset (and the font
    // at the vertical one, which moves every baseline alike) that leaves the
    // fewest half-covered pixels. Without this a one-pixel stem that
    // straddles two columns covers each by half and thresholds to nothing.
    struct Glyph {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        std::vector<unsigned char> cov;
        long blur = 0;  // sum of min(c, 255 - c)
    };
    constexpr float kShifts[] = {0.f, 0.25f, 0.5f, 0.75f};
    auto render = [&](int i, float sx, float sy) {
        Glyph gl;
        stbtt_GetCodepointBitmapBoxSubpixel(&info, kFirst + i, scale, scale, sx, sy, &gl.x0, &gl.y0, &gl.x1, &gl.y1);
        int w = gl.x1 - gl.x0, h = gl.y1 - gl.y0;
        if (w <= 0 || h <= 0) return gl;
        gl.cov.resize(size_t(w) * h);
        stbtt_MakeCodepointBitmapSubpixel(&info, gl.cov.data(), w, h, w, scale, scale, sx, sy, kFirst + i);
        for (unsigned char c : gl.cov) gl.blur += std::min<int>(c, 255 - c);
        return gl;
    };
    auto bestFor = [&](int i, float sy) {
        Glyph best = render(i, 0, sy);
        for (float sx : kShifts) {
            Glyph g = render(i, sx, sy);
            if (g.blur < best.blur) best = std::move(g);
        }
        return best;
    };
    std::array<Glyph, kGlyphs> g;
    long bestBlur = -1;
    for (float sy : kShifts) {
        std::array<Glyph, kGlyphs> cand;
        long blur = 0;
        for (int i = 0; i < kGlyphs; i++) {
            cand[size_t(i)] = bestFor(i, sy);
            blur += cand[size_t(i)].blur;
        }
        if (bestBlur < 0 || blur < bestBlur) {
            bestBlur = blur;
            g = std::move(cand);
        }
    }
    FontSheet s;
    int ascent = 0, descent = 0, right = 1;
    for (int i = 0; i < kGlyphs; i++) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&info, kFirst + i, &adv, &lsb);
        s.advances[size_t(i)] = uint8_t(std::clamp(int(std::lround(adv * scale)), 1, 255));
        const Glyph& gl = g[size_t(i)];
        if (gl.cov.empty()) continue;
        ascent = std::max(ascent, -gl.y0);
        descent = std::max(descent, gl.y1);
        right = std::max(right, std::max(0, gl.x0) + gl.x1 - gl.x0);
    }
    // Row 0 of the sheet stays blank: psxsplash uploads the font's CLUT over
    // the first 8 texels of it (psxsplash#56).
    s.glyphHeight = 1 + ascent + descent;
    // Narrow the cells until the sheet fits a texture page; the widest glyphs
    // then lose their right edge (reported below).
    s.glyphWidth = right;
    auto rowsFor = [](int w) { return (kGlyphs + kSheetW / w - 1) / (kSheetW / w); };
    while (s.glyphWidth > 1 && rowsFor(s.glyphWidth) * s.glyphHeight > 256) s.glyphWidth--;
    layout(s, f);
    std::string clipped;
    for (int i = 0; i < kGlyphs; i++) {
        const Glyph& gl = g[size_t(i)];
        if (gl.cov.empty()) continue;
        int w = gl.x1 - gl.x0, h = gl.y1 - gl.y0;
        int ox = cellX(s, i) + std::max(0, gl.x0), oy = cellY(s, i) + 1 + ascent + gl.y0;
        int drawn = std::min<int>(s.advances[size_t(i)], s.glyphWidth);
        bool lost = false;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                if (gl.cov[size_t(y) * w + x] < kInk) continue;
                if (std::max(0, gl.x0) + x >= s.glyphWidth) {
                    lost = true;
                    continue;
                }
                s.texels[size_t(oy + y) * kSheetW + ox + x] = 1;
                if (std::max(0, gl.x0) + x >= drawn) lost = true;
            }
        if (lost) clipped += char(kFirst + i);
    }
    s.advances[95] = uint8_t(s.glyphWidth);
    if (!clipped.empty())
        s.warnings.push_back("font '" + f.name + "': ink past the advance or the cell is not drawn for \"" + clipped + "\"");
    return s;
}

FontSheet fromBitmap(const UIFont& f, const std::filesystem::path& file) {
    Image img = loadImage(file);
    if (img.width != kSheetW) fail(f, "bitmap must be 256 pixels wide, " + file.string() + " is " + std::to_string(img.width));
    FontSheet s;
    s.glyphWidth = f.glyphWidth;
    s.glyphHeight = f.glyphHeight;
    layout(s, f);
    if (img.height < s.height)
        fail(f, "bitmap needs " + std::to_string(s.height) + " rows for " + std::to_string(f.glyphWidth) + "x" +
                    std::to_string(f.glyphHeight) + " cells, " + file.string() + " has " + std::to_string(img.height));
    for (int y = 0; y < s.height; y++)
        for (int x = 0; x < kSheetW; x++)  // Image rows are bottom-up
            s.texels[size_t(y) * kSheetW + x] = img.pixels[size_t(img.height - 1 - y) * kSheetW + x].a > 0.5f;
    if (!f.advances.empty()) {
        for (int i = 0; i < 96; i++) s.advances[size_t(i)] = uint8_t(std::clamp(f.advances[size_t(i)], 1, 255));
    } else if (f.monospace) {
        s.advances.fill(uint8_t(std::clamp(s.glyphWidth, 1, 255)));
    } else {
        // Ink width plus `gap` columns of spacing; space is half a cell.
        for (int i = 0; i < kGlyphs; i++) {
            int last = -1;
            for (int y = 0; y < s.glyphHeight; y++)
                for (int x = 0; x < s.glyphWidth; x++)
                    if (s.texels[size_t(cellY(s, i) + y) * kSheetW + cellX(s, i) + x]) last = std::max(last, x);
            s.advances[size_t(i)] = uint8_t(last < 0 ? std::max(1, (s.glyphWidth + 1) / 2) : std::clamp(last + 1 + f.gap, 1, 255));
        }
        s.advances[95] = uint8_t(s.glyphWidth);
    }
    for (int x = 0; x < 64; x++)
        if (s.texels[size_t(x)]) {
            s.warnings.push_back("font '" + f.name +
                                 "': the top row of the first glyph cells has ink; psxsplash overwrites 8 texels "
                                 "of it with the font CLUT and reads palette entries from the rest (psxsplash#56)");
            break;
        }
    return s;
}

}  // namespace

std::vector<uint8_t> FontSheet::packed4bpp() const {
    std::vector<uint8_t> out(texels.size() / 2);
    for (size_t i = 0; i < out.size(); i++) out[i] = uint8_t(texels[2 * i] | (texels[2 * i + 1] << 4));
    return out;
}

FontSheet buildFont(const UIFont& font, const std::filesystem::path& root) {
    return font.bitmap.empty() ? fromTrueType(font, root / font.source) : fromBitmap(font, root / font.bitmap);
}

}  // namespace splash
