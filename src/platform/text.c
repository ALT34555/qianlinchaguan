#include "platform.h"
#include <ctype.h>
#include <limits.h>

size_t qlcg_utf8_to_wide(const char *src, wchar_t *dst, size_t cap) {
    size_t n = 0;
    if (!src || !dst || !cap) return 0;
    dst[0] = 0;
    while (*src) {
        uint32_t cp = (unsigned char)*src++;
        int more = 0;
        uint32_t min = 0;
        if (cp >= 0xC2 && cp <= 0xDF) { cp &= 31; more = 1; min = 0x80; }
        else if (cp >= 0xE0 && cp <= 0xEF) { cp &= 15; more = 2; min = 0x800; }
        else if (cp >= 0xF0 && cp <= 0xF4) { cp &= 7; more = 3; min = 0x10000; }
        else if (cp >= 0x80) goto invalid;
        for (int i = 0; i < more; ++i) {
            unsigned char ch = (unsigned char)*src;
            if ((ch & 0xC0) != 0x80) goto invalid;
            cp = (cp << 6) | (ch & 63);
            ++src;
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) goto invalid;
        if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
            if (n + 2 >= cap) goto invalid;
            cp -= 0x10000;
            dst[n++] = (wchar_t)(0xD800 | (cp >> 10));
            dst[n++] = (wchar_t)(0xDC00 | (cp & 1023));
        } else {
            if (n + 1 >= cap) goto invalid;
            dst[n++] = (wchar_t)cp;
        }
    }
    dst[n] = 0;
    return n + 1;
invalid:
    dst[0] = 0;
    return 0;
}

size_t qlcg_wide_to_utf8(const wchar_t *src, char *dst, size_t cap) {
    size_t n = 0;
    if (!src || !dst || !cap) return 0;
    dst[0] = 0;
    while (*src) {
        uint32_t cp = (uint32_t)*src++;
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF) {
            uint32_t lo = (uint32_t)*src;
            if (lo < 0xDC00 || lo > 0xDFFF) goto invalid;
            ++src;
            cp = 0x10000 + ((cp - 0xD800) << 10) + lo - 0xDC00;
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) goto invalid;
        int bytes = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (n + (size_t)bytes >= cap) goto invalid;
        if (bytes == 1) dst[n++] = (char)cp;
        else {
            dst[n++] = (char)((bytes == 2 ? 0xC0 : bytes == 3 ? 0xE0 : 0xF0) | (cp >> (6 * (bytes - 1))));
            for (int i = bytes - 2; i >= 0; --i) dst[n++] = (char)(0x80 | ((cp >> (6 * i)) & 63));
        }
    }
    dst[n] = 0;
    return n + 1;
invalid:
    dst[0] = 0;
    return 0;
}

int qlcg_strnicmp(const char *a, const char *b, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
        if (x != y || !x) return x - y;
    }
    return 0;
}

int qlcg_stricmp(const char *a, const char *b) {
    return qlcg_strnicmp(a, b, SIZE_MAX);
}
