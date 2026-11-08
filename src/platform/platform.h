#ifndef QLCG_PLATFORM_H
#define QLCG_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

typedef struct {
    int year, month, day, hour, minute, second, millisecond;
} QlLocalTime;

typedef struct QlDirectory QlDirectory;
typedef struct {
    char name[1024];
    bool directory;
    uint64_t size, modified;
} QlDirectoryEntry;

double qlcg_platform_now_ms(void);
void qlcg_platform_local_time(QlLocalTime *out);
int qlcg_platform_local_offset(void);
bool qlcg_platform_date(uint64_t modified, QlLocalTime *out);
bool qlcg_platform_sync(FILE *file);
FILE *qlcg_platform_fopen(const char *path, const char *mode);
bool qlcg_platform_exists(const char *path);
bool qlcg_platform_mkdir(const char *path);
int qlcg_platform_remove(const char *path);
int qlcg_platform_replace(const char *from, const char *to);
bool qlcg_platform_copy(const char *from, const char *to);
QlDirectory *qlcg_platform_dir_open(const char *path);
bool qlcg_platform_dir_next(QlDirectory *dir, QlDirectoryEntry *entry);
void qlcg_platform_dir_close(QlDirectory *dir);
bool qlcg_platform_key_down(unsigned key);
bool qlcg_platform_dark(void);
bool qlcg_platform_clip_get(char *dst, size_t cap);
bool qlcg_platform_clip_set(const char *text);
bool qlcg_platform_window_open(int width, int height);
void qlcg_platform_window_close(void);
void qlcg_platform_poll(void);
void qlcg_platform_present(void);
void qlcg_platform_capture(void);
void qlcg_platform_prepare(void);
size_t qlcg_utf8_to_wide(const char *src, wchar_t *dst, size_t cap);
size_t qlcg_wide_to_utf8(const wchar_t *src, char *dst, size_t cap);
int qlcg_stricmp(const char *a, const char *b);
int qlcg_strnicmp(const char *a, const char *b, size_t count);

#endif
