// Standalone player: a folder to zip and hand out that starts a disc image
// in pcsx-redux's kiosk mode (no menus, no BIOS needed), so a player never
// opens a bin/cue in an emulator themselves (splashedit-ng#151).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace splash {

// The system the player folder runs on: the pcsx-redux copied into it decides.
enum class PlayerOs { Windows, Mac, Linux };

// What of a pcsx-redux install the player copies, from the path Play runs:
//   Folder  a .exe (or pcsx-redux.main, the program pcsx-redux.exe starts):
//           its whole folder, the DLLs beside it included;
//   App     a program inside a macOS .app: the .app;
//   AppDir  an AppDir's AppRun, or a symlink to it (the Linux bundle's
//           redux/pcsx-redux): the AppDir;
//   File    anything else (an AppImage): the file alone.
// `root` is what is copied, `exe` the program the launcher runs, relative to
// what lands in the player's redux/ ('/' separators), `os` what it runs on
// (File: the system this runs on).
enum class ReduxKind { Folder, App, AppDir, File };
struct ReduxInstall {
    ReduxKind kind = ReduxKind::File;
    std::filesystem::path root;
    std::string exe;
    PlayerOs os = PlayerOs::Linux;
};
// False with `error` set when `redux` is not a file.
bool findReduxInstall(const std::filesystem::path& redux, ReduxInstall& out, std::string& error);

// `title` as a file name: characters Windows, a shell or a .bat would trip on
// (< > : " / \ | ? * % $ ` ! and control characters) dropped, spaces and
// dots trimmed from the ends, "Game" when nothing is left, a name Windows
// keeps for a device (CON, NUL, COM1, ...) with " Game" after it. `ascii`
// also drops every byte past 0x7E (the disc's file name, which the .bat
// names and cmd reads in the console's code page).
std::string playerFileName(const std::string& title, bool ascii = false);

// The launcher, for `os`: <title>.bat, .command or .sh. Each finds the
// files beside itself (%~dp0, the directory of $0), wherever the folder was
// unpacked, and runs `exe` with -kiosk -run -iso <cue> -portable <config>.
// The paths are relative to the player folder, '/' separators.
std::string launcherName(const std::string& title, PlayerOs os);
std::string launcherText(PlayerOs os, const std::string& exe, const std::string& cue, const std::string& config);

// The Windows launcher (tools/player_launcher), shipped beside splashedit.exe:
// copied to <title>.exe, it reads config/launch.txt (launchConfig) and starts
// pcsx-redux as the .bat would, under the icon setExeIcon gives it.
inline constexpr const char* kLauncherStub = "splashedit-player-launcher.exe";
// config/launch.txt: the program, the cue and the config folder, a line each,
// relative to the player folder ('/' separators).
std::string launchConfig(const std::string& exe, const std::string& cue, const std::string& config);

// Makes `rgba16` (16x16 texels, top row first, 0xAABBGGRR, alpha below 128
// transparent) the icon of the program `exe`: 16, 32 and 48 pixel images,
// nearest-neighbour, in icon group 1 (what Explorer shows), in place of the
// icons it had. Windows only: elsewhere false with `error` set.
bool setExeIcon(const std::filesystem::path& exe, const std::vector<uint32_t>& rgba16, std::string& error);

// The pcsx.json a player folder starts with: no first-run "Update
// configuration" dialog over the game, no update checks.
std::string seededReduxConfig();

// The file marking a folder as a player export; it lists what the export wrote.
inline constexpr const char* kPlayerMarker = ".splashedit-player";

// "" when `out` can take a player: missing, empty, or a player export
// (kPlayerMarker); else why not.
std::string playerDirError(const std::filesystem::path& out);

// Writes the player into `out`:
//   game/<bin and cue>    `bin` and the .cue beside it, under their names;
//   redux/...             the ReduxInstall of `redux`;
//   config/pcsx.json      seededReduxConfig;
//   <title>.bat|.command|.sh  the launcher, executable;
//   .splashedit-player    the marker.
// For Windows, with `launcherStub` (a kLauncherStub) there, the launcher is
// <title>.exe, a copy of it, beside config/launch.txt, with the icon `icon`
// when given (see setExeIcon; nullptr keeps the stub's). An empty
// `launcherStub` writes the .bat; a missing one too, with a warning.
// A previous player export there has what its marker lists removed first;
// anything else in it is left. `out` must pass playerDirError and not lie
// inside what is copied. `error` empty means it was written; `warnings` say
// what the player may still need (an AppImage needs FUSE; a lone program,
// its libraries).
struct PlayerResult {
    std::string error;
    std::vector<std::string> warnings;
    std::filesystem::path launcher;
    PlayerOs os = PlayerOs::Linux;
};
PlayerResult writePlayer(const std::filesystem::path& bin, const std::filesystem::path& redux,
                         const std::filesystem::path& out, const std::string& title,
                         const std::filesystem::path& launcherStub = {}, const std::vector<uint32_t>* icon = nullptr);

}  // namespace splash
