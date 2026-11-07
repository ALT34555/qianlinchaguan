/* mod 字体：扫目录注册，族名以字体文件为准 */
#include "ui_font_mod.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* 中文系统含 @ 竖排族名，实测已超 512，留足余量 */
#define FAM_CAP      4096
#define MANIFEST_CAP 64

/* ---- 极简 JSON 字段提取 ---- */

/* 在区间内取 "key":"value" 字符串值 */
static bool span_str(const char *base, size_t len, const char *key, char *out, int cap) {
    size_t klen = strlen(key);
    if (cap <= 0) return false;
    for (size_t i = 0; i + klen + 2 <= len; i++) {
        if (base[i] != '"') continue;
        if (memcmp(base + i + 1, key, klen) != 0) continue;
        if (base[i + 1 + klen] != '"') continue;
        size_t p = i + klen + 2;
        while (p < len && base[p] != ':') { if (base[p] == '}') return false; p++; }
        if (p >= len) return false;
        p++;
        while (p < len && base[p] != '"' && base[p] != '}') p++;
        if (p >= len || base[p] != '"') return false;
        p++;
        int n = 0;
        while (p < len && base[p] != '"' && base[p] != '\\' && n < cap - 1) out[n++] = base[p++];
        out[n] = '\0';
        return n > 0;
    }
    return false;
}

static void utf8_to_w(const char *src, wchar_t *dst, int cap) {
    if (cap <= 0) return;
    if (!src || !src[0]) { dst[0] = L'\0'; return; }
    MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, cap);
    dst[cap - 1] = L'\0';
}

typedef struct { char file[260]; char id[96]; char label[96]; } ManifestEntry;

/* 读 fonts.json：按 file 切块取 id */
static int read_manifest(const wchar_t *dir, ManifestEntry *out, int cap) {
    wchar_t path[300];
    swprintf_s(path, 300, L"%s\\fonts.json", dir);
    FILE *fp = _wfopen(path, L"rb");
    if (!fp) return 0;
    static char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';

    size_t starts[MANIFEST_CAP];
    int ns = 0;
    for (size_t i = 0; i + 6 <= n && ns < MANIFEST_CAP; i++) {
        if (memcmp(buf + i, "\"file\"", 6) == 0) starts[ns++] = i;
    }

    int cnt = 0;
    for (int k = 0; k < ns && cnt < cap; k++) {
        size_t lo = starts[k];
        while (lo > 0 && buf[lo] != '{') lo--;
        size_t hi = starts[k];
        while (hi < n && buf[hi] != '}') hi++;
        if (hi < n) hi++;
        if (hi <= lo) continue;
        ManifestEntry *e = &out[cnt];
        e->file[0] = '\0'; e->id[0] = '\0'; e->label[0] = '\0';
        span_str(buf + lo, hi - lo, "file",  e->file,  sizeof(e->file));
        span_str(buf + lo, hi - lo, "id",    e->id,    sizeof(e->id));
        span_str(buf + lo, hi - lo, "label", e->label, sizeof(e->label));
        if (e->file[0]) cnt++;
    }
    return cnt;
}

/* ---- 私有字体注册 ---- */

static wchar_t g_added[UI_FONT_MOD_MAX][260];
static int     g_added_count = 0;

static bool has_suffix(const wchar_t *name, const wchar_t *ext) {
    size_t n = wcslen(name), e = wcslen(ext);
    if (n <= e) return false;
    return _wcsicmp(name + n - e, ext) == 0;
}

static bool is_font_file(const wchar_t *name) {
    return has_suffix(name, L".ttf") || has_suffix(name, L".otf") || has_suffix(name, L".ttc");
}

/* ---------- 族名枚举 ---------- */

typedef struct {
    wchar_t names[FAM_CAP][LF_FACESIZE];
    int     count;
} FamilySet;

static int CALLBACK enum_family_cb(const LOGFONTW *lf, const TEXTMETRICW *tm,
                                   DWORD type, LPARAM lp) {
    (void)tm; (void)type;
    FamilySet *fs = (FamilySet *)lp;
    for (int i = 0; i < fs->count; i++) {
        if (wcscmp(fs->names[i], lf->lfFaceName) == 0) return 1;
    }
    if (fs->count < FAM_CAP) {
        wcscpy_s(fs->names[fs->count], LF_FACESIZE, lf->lfFaceName);
        fs->count++;
    }
    return 1;
}

static void enum_families(FamilySet *fs) {
    fs->count = 0;
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) return;
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)enum_family_cb, (LPARAM)fs, 0);
    DeleteDC(dc);
}

static bool family_in(const FamilySet *fs, const wchar_t *name) {
    for (int i = 0; i < fs->count; i++) {
        if (wcscmp(fs->names[i], name) == 0) return true;
    }
    return false;
}

static int cmp_name(const void *a, const void *b) {
    return _wcsicmp((const wchar_t *)a, (const wchar_t *)b);
}

/* ---- 读 TTF name 表取族名 ---- */

static unsigned rd_be16(const unsigned char *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

static unsigned long rd_be32(const unsigned char *p) {
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

/* 取候选族名：nameID 1/16 英文与简中 */
static int ttf_family_names(const wchar_t *path, wchar_t out[][64], int cap) {
    if (cap <= 0) return 0;
    FILE *fp = _wfopen(path, L"rb");
    if (!fp) return 0;
    unsigned char hdr[12];
    if (fread(hdr, 1, 12, fp) != 12) { fclose(fp); return 0; }
    if (memcmp(hdr, "ttcf", 4) == 0) { fclose(fp); return 0; }  /* 字体集合暂不处理 */
    unsigned numt = rd_be16(hdr + 4);
    if (numt == 0 || numt > 512) { fclose(fp); return 0; }

    unsigned long name_off = 0, name_len = 0;
    for (unsigned i = 0; i < numt; i++) {
        unsigned char rec[16];
        if (fread(rec, 1, 16, fp) != 16) break;
        if (memcmp(rec, "name", 4) == 0) {
            name_off = rd_be32(rec + 8);
            name_len = rd_be32(rec + 12);
            break;
        }
    }
    if (name_off == 0 || name_len < 6 || name_len > (1u << 20)) { fclose(fp); return 0; }

    unsigned char *tbl = (unsigned char *)malloc(name_len);
    if (!tbl) { fclose(fp); return 0; }
    if (fseek(fp, (long)name_off, SEEK_SET) != 0 ||
        fread(tbl, 1, name_len, fp) != name_len) {
        free(tbl); fclose(fp); return 0;
    }
    fclose(fp);

    int n = 0;
    unsigned count = rd_be16(tbl + 2);
    unsigned str_off = rd_be16(tbl + 4);
    for (unsigned i = 0; i < count && n < cap; i++) {
        unsigned long r = 6 + (unsigned long)i * 12;
        if (r + 12 > name_len) break;
        unsigned pid = rd_be16(tbl + r);
        unsigned lid = rd_be16(tbl + r + 4);
        unsigned nid = rd_be16(tbl + r + 6);
        unsigned slen = rd_be16(tbl + r + 8);
        unsigned soff = rd_be16(tbl + r + 10);
        if (nid != 1 && nid != 16) continue;
        if (pid != 3) continue;                        /* Windows UTF-16BE */
        if (lid != 0x0409 && lid != 0x0804) continue;  /* 英文 / 简体中文 */
        unsigned long s = str_off + soff;
        if (slen == 0 || s + slen > name_len) continue;
        wchar_t cand[64];
        int k = 0;
        for (unsigned j = 0; j + 1 < slen && k < 63; j += 2) {
            cand[k++] = (wchar_t)rd_be16(tbl + s + j);
        }
        cand[k] = L'\0';
        if (k == 0) continue;
        bool dup = false;
        for (int q = 0; q < n; q++) {
            if (wcscmp(out[q], cand) == 0) { dup = true; break; }
        }
        if (dup) continue;
        wcscpy_s(out[n++], 64, cand);
    }
    free(tbl);
    return n;
}

/* GDI 实测：CreateFontW 后族名不变 */
static bool family_resolves(const wchar_t *name) {
    if (!name || !name[0]) return false;
    HFONT f = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH, name);
    if (!f) return false;
    LOGFONTW got;
    memset(&got, 0, sizeof(got));
    bool ok = GetObjectW(f, sizeof(got), &got) == (int)sizeof(got) &&
              _wcsicmp(got.lfFaceName, name) == 0;
    DeleteObject(f);
    return ok;
}

/* 列目录字体文件，按名排序对齐 base */
static int collect_files(const wchar_t *dir, wchar_t names[][260], int cap) {
    wchar_t pattern[300];
    swprintf_s(pattern, 300, L"%s\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!is_font_file(fd.cFileName)) continue;
        if (n < cap) wcscpy_s(names[n++], 260, fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (n > 1) qsort(names, (size_t)n, sizeof(names[0]), cmp_name);
    return n;
}

/* ---------- 主流程 ---------- */

int ui_font_mod_scan(const wchar_t *dir, UiModFontList *list) {
    if (list) list->count = 0;
    if (!dir || !dir[0]) return 0;

    ManifestEntry man[MANIFEST_CAP];
    int man_n = read_manifest(dir, man, MANIFEST_CAP);

    static wchar_t names[UI_FONT_MOD_MAX][260];
    int file_n = collect_files(dir, names, UI_FONT_MOD_MAX);
    if (file_n <= 0) return 0;

    FamilySet *pre  = (FamilySet *)malloc(sizeof(FamilySet));
    FamilySet *post = (FamilySet *)malloc(sizeof(FamilySet));
    if (!pre || !post) { free(pre); free(post); return 0; }

    int found = 0;
    for (int i = 0; i < file_n; i++) {
        wchar_t full[300];
        swprintf_s(full, 300, L"%s\\%s", dir, names[i]);

        enum_families(pre);
        if (AddFontResourceExW(full, FR_PRIVATE, 0) <= 0) {
            printf("[FontMod] register failed: %ls\n", names[i]);
        } else if (g_added_count < UI_FONT_MOD_MAX) {
            wcscpy_s(g_added[g_added_count++], 260, full);
        }
        enum_families(post);

        /* 族名优先级：枚举差集＞name 表＞文件名 */
        wchar_t family[64] = { 0 };
        for (int j = 0; j < post->count && !family[0]; j++) {
            if (!family_in(pre, post->names[j]) && family_resolves(post->names[j])) {
                wcscpy_s(family, 64, post->names[j]);
            }
        }
        if (!family[0]) {
            wchar_t cands[8][64];
            int cn = ttf_family_names(full, cands, 8);
            for (int c = 0; c < cn && !family[0]; c++) {
                if (family_resolves(cands[c])) wcscpy_s(family, 64, cands[c]);
            }
            if (!family[0] && cn > 0) wcscpy_s(family, 64, cands[0]);
        }
        if (!family[0]) {
            printf("[FontMod] no family name: %ls\n", names[i]);
            continue;
        }
        found++;
        if (!list || list->count >= UI_FONT_MOD_MAX) continue;

        UiModFont *f = &list->items[list->count++];
        memset(f, 0, sizeof(*f));
        wcscpy_s(f->family, 64, family);
        wcscpy_s(f->file,   260, full);

        char name_u8[260];
        WideCharToMultiByte(CP_UTF8, 0, names[i], -1, name_u8, sizeof(name_u8), NULL, NULL);
        const ManifestEntry *m = NULL;
        for (int k = 0; k < man_n; k++) {
            if (_stricmp(man[k].file, name_u8) == 0) { m = &man[k]; break; }
        }
        if (m && m->id[0]) {
            strncpy_s(f->id, sizeof(f->id), m->id, sizeof(f->id) - 1);
        } else {
            snprintf(f->id, sizeof(f->id), "mod:%s", name_u8);
        }
        if (m && m->label[0]) {
            utf8_to_w(m->label, f->label, 48);
        } else {
            /* 无清单时 label 取文件名主干（对齐 base） */
            wcscpy_s(f->label, 48, names[i]);
            wchar_t *dot = wcsrchr(f->label, L'.');
            if (dot && dot != f->label) *dot = L'\0';
        }
    }
    free(pre);
    free(post);
    return found;
}

const UiModFont *ui_font_mod_find(const UiModFontList *list, const char *id) {
    if (!list || !id) return NULL;
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].id, id) == 0) return &list->items[i];
    }
    return NULL;
}

void ui_font_mod_unload(void) {
    for (int i = 0; i < g_added_count; i++) {
        RemoveFontResourceExW(g_added[i], FR_PRIVATE, 0);
    }
    g_added_count = 0;
}
