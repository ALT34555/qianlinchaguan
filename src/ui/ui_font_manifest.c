#include "ui_font_manifest.h"
#include "platform/platform.h"
#include <string.h>
#include <stdio.h>
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


int ui_font_manifest_read(const char *dir, UiFontManifest *out, int cap) {
    char path[4096];
    if (snprintf(path,sizeof(path),"%s/fonts.json",dir)>=(int)sizeof(path)) return 0;
    FILE *fp = qlcg_platform_fopen(path,"rb");
    if (!fp) return 0;
    char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';

    size_t starts[UI_FONT_MANIFEST_CAP];
    int ns = 0;
    for (size_t i = 0; i + 6 <= n && ns < UI_FONT_MANIFEST_CAP; i++) {
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
        UiFontManifest *e = &out[cnt];
        e->file[0] = '\0'; e->id[0] = '\0'; e->label[0] = '\0';
        span_str(buf + lo, hi - lo, "file",  e->file,  sizeof(e->file));
        span_str(buf + lo, hi - lo, "id",    e->id,    sizeof(e->id));
        span_str(buf + lo, hi - lo, "label", e->label, sizeof(e->label));
        if (e->file[0]) cnt++;
    }
    return cnt;
}
