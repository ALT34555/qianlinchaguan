#ifdef __ANDROID__
#include "platform/platform.h"
#include "platform/paths.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct QlDirectory { DIR *dir; };

static void unpack_time(const struct tm *s, QlLocalTime *out) {
    *out = (QlLocalTime){s->tm_year + 1900, s->tm_mon + 1, s->tm_mday, s->tm_hour, s->tm_min, s->tm_sec, 0};
}

double qlcg_platform_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}

void qlcg_platform_local_time(QlLocalTime *out) {
    struct timespec t;
    struct tm local;
    clock_gettime(CLOCK_REALTIME, &t);
    localtime_r(&t.tv_sec, &local);
    unpack_time(&local, out);
    out->millisecond = (int)(t.tv_nsec / 1000000);
}

int qlcg_platform_local_offset(void) {
    time_t t = time(NULL);
    struct tm local;
    return localtime_r(&t, &local) ? (int)(local.tm_gmtoff / 60) : 0;
}

bool qlcg_platform_date(uint64_t modified, QlLocalTime *out) {
    time_t t = (time_t)modified;
    struct tm local;
    if (!localtime_r(&t, &local)) return false;
    unpack_time(&local, out);
    return true;
}

bool qlcg_platform_sync(FILE *file) { return fflush(file)==0 && fsync(fileno(file))==0; }

FILE *qlcg_platform_fopen(const char *path, const char *mode) {
    char resolved[4096];
    if(!qlcg_path_resolve(path,strpbrk(mode,"wa+")!=NULL,resolved,sizeof(resolved))) return NULL;
    path=resolved; return fopen(path, mode); }
bool qlcg_platform_exists(const char *path) {
    char resolved[4096];
    if(!qlcg_path_resolve(path,false,resolved,sizeof(resolved))) return false;
    path=resolved; struct stat s; return stat(path, &s) == 0; }
bool qlcg_platform_mkdir(const char *path) {
    char resolved[4096];
    if(!qlcg_path_resolve(path,true,resolved,sizeof(resolved))) return false;
    path=resolved;
    struct stat s;
    return mkdir(path, 0700) == 0 || (stat(path, &s) == 0 && S_ISDIR(s.st_mode));
}
int qlcg_platform_remove(const char *path) {
    char resolved[4096];
    if(!qlcg_path_resolve(path,true,resolved,sizeof(resolved))) return -1;
    path=resolved; return unlink(path); }
int qlcg_platform_replace(const char *from, const char *to) {
    char source[4096],destination[4096];
    if(!qlcg_path_resolve(from,false,source,sizeof(source)) || !qlcg_path_resolve(to,true,destination,sizeof(destination))) return -1;
    from=source; to=destination; return rename(from, to); }

bool qlcg_platform_copy(const char *from, const char *to) {
    char source[4096],destination[4096];
    if(!qlcg_path_resolve(from,false,source,sizeof(source)) || !qlcg_path_resolve(to,true,destination,sizeof(destination))) return false;
    from=source; to=destination;
    FILE *in = fopen(from, "rb");
    if (!in) return false;
    int fd = open(to, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) { fclose(in); return false; }
    FILE *out = fdopen(fd, "wb");
    if (!out) { close(fd); unlink(to); fclose(in); return false; }
    char buf[16384];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) != 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (ferror(in)) ok = false;
    if (fclose(out) != 0) ok = false;
    fclose(in);
    if (!ok) unlink(to);
    return ok;
}

QlDirectory *qlcg_platform_dir_open(const char *path) {
    char resolved[4096];
    if(!qlcg_path_resolve(path,false,resolved,sizeof(resolved))) return NULL;
    path=resolved;
    DIR *raw = opendir(path);
    if (!raw) return NULL;
    QlDirectory *d = malloc(sizeof(*d));
    if (!d) { closedir(raw); return NULL; }
    d->dir = raw;
    return d;
}

bool qlcg_platform_dir_next(QlDirectory *dir, QlDirectoryEntry *entry) {
    if (!dir || !entry) return false;
    struct dirent *d;
    struct stat st;
    while ((d = readdir(dir->dir)) != NULL) {
        if (fstatat(dirfd(dir->dir), d->d_name, &st, 0) != 0) continue;
        snprintf(entry->name, sizeof(entry->name), "%s", d->d_name);
        entry->directory = S_ISDIR(st.st_mode);
        entry->size = (uint64_t)st.st_size;
        entry->modified = (uint64_t)st.st_mtime;
        return true;
    }
    return false;
}

void qlcg_platform_dir_close(QlDirectory *dir) {
    if (dir) { closedir(dir->dir); free(dir); }
}
#endif
