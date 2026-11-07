/* 稀疏参数/色板字典实现 */
#include "pd_dict.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *out = (char *)malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

void pd_init(PdDict *d) {
    d->e = NULL;
    d->count = 0;
    d->cap = 0;
}

void pd_free(PdDict *d) {
    for (int i = 0; i < d->count; i++) {
        free(d->e[i].key);
        free(d->e[i].str);
    }
    free(d->e);
    d->e = NULL;
    d->count = 0;
    d->cap = 0;
}

static PdEntry *find(PdDict *d, const char *key) {
    if (!d || !key) return NULL;
    for (int i = 0; i < d->count; i++) {
        if (d->e[i].key && strcmp(d->e[i].key, key) == 0) return &d->e[i];
    }
    return NULL;
}

static PdEntry *slot(PdDict *d, const char *key) {
    PdEntry *e = find(d, key);
    if (e) return e;
    if (d->count >= d->cap) {
        int next = d->cap ? d->cap * 2 : 16;
        PdEntry *ne = (PdEntry *)realloc(d->e, (size_t)next * sizeof(PdEntry));
        if (!ne) return NULL;
        d->e = ne;
        d->cap = next;
    }
    e = &d->e[d->count++];
    memset(e, 0, sizeof(*e));
    e->key = dup_str(key);
    return e;
}

void pd_set_num(PdDict *d, const char *key, double v) {
    PdEntry *e = slot(d, key);
    if (!e) return;
    free(e->str);
    e->str = NULL;
    e->num = v;
    e->is_num = 1;
}

void pd_set_str(PdDict *d, const char *key, const char *v) {
    PdEntry *e = slot(d, key);
    if (!e) return;
    free(e->str);
    e->str = dup_str(v);
    e->num = 0.0;
    e->is_num = 0;
}

int pd_has(const PdDict *d, const char *key) {
    return find((PdDict *)d, key) != NULL;
}

double pd_num(const PdDict *d, const char *key, double dflt) {
    PdEntry *e = find((PdDict *)d, key);
    if (!e || !e->is_num) return dflt;
    return e->num;
}

const char *pd_str(const PdDict *d, const char *key, const char *dflt) {
    PdEntry *e = find((PdDict *)d, key);
    if (!e || e->is_num) return dflt;
    return e->str ? e->str : dflt;
}

void pd_merge(PdDict *dst, const PdDict *src) {
    if (!src) return;
    for (int i = 0; i < src->count; i++) {
        const PdEntry *s = &src->e[i];
        if (s->is_num) pd_set_num(dst, s->key, s->num);
        else pd_set_str(dst, s->key, s->str);
    }
}

void pd_scale_keys(PdDict *d, const char *const *keys, int key_count, double scale) {
    for (int k = 0; k < key_count; k++) {
        PdEntry *e = find(d, keys[k]);
        if (!e || !e->is_num) continue;
        if (!isfinite(e->num)) continue;
        e->num *= scale;
    }
}
