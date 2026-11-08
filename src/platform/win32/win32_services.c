#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "platform/platform.h"
#include "platform/paths.h"
#include <stdlib.h>
#include <io.h>
#include <string.h>

struct QlDirectory {
    HANDLE handle;
    WIN32_FIND_DATAW data;
    bool first;
};

static void unpack_time(const SYSTEMTIME *s, QlLocalTime *out) {
    *out = (QlLocalTime){s->wYear, s->wMonth, s->wDay, s->wHour, s->wMinute, s->wSecond, s->wMilliseconds};
}

double qlcg_platform_now_ms(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
}

void qlcg_platform_local_time(QlLocalTime *out) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    unpack_time(&st, out);
}

int qlcg_platform_local_offset(void) {
    SYSTEMTIME utc, local;
    FILETIME fu, fl;
    GetSystemTime(&utc);
    if (!SystemTimeToTzSpecificLocalTime(NULL, &utc, &local) ||
        !SystemTimeToFileTime(&utc, &fu) || !SystemTimeToFileTime(&local, &fl)) return 0;
    uint64_t a = ((uint64_t)fu.dwHighDateTime << 32) | fu.dwLowDateTime;
    uint64_t b = ((uint64_t)fl.dwHighDateTime << 32) | fl.dwLowDateTime;
    return (int)(((int64_t)b - (int64_t)a) / 600000000);
}

bool qlcg_platform_date(uint64_t modified, QlLocalTime *out) {
    FILETIME ft = {(DWORD)modified, (DWORD)(modified >> 32)};
    SYSTEMTIME utc, local;
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(NULL, &utc, &local)) return false;
    unpack_time(&local, out);
    return true;
}

bool qlcg_platform_sync(FILE *file) { return fflush(file)==0 && _commit(_fileno(file))==0; }

FILE *qlcg_platform_fopen(const char *path, const char *mode) {
    char resolved[4096];
    if(!qlcg_path_resolve(path,strpbrk(mode,"wa+")!=NULL,resolved,sizeof(resolved))) return NULL;
    path=resolved;
    wchar_t p[2048], m[16];
    if (!qlcg_utf8_to_wide(path, p, 2048) || !qlcg_utf8_to_wide(mode, m, 16)) return NULL;
    return _wfopen(p, m);
}

bool qlcg_platform_exists(const char *path) {
    char resolved[4096];
    if(!qlcg_path_resolve(path,false,resolved,sizeof(resolved))) return false;
    path=resolved;
    wchar_t p[2048];
    return qlcg_utf8_to_wide(path, p, 2048) && GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

bool qlcg_platform_mkdir(const char *path) {
    char resolved[4096];
    if(!qlcg_path_resolve(path,true,resolved,sizeof(resolved))) return false;
    path=resolved;
    wchar_t p[2048];
    if (!qlcg_utf8_to_wide(path, p, 2048)) return false;
    if (CreateDirectoryW(p, NULL)) return true;
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

int qlcg_platform_remove(const char *path) {
    char resolved[4096];
    if(!qlcg_path_resolve(path,true,resolved,sizeof(resolved))) return -1;
    path=resolved;
    wchar_t p[2048];
    return qlcg_utf8_to_wide(path, p, 2048) && DeleteFileW(p) ? 0 : -1;
}

int qlcg_platform_replace(const char *from, const char *to) {
    char source[4096],destination[4096];
    if(!qlcg_path_resolve(from,false,source,sizeof(source)) || !qlcg_path_resolve(to,true,destination,sizeof(destination))) return -1;
    from=source; to=destination;
    wchar_t a[2048], b[2048];
    return qlcg_utf8_to_wide(from, a, 2048) && qlcg_utf8_to_wide(to, b, 2048) &&
        MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}

bool qlcg_platform_copy(const char *from, const char *to) {
    char source[4096],destination[4096];
    if(!qlcg_path_resolve(from,false,source,sizeof(source)) || !qlcg_path_resolve(to,true,destination,sizeof(destination))) return false;
    from=source; to=destination;
    wchar_t a[2048], b[2048];
    return qlcg_utf8_to_wide(from, a, 2048) && qlcg_utf8_to_wide(to, b, 2048) && CopyFileW(a, b, TRUE);
}

QlDirectory *qlcg_platform_dir_open(const char *path) {
    char resolved[4096];
    if(!qlcg_path_resolve(path,false,resolved,sizeof(resolved))) return NULL;
    path=resolved;
    char pattern[2048];
    wchar_t wide[2048];
    int n = snprintf(pattern, sizeof(pattern), "%s/*", path);
    if (n < 0 || n >= sizeof(pattern) || !qlcg_utf8_to_wide(pattern, wide, 2048)) return NULL;
    QlDirectory *dir = calloc(1, sizeof(*dir));
    if (!dir) return NULL;
    dir->handle = FindFirstFileW(wide, &dir->data);
    if (dir->handle == INVALID_HANDLE_VALUE) { free(dir); return NULL; }
    dir->first = true;
    return dir;
}

bool qlcg_platform_dir_next(QlDirectory *dir, QlDirectoryEntry *entry) {
    if (!dir || !entry) return false;
    do {
        if (!dir->first && !FindNextFileW(dir->handle, &dir->data)) return false;
        dir->first = false;
    } while (!qlcg_wide_to_utf8(dir->data.cFileName, entry->name, sizeof(entry->name)));
    entry->directory = (dir->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    entry->size = ((uint64_t)dir->data.nFileSizeHigh << 32) | dir->data.nFileSizeLow;
    entry->modified = ((uint64_t)dir->data.ftLastWriteTime.dwHighDateTime << 32) | dir->data.ftLastWriteTime.dwLowDateTime;
    return true;
}

void qlcg_platform_dir_close(QlDirectory *dir) {
    if (!dir) return;
    FindClose(dir->handle);
    free(dir);
}

bool qlcg_platform_key_down(unsigned key) { return (GetKeyState((int)key) & 0x8000) != 0; }

bool qlcg_platform_dark(void) {
    HKEY key;
    DWORD value = 1, size = sizeof(value), type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    RegQueryValueExW(key, L"AppsUseLightTheme", NULL, &type, (LPBYTE)&value, &size);
    RegCloseKey(key);
    return value == 0;
}
#endif
