#ifdef _WIN32
#include "platform/win32/native.h"
#include "platform/platform.h"
/* 剪贴板 UTF-8⇄CF_UNICODETEXT */
#include "ui/ui_input.h"
#include <windows.h>

/* 打开剪贴板；被别的进程占用时短暂重试 */
static bool clip_open(void) {
    for (int i = 0; i < 4; i++) {
        if (OpenClipboard(NULL)) return true;
        Sleep(4);
    }
    return false;
}

/* 读取失败时清空输出 */
bool qlcg_platform_clip_get(char *dst, size_t cap) {
    if (!dst || cap < 2) return false;
    dst[0] = '\0';
    if (!clip_open()) return false;
    bool ok = false;
    HANDLE h = IsClipboardFormatAvailable(CF_UNICODETEXT) ? GetClipboardData(CF_UNICODETEXT)
                                                          : NULL;
    if (h) {
        const wchar_t *w = (const wchar_t *)GlobalLock(h);
        if (w) {
            int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, dst, (int)cap, NULL, NULL);
            GlobalUnlock(h);
            if (n > 0) {
                dst[cap - 1] = '\0';
                ui_u8_trim(dst);
                ok = dst[0] != '\0';
            }
        }
    }
    CloseClipboard();
    if (!ok) dst[0] = '\0';
    return ok;
}

/* 写剪贴板纯文本；utf8 为 UTF-8 */
bool qlcg_platform_clip_set(const char *utf8) {
    if (!utf8 || !utf8[0]) return false;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen <= 0) return false;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wlen * sizeof(wchar_t));
    if (!h) return false;
    wchar_t *w = (wchar_t *)GlobalLock(h);
    if (!w) { GlobalFree(h); return false; }
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
    GlobalUnlock(h);

    if (!clip_open()) { GlobalFree(h); return false; }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_UNICODETEXT, h) != NULL;
    CloseClipboard();
    if (!ok) GlobalFree(h);   /* 所有权未转移 */
    return ok;
}

#endif
