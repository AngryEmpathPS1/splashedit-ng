#include "player.hh"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <system_error>

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

PlayerResult writePlayer(const fs::path& bin, const fs::path& redux, const fs::path& out, const std::string& title) {
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

        const std::string launcher = launcherName(title, ri.os);
        r.launcher = out / fromUtf8(launcher);
        writeText(r.launcher, launcherText(ri.os, ri.exe, "game/" + utf8(cue.filename()), "config"));
        if (ri.os != PlayerOs::Windows) makeExecutable(r.launcher);
        writeText(out / kPlayerMarker, "game\nredux\nconfig\n" + launcher + "\n");
    } catch (const std::exception& e) {
        r.error = e.what();
        r.launcher.clear();
    }
    return r;
}

}  // namespace splash
