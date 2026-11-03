// The Windows launcher of a standalone player (core/player.hh): copied into
// the player folder as <title>.exe, it reads config\launch.txt beside itself
// (UTF-8, a line each: the pcsx-redux program, the cue, the config folder,
// relative to the folder, '/' separators) and starts
//   "<folder>\redux\..." -kiosk -run -iso "<folder>\game\....cue" -portable "<folder>\config"
// then exits. What it cannot find it says in a message box. kernel32 and
// user32 only, no console window.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define MAX_TEXT 32768

static wchar_t g_dir[MAX_TEXT];       // the player folder, with a trailing backslash
static wchar_t g_paths[3][MAX_TEXT];  // the program, the cue, the config folder: full paths
static wchar_t g_cmd[4 * MAX_TEXT];
static wchar_t g_msg[2 * MAX_TEXT];

static int fail(const wchar_t* what, const wchar_t* path) {
    lstrcpynW(g_msg, what, MAX_TEXT);
    if (path) {
        lstrcatW(g_msg, L"\n\n");
        lstrcpynW(g_msg + lstrlenW(g_msg), path, MAX_TEXT);
    }
    MessageBoxW(NULL, g_msg, L"Standalone player", MB_OK | MB_ICONERROR);
    return 1;
}

// Appends `s` to g_cmd as one argument in quotes, as CommandLineToArgvW reads
// it: backslashes before a quote doubled, quotes escaped.
static void appendQuoted(const wchar_t* s) {
    wchar_t* o = g_cmd + lstrlenW(g_cmd);
    const wchar_t* end = g_cmd + sizeof g_cmd / sizeof g_cmd[0] - 4;
    *o++ = L'"';
    for (; *s && o < end; ++s) {
        int slashes = 0;
        while (s[slashes] == L'\\') ++slashes;
        if (slashes && (s[slashes] == L'"' || s[slashes] == 0)) {
            for (int i = 0; i < 2 * slashes && o < end; ++i) *o++ = L'\\';
            s += slashes - 1;
            continue;
        }
        if (*s == L'"' && o + 1 < end) *o++ = L'\\';
        *o++ = *s;
    }
    *o++ = L'"';
    *o = 0;
}

static void append(const wchar_t* s) { lstrcpynW(g_cmd + lstrlenW(g_cmd), s, (int)(sizeof g_cmd / sizeof g_cmd[0]) - lstrlenW(g_cmd)); }

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR args, int show) {
    (void)inst, (void)prev, (void)args, (void)show;
    DWORD n = GetModuleFileNameW(NULL, g_dir, MAX_TEXT);
    if (n == 0 || n >= MAX_TEXT) return fail(L"Cannot find the folder this program is in.", NULL);
    for (wchar_t* p = g_dir + n; p > g_dir; --p)
        if (p[-1] == L'\\' || p[-1] == L'/') {
            *p = 0;
            break;
        }

    static wchar_t config[MAX_TEXT];
    lstrcpynW(config, g_dir, MAX_TEXT);
    lstrcatW(config, L"config\\launch.txt");
    HANDLE f = CreateFileW(config, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return fail(L"The player's config\\launch.txt is missing:", config);
    static char text[MAX_TEXT];
    DWORD got = 0;
    BOOL read = ReadFile(f, text, MAX_TEXT - 1, &got, NULL);
    CloseHandle(f);
    if (!read) return fail(L"Cannot read the player's config\\launch.txt:", config);
    text[got] = 0;

    // Three lines, a UTF-8 BOM and \r allowed.
    char* line = text;
    if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) line += 3;
    for (int i = 0; i < 3; ++i) {
        char* eol = line;
        while (*eol && *eol != '\n') ++eol;
        char* next = *eol ? eol + 1 : eol;
        if (eol > line && eol[-1] == '\r') --eol;
        *eol = 0;
        if (!*line) return fail(L"The player's config\\launch.txt does not name the program, the disc and the config folder:", config);
        lstrcpynW(g_paths[i], g_dir, MAX_TEXT);
        int len = lstrlenW(g_paths[i]);
        int w = MultiByteToWideChar(CP_UTF8, 0, line, -1, g_paths[i] + len, MAX_TEXT - len);
        if (w == 0) return fail(L"The player's config\\launch.txt is not readable text:", config);
        for (wchar_t* p = g_paths[i] + len; *p; ++p)
            if (*p == L'/') *p = L'\\';
        line = next;
    }

    static const wchar_t* const missing[3] = {L"The player's pcsx-redux is missing:", L"The player's disc image is missing:",
                                              L"The player's config folder is missing:"};
    for (int i = 0; i < 3; ++i) {
        DWORD a = GetFileAttributesW(g_paths[i]);
        if (a == INVALID_FILE_ATTRIBUTES || (i == 2) != ((a & FILE_ATTRIBUTE_DIRECTORY) != 0)) return fail(missing[i], g_paths[i]);
    }

    g_cmd[0] = 0;
    appendQuoted(g_paths[0]);
    append(L" -kiosk -run -iso ");
    appendQuoted(g_paths[1]);
    append(L" -portable ");
    appendQuoted(g_paths[2]);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessW(g_paths[0], g_cmd, NULL, NULL, FALSE, 0, NULL, g_dir, &si, &pi)) {
        wsprintfW(g_msg, L"pcsx-redux did not start (error %lu):", GetLastError());
        static wchar_t what[256];
        lstrcpynW(what, g_msg, 256);
        return fail(what, g_paths[0]);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
