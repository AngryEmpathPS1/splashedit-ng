#include "player.hh"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace splash {

namespace {

std::string utf8(const fs::path& p) {
    std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}
fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

PlayerOs hostOs() {
#if defined(_WIN32)
    return PlayerOs::Windows;
#elif defined(__APPLE__)
    return PlayerOs::Mac;
#else
    return PlayerOs::Linux;
#endif
}

// Whether `inner` is `outer` or inside it, both made absolute and resolved as far as they exist.
bool within(const fs::path& inner, const fs::path& outer) {
    std::error_code ec;
    const fs::path i = fs::weakly_canonical(fs::absolute(inner, ec), ec);
    const fs::path o = fs::weakly_canonical(fs::absolute(outer, ec), ec);
    const fs::path rel = i.lexically_relative(o);
    return !rel.empty() && *rel.begin() != "..";
}

// `s` inside "..." of a POSIX shell: \ $ ` and " escaped.
std::string shQuoted(const std::string& s) {
    std::string q = "\"";
    for (char c : s) {
        if (c == '\\' || c == '$' || c == '`' || c == '"') q += '\\';
        q += c;
    }
    return q + "\"";
}

// `rel` ('/' separators) under %~dp0 inside "..." of a .bat: backslashes, % doubled.
std::string batQuoted(const std::string& rel) {
    std::string q = "\"%~dp0";
    for (char c : rel) {
        if (c == '/') q += '\\';
        else if (c == '%') q += "%%";
        else q += c;
    }
    return q + "\"";
}

void writeText(const fs::path& file, const std::string& text) {
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f << text;
    f.close();
    if (!f) throw std::runtime_error("cannot write " + utf8(file));
}

void makeExecutable(const fs::path& file) {
    std::error_code ec;
    fs::permissions(file, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add,
                    ec);
}

// An AppImage starts as an ELF with "AI" 2 at offset 8.
bool isAppImage(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    char head[11] = {};
    f.read(head, sizeof head);
    return f && head[0] == 0x7f && head[1] == 'E' && head[8] == 'A' && head[9] == 'I' && head[10] == 2;
}

}  // namespace

bool findReduxInstall(const fs::path& redux, ReduxInstall& out, std::string& error) {
    std::error_code ec;
    if (redux.empty() || !fs::is_regular_file(redux, ec)) {
        error = redux.empty() ? "no pcsx-redux is set (Build > Tools...)" : "pcsx-redux not found: " + utf8(redux);
        return false;
    }
    out = ReduxInstall();
    // An AppDir's AppRun is often a symlink itself (to usr/bin/pcsx-redux), so
    // the links are followed one at a time and the first AppRun on the way wins.
    fs::path p = fs::absolute(redux, ec);
    for (int hop = 0; hop < 32; ++hop) {
        if (p.filename() == "AppRun") {
            out.kind = ReduxKind::AppDir;
            out.root = p.parent_path();
            out.exe = utf8(out.root.filename()) + "/AppRun";
            out.os = PlayerOs::Linux;
            return true;
        }
        if (!fs::is_symlink(p, ec)) break;
        fs::path target = fs::read_symlink(p, ec);
        if (ec) break;
        p = (target.is_absolute() ? target : p.parent_path() / target).lexically_normal();
    }
    fs::path real = fs::canonical(redux, ec);
    if (ec) real = fs::absolute(redux, ec);
    for (fs::path a = real.parent_path(); a.has_relative_path(); a = a.parent_path()) {
        if (lower(utf8(a.extension())) == ".app") {
            out.kind = ReduxKind::App;
            out.root = a;
            const std::u8string rel = real.lexically_relative(a).generic_u8string();
            out.exe = utf8(a.filename()) + "/" + std::string(rel.begin(), rel.end());
            out.os = PlayerOs::Mac;
            return true;
        }
        if (a == a.parent_path()) break;
    }
    const std::string name = utf8(real.filename());
    if (lower(utf8(real.extension())) == ".exe" || name == "pcsx-redux.main") {
        out.kind = ReduxKind::Folder;
        out.root = real.parent_path();
        // pcsx-redux.main is what pcsx-redux.exe starts (editor::withDefaults
        // runs it directly); a launcher starts the .exe beside it, as a user would.
        out.exe = name == "pcsx-redux.main" && fs::is_regular_file(out.root / "pcsx-redux.exe", ec) ? "pcsx-redux.exe" : name;
        out.os = PlayerOs::Windows;
        return true;
    }
    out.kind = ReduxKind::File;
    out.root = real;
    out.exe = name;
    out.os = hostOs();
    return true;
}

std::string playerFileName(const std::string& title, bool ascii) {
    std::string s;
    for (char c : title) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f || (ascii && u > 0x7e)) continue;
        if (std::string_view("<>:\"/\\|?*%$`!").find(c) != std::string_view::npos) continue;
        s += c;
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '.')) ++start;
    s = s.substr(start);
    if (s.empty()) return "Game";
    static const char* const reserved[] = {"con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
                                           "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                                           "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    const std::string stem = lower(s.substr(0, s.find('.')));
    for (const char* r : reserved)
        if (stem == r) return s + " Game";
    return s;
}

std::string launcherName(const std::string& title, PlayerOs os) {
    return playerFileName(title) + (os == PlayerOs::Windows ? ".bat" : os == PlayerOs::Mac ? ".command" : ".sh");
}

std::string launcherText(PlayerOs os, const std::string& exe, const std::string& cue, const std::string& config) {
    const std::string flags = " -kiosk -run -iso ";
    if (os == PlayerOs::Windows)
        return "@echo off\r\n"
               "rem Starts the game in pcsx-redux, from wherever this folder is. Written by SplashEdit.\r\n"
               "start \"\" " +
               batQuoted("redux/" + exe) + flags + batQuoted(cue) + " -portable " + batQuoted(config) + "\r\n";
    return "#!/bin/sh\n"
           "# Starts the game in pcsx-redux, from wherever this folder is. Written by SplashEdit.\n"
           "here=$(cd \"$(dirname \"$0\")\" && pwd)\n"
           "exec \"$here\"/" +
           shQuoted("redux/" + exe) + flags + "\"$here\"/" + shQuoted(cue) + " -portable \"$here\"/" + shQuoted(config) +
           "\n";
}

std::string launchConfig(const std::string& exe, const std::string& cue, const std::string& config) {
    return "redux/" + exe + "\n" + cue + "\n" + config + "\n";
}

#ifdef _WIN32
namespace {

void put16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    put16(b, v & 0xffff);
    put16(b, v >> 16);
}

// One RT_ICON image, `n` pixels square: a BITMAPINFOHEADER (the height
// doubled for the mask), 32-bit BGRA rows bottom up, then the 1-bit AND mask.
std::vector<uint8_t> iconImage(const std::vector<uint32_t>& rgba16, int n) {
    auto at = [&](int x, int y) { return rgba16[size_t(y * 16 / n) * 16 + size_t(x * 16 / n)]; };
    std::vector<uint8_t> b;
    put32(b, 40);
    put32(b, uint32_t(n));
    put32(b, uint32_t(2 * n));
    put16(b, 1);
    put16(b, 32);
    for (int i = 0; i < 6; ++i) put32(b, 0);  // BI_RGB, no size, resolution or palette
    for (int y = n - 1; y >= 0; --y)
        for (int x = 0; x < n; ++x) {
            const uint32_t c = at(x, y);
            const bool opaque = (c >> 24) >= 128;
            b.push_back(opaque ? uint8_t(c >> 16) : 0);
            b.push_back(opaque ? uint8_t(c >> 8) : 0);
            b.push_back(opaque ? uint8_t(c) : 0);
            b.push_back(uint8_t(opaque ? 255 : 0));
        }
    const size_t stride = size_t(n + 31) / 32 * 4;
    for (int y = n - 1; y >= 0; --y) {
        std::vector<uint8_t> row(stride, 0);
        for (int x = 0; x < n; ++x)
            if ((at(x, y) >> 24) < 128) row[size_t(x / 8)] |= uint8_t(0x80 >> (x % 8));
        b.insert(b.end(), row.begin(), row.end());
    }
    return b;
}

// RT_ICON and RT_GROUP_ICON for the W functions (windows.h gives them as char*).
const LPCWSTR kRtIcon = MAKEINTRESOURCEW(3), kRtGroupIcon = MAKEINTRESOURCEW(14);

// A resource id: a number, or a name.
struct ResId {
    WORD id = 0;
    std::wstring name;
    explicit ResId(LPCWSTR s) {
        if (IS_INTRESOURCE(s)) id = WORD(reinterpret_cast<ULONG_PTR>(s));
        else name = s;
    }
    LPCWSTR get() const { return name.empty() ? MAKEINTRESOURCEW(id) : name.c_str(); }
};
// The resources a program keeps when its icons are replaced: all but the icons.
struct Kept {
    ResId type, name;
    WORD lang;
    std::vector<uint8_t> data;
};
BOOL CALLBACK keptLanguage(HMODULE m, LPCWSTR type, LPCWSTR name, WORD lang, LONG_PTR p) {
    HRSRC r = FindResourceExW(m, type, name, lang);
    const auto* d = r ? static_cast<const uint8_t*>(LockResource(LoadResource(m, r))) : nullptr;
    if (d) reinterpret_cast<std::vector<Kept>*>(p)->push_back({ResId(type), ResId(name), lang, {d, d + SizeofResource(m, r)}});
    return TRUE;
}
BOOL CALLBACK keptName(HMODULE m, LPCWSTR type, LPWSTR name, LONG_PTR p) {
    EnumResourceLanguagesW(m, type, name, keptLanguage, p);
    return TRUE;
}
BOOL CALLBACK keptType(HMODULE m, LPWSTR type, LONG_PTR p) {
    if (type != kRtIcon && type != kRtGroupIcon) EnumResourceNamesW(m, type, keptName, p);
    return TRUE;
}

}  // namespace
#endif

bool setExeIcon(const fs::path& exe, const std::vector<uint32_t>& rgba16, std::string& error) {
    if (rgba16.size() != size_t(16 * 16)) {
        error = "the icon is not 16x16";
        return false;
    }
#ifdef _WIN32
    // Every resource goes and all but the icons come back, so none of the
    // stub's icons is left behind ours (deleting them one by one through
    // UpdateResourceW does nothing under Wine).
    std::vector<Kept> kept;
    if (HMODULE m = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE)) {
        EnumResourceTypesW(m, keptType, reinterpret_cast<LONG_PTR>(&kept));
        FreeLibrary(m);
    } else {
        error = "cannot read " + utf8(exe) + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    HANDLE h = BeginUpdateResourceW(exe.c_str(), TRUE);
    if (!h) {
        error = "cannot change " + utf8(exe) + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    bool ok = true;
    for (Kept& k : kept)
        ok = ok && UpdateResourceW(h, k.type.get(), k.name.get(), k.lang, k.data.data(), DWORD(k.data.size()));
    const WORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    // GRPICONDIR then a GRPICONDIRENTRY per image, naming its RT_ICON id.
    std::vector<uint8_t> group;
    put16(group, 0);
    put16(group, 1);
    put16(group, 3);
    WORD id = 1;
    for (int n : {16, 32, 48}) {
        std::vector<uint8_t> image = iconImage(rgba16, n);
        ok = ok && UpdateResourceW(h, kRtIcon, MAKEINTRESOURCEW(id), lang, image.data(), DWORD(image.size()));
        group.push_back(uint8_t(n));
        group.push_back(uint8_t(n));
        group.push_back(0);
        group.push_back(0);
        put16(group, 1);
        put16(group, 32);
        put32(group, uint32_t(image.size()));
        put16(group, id++);
    }
    ok = ok && UpdateResourceW(h, kRtGroupIcon, MAKEINTRESOURCEW(1), lang, group.data(), DWORD(group.size()));
    const DWORD failed = ok ? 0 : GetLastError();
    if (!EndUpdateResourceW(h, ok ? FALSE : TRUE) || !ok) {
        error = "cannot set the icon of " + utf8(exe) + " (error " + std::to_string(ok ? GetLastError() : failed) + ")";
        return false;
    }
    return true;
#else
    (void)exe;
    error = "a program's icon is set on Windows only";
    return false;
#endif
}

std::string seededReduxConfig() { return "{\"emulator\":{\"ShownAutoUpdateConfig\":true,\"AutoUpdate\":false}}\n"; }

std::string playerDirError(const fs::path& out) {
    std::error_code ec;
    if (!fs::exists(out, ec)) return {};
    if (!fs::is_directory(out, ec)) return utf8(out) + " is a file, not a folder";
    if (fs::is_regular_file(out / kPlayerMarker, ec)) return {};
    fs::directory_iterator it(out, ec);
    if (ec) return "cannot read " + utf8(out) + ": " + ec.message();
    if (it != fs::directory_iterator()) return utf8(out) + " is not empty and holds no standalone player: pick an empty folder";
    return {};
}

PlayerResult writePlayer(const fs::path& bin, const fs::path& redux, const fs::path& out, const std::string& title,
                         const fs::path& launcherStub, const std::vector<uint32_t>* icon) {
    PlayerResult r;
    ReduxInstall ri;
    if (!findReduxInstall(redux, ri, r.error)) return r;
    r.os = ri.os;
    std::error_code ec;
    fs::path cue = bin;
    cue.replace_extension(".cue");
    if (!fs::is_regular_file(bin, ec) || !fs::is_regular_file(cue, ec)) {
        r.error = "disc image not found: " + utf8(bin);
        return r;
    }
    if (std::string e = playerDirError(out); !e.empty()) {
        r.error = e;
        return r;
    }
    if (within(out, ri.root)) {
        r.error = "the player folder " + utf8(out) + " is inside the pcsx-redux it copies (" + utf8(ri.root) +
                  "): pick a folder outside it";
        return r;
    }
    if (within(bin, out)) {
        r.error = "the disc image " + utf8(bin) + " is inside the player folder " + utf8(out);
        return r;
    }
    try {
        fs::create_directories(out);
        // What a previous export wrote goes; whatever else is there stays.
        if (std::ifstream m(out / kPlayerMarker); m) {
            std::string line;
            while (std::getline(m, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const fs::path name = fromUtf8(line);
                if (line.empty() || line == "." || line == ".." || name.has_parent_path() || name.has_root_path()) continue;
                fs::remove_all(out / name);
            }
        }
        fs::create_directories(out / "game");
        fs::copy_file(bin, out / "game" / bin.filename(), fs::copy_options::overwrite_existing);
        fs::copy_file(cue, out / "game" / cue.filename(), fs::copy_options::overwrite_existing);

        const auto how = fs::copy_options::recursive | fs::copy_options::copy_symlinks;
        if (ri.kind == ReduxKind::Folder) {
            fs::copy(ri.root, out / "redux", how);
        } else {
            fs::create_directories(out / "redux");
            fs::copy(ri.root, out / "redux" / ri.root.filename(), how);
        }
        if (ri.os != PlayerOs::Windows) makeExecutable(out / "redux" / fromUtf8(ri.exe));
        if (ri.kind == ReduxKind::File && ri.os == PlayerOs::Linux && isAppImage(ri.root))
            r.warnings.push_back("pcsx-redux is an AppImage: some systems need FUSE installed to start it");
        else if (ri.kind == ReduxKind::File)
            r.warnings.push_back("pcsx-redux was copied on its own (" + utf8(ri.root) +
                                 "): the libraries it uses must be installed where the player runs");

        fs::create_directories(out / "config");
        writeText(out / "config" / "pcsx.json", seededReduxConfig());

        const std::string cueRel = "game/" + utf8(cue.filename());
        const bool stub = ri.os == PlayerOs::Windows && !launcherStub.empty() && fs::is_regular_file(launcherStub, ec);
        if (ri.os == PlayerOs::Windows && !launcherStub.empty() && !stub)
            r.warnings.push_back("the player launcher " + utf8(launcherStub) +
                                 " is missing: the player starts from a .bat instead, without the game's icon");
        const std::string launcher = stub ? playerFileName(title) + ".exe" : launcherName(title, ri.os);
        r.launcher = out / fromUtf8(launcher);
        if (stub) {
            fs::copy_file(launcherStub, r.launcher, fs::copy_options::overwrite_existing);
            writeText(out / "config" / "launch.txt", launchConfig(ri.exe, cueRel, "config"));
            if (std::string e; icon && !setExeIcon(r.launcher, *icon, e))
                r.warnings.push_back("the player keeps the default icon: " + e);
        } else {
            writeText(r.launcher, launcherText(ri.os, ri.exe, cueRel, "config"));
        }
        if (ri.os != PlayerOs::Windows) makeExecutable(r.launcher);
        writeText(out / kPlayerMarker, "game\nredux\nconfig\n" + launcher + "\n");
    } catch (const std::exception& e) {
        r.error = e.what();
        r.launcher.clear();
    }
    return r;
}

}  // namespace splash
