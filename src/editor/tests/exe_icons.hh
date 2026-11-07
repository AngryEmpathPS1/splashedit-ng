// A program's icons read back as Explorer finds them (LoadLibraryExW as data,
// FindResourceW), for the test of splash::setExeIcon. Windows only;
// windows.h comes first.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

struct ExeIcons {
    // Each image icon group 1 lists: its width, or -1 where its RT_ICON is
    // missing or is not a 32-bit BITMAPINFOHEADER image of that size.
    std::vector<int> sizes;
    int icons = 0;            // RT_ICON resources in the program, listed or not
    uint32_t topLeft = 0;     // the first image's top-left texel, as BGRA bytes
    bool topRightClear = false;  // its top-right texel set in the AND mask
    bool manifest = false;       // an RT_MANIFEST 1 (what MSVC's linker embeds)
};

inline BOOL CALLBACK exeIconsCount(HMODULE, LPCWSTR, LPWSTR, LONG_PTR p) {
    ++*reinterpret_cast<int*>(p);
    return TRUE;
}

inline ExeIcons exeIcons(const wchar_t* exe) {
    ExeIcons out;
    HMODULE m = LoadLibraryExW(exe, nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!m) return out;
    EnumResourceNamesW(m, MAKEINTRESOURCEW(3), exeIconsCount, reinterpret_cast<LONG_PTR>(&out.icons));
    out.manifest = FindResourceW(m, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24)) != nullptr;
    auto data = [&](LPCWSTR type, WORD id, DWORD& size) -> const uint8_t* {
        HRSRC r = FindResourceW(m, MAKEINTRESOURCEW(id), type);
        size = r ? SizeofResource(m, r) : 0;
        return r ? static_cast<const uint8_t*>(LockResource(LoadResource(m, r))) : nullptr;
    };
    DWORD gsize = 0;
    const uint8_t* g = data(MAKEINTRESOURCEW(14), 1, gsize);
    const int count = g && gsize >= 6 ? g[4] | g[5] << 8 : 0;
    for (int i = 0; i < count && 6 + 14 * DWORD(i + 1) <= gsize; ++i) {
        const uint8_t* e = g + 6 + 14 * i;
        const int w = e[0] ? e[0] : 256;
        DWORD bytes = 0, size = 0;
        std::memcpy(&bytes, e + 8, 4);
        const uint8_t* d = data(MAKEINTRESOURCEW(3), WORD(e[12] | e[13] << 8), size);
        uint32_t head = 0;
        int32_t hw = 0, hh = 0;
        uint16_t bits = 0;
        if (d && size >= 40) {
            std::memcpy(&head, d, 4);
            std::memcpy(&hw, d + 4, 4);
            std::memcpy(&hh, d + 8, 4);
            std::memcpy(&bits, d + 14, 2);
        }
        const bool ok = d && size == bytes && head == 40 && hw == w && hh == 2 * w && bits == 32 &&
                        size >= 40 + DWORD(w * w * 4 + w * ((w + 31) / 32 * 4));
        out.sizes.push_back(ok ? w : -1);
        if (ok && i == 0) {
            // Rows run bottom up: the top row is the last, in the colours and in the mask.
            std::memcpy(&out.topLeft, d + 40 + size_t(w - 1) * w * 4, 4);
            const uint8_t* mask = d + 40 + size_t(w) * w * 4 + size_t(w - 1) * ((w + 31) / 32 * 4);
            out.topRightClear = (mask[(w - 1) / 8] & (0x80 >> ((w - 1) % 8))) != 0;
        }
    }
    FreeLibrary(m);
    return out;
}
