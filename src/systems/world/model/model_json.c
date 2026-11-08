#include "platform/platform.h"
/* 精简 JSON DOM 实现 */
#include "model_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JS_MAX_DEPTH 96

typedef struct {
    const char *p;
    const char *start;
    const char *err;
    int         depth;
} JsCursor;

static JsVal *parse_value(JsCursor *c);

static void skip_ws(JsCursor *c) {
    for (;;) {
        char ch = *c->p;
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') { c->p++; continue; }
        break;
    }
}

static JsVal *new_val(JsType t) {
    JsVal *v = (JsVal *)calloc(1, sizeof(JsVal));
    if (v) v->type = t;
    return v;
}

static int push_item(JsVal *v, JsVal *item, char *key) {
    if (v->count >= v->cap) {
        int next = v->cap ? v->cap * 2 : 8;
        JsVal **ni = (JsVal **)realloc(v->items, (size_t)next * sizeof(JsVal *));
        if (!ni) return 0;
        v->items = ni;
        if (v->type == JS_OBJ) {
            char **nk = (char **)realloc(v->keys, (size_t)next * sizeof(char *));
            if (!nk) return 0;
            v->keys = nk;
        }
        v->cap = next;
    }
    v->items[v->count] = item;
    if (v->type == JS_OBJ) v->keys[v->count] = key;
    v->count++;
    return 1;
}

static char *parse_string_raw(JsCursor *c) {
    if (*c->p != '"') { c->err = "expect string"; return NULL; }
    c->p++;
    size_t cap = 32, len = 0;
    char *out = (char *)malloc(cap);
    if (!out) { c->err = "oom"; return NULL; }
    for (;;) {
        unsigned char ch = (unsigned char)*c->p;
        if (ch == 0) { c->err = "unterminated string"; free(out); return NULL; }
        if (ch == '"') { c->p++; break; }
        if (ch == '\\') {
            c->p++;
            char e = *c->p++;
            unsigned cp = 0;
            switch (e) {
            case '"':  cp = '"';  break;
            case '\\': cp = '\\'; break;
            case '/':  cp = '/';  break;
            case 'b':  cp = '\b'; break;
            case 'f':  cp = '\f'; break;
            case 'n':  cp = '\n'; break;
            case 'r':  cp = '\r'; break;
            case 't':  cp = '\t'; break;
            case 'u': {
                unsigned v = 0;
                for (int i = 0; i < 4; i++) {
                    char h = *c->p++;
                    v <<= 4;
                    if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                    else { c->err = "bad \\u"; free(out); return NULL; }
                }
                cp = v;
                /* 低代理区合并成对 */
                if (cp >= 0xD800 && cp <= 0xDBFF && c->p[0] == '\\' && c->p[1] == 'u') {
                    const char *save = c->p;
                    c->p += 2;
                    unsigned lo = 0;
                    int ok = 1;
                    for (int i = 0; i < 4; i++) {
                        char h = *c->p++;
                        lo <<= 4;
                        if (h >= '0' && h <= '9') lo |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') lo |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') lo |= (unsigned)(h - 'A' + 10);
                        else { ok = 0; break; }
                    }
                    if (ok && lo >= 0xDC00 && lo <= 0xDFFF)
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    else
                        c->p = save;
                }
                break;
            }
            default: c->err = "bad escape"; free(out); return NULL;
            }
            /* 编码为 UTF-8 */
            unsigned char buf[4];
            int n;
            if (cp < 0x80)          { buf[0] = (unsigned char)cp; n = 1; }
            else if (cp < 0x800)    { buf[0] = (unsigned char)(0xC0 | (cp >> 6));
                                      buf[1] = (unsigned char)(0x80 | (cp & 0x3F)); n = 2; }
            else if (cp < 0x10000)  { buf[0] = (unsigned char)(0xE0 | (cp >> 12));
                                      buf[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                                      buf[2] = (unsigned char)(0x80 | (cp & 0x3F)); n = 3; }
            else                    { buf[0] = (unsigned char)(0xF0 | (cp >> 18));
                                      buf[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
                                      buf[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                                      buf[3] = (unsigned char)(0x80 | (cp & 0x3F)); n = 4; }
            if (len + (size_t)n + 1 > cap) {
                cap = (len + (size_t)n + 1) * 2;
                char *no = (char *)realloc(out, cap);
                if (!no) { c->err = "oom"; free(out); return NULL; }
                out = no;
            }
            memcpy(out + len, buf, (size_t)n);
            len += (size_t)n;
            continue;
        }
        if (len + 2 > cap) {
            cap *= 2;
            char *no = (char *)realloc(out, cap);
            if (!no) { c->err = "oom"; free(out); return NULL; }
            out = no;
        }
        out[len++] = (char)ch;
        c->p++;
    }
    out[len] = '\0';
    return out;
}

static JsVal *parse_object(JsCursor *c) {
    JsVal *v = new_val(JS_OBJ);
    if (!v) { c->err = "oom"; return NULL; }
    c->p++; /* { */
    skip_ws(c);
    if (*c->p == '}') { c->p++; return v; }
    for (;;) {
        skip_ws(c);
        char *key = parse_string_raw(c);
        if (!key) { qlcg_js_free(v); return NULL; }
        skip_ws(c);
        if (*c->p != ':') { c->err = "expect ':'"; free(key); qlcg_js_free(v); return NULL; }
        c->p++;
        JsVal *item = parse_value(c);
        if (!item) { free(key); qlcg_js_free(v); return NULL; }
        if (!push_item(v, item, key)) { c->err = "oom"; free(key); qlcg_js_free(item); qlcg_js_free(v); return NULL; }
        skip_ws(c);
        if (*c->p == ',') { c->p++; continue; }
        if (*c->p == '}') { c->p++; return v; }
        c->err = "expect ',' or '}'";
        qlcg_js_free(v);
        return NULL;
    }
}

static JsVal *parse_array(JsCursor *c) {
    JsVal *v = new_val(JS_ARR);
    if (!v) { c->err = "oom"; return NULL; }
    c->p++; /* [ */
    skip_ws(c);
    if (*c->p == ']') { c->p++; return v; }
    for (;;) {
        JsVal *item = parse_value(c);
        if (!item) { qlcg_js_free(v); return NULL; }
        if (!push_item(v, item, NULL)) { c->err = "oom"; qlcg_js_free(item); qlcg_js_free(v); return NULL; }
        skip_ws(c);
        if (*c->p == ',') { c->p++; continue; }
        if (*c->p == ']') { c->p++; return v; }
        c->err = "expect ',' or ']'";
        qlcg_js_free(v);
        return NULL;
    }
}

static JsVal *parse_value(JsCursor *c) {
    if (++c->depth > JS_MAX_DEPTH) { c->err = "too deep"; c->depth--; return NULL; }
    skip_ws(c);
    char ch = *c->p;
    JsVal *out = NULL;
    if (ch == '{') {
        out = parse_object(c);
    } else if (ch == '[') {
        out = parse_array(c);
    } else if (ch == '"') {
        char *s = parse_string_raw(c);
        if (s) {
            out = new_val(JS_STR);
            if (out) out->str = s;
            else { free(s); c->err = "oom"; }
        }
    } else if (ch == 't' && strncmp(c->p, "true", 4) == 0) {
        c->p += 4;
        out = new_val(JS_BOOL);
        if (out) out->bval = 1;
    } else if (ch == 'f' && strncmp(c->p, "false", 5) == 0) {
        c->p += 5;
        out = new_val(JS_BOOL);
        if (out) out->bval = 0;
    } else if (ch == 'n' && strncmp(c->p, "null", 4) == 0) {
        c->p += 4;
        out = new_val(JS_NULL);
    } else {
        char *end = NULL;
        double d = strtod(c->p, &end);
        if (end == c->p) { c->err = "unexpected token"; }
        else {
            c->p = end;
            out = new_val(JS_NUM);
            if (out) out->num = d;
        }
    }
    c->depth--;
    return out;
}

JsVal *qlcg_js_parse(const char *text, const char **err) {
    if (err) *err = NULL;
    if (!text) { if (err) *err = "null text"; return NULL; }
    JsCursor c;
    c.p = text;
    /* 跳过 UTF-8 BOM */
    if ((unsigned char)c.p[0] == 0xEF && (unsigned char)c.p[1] == 0xBB && (unsigned char)c.p[2] == 0xBF) c.p += 3;
    c.start = c.p;
    c.err = NULL;
    c.depth = 0;
    JsVal *v = parse_value(&c);
    if (!v) { if (err) *err = c.err ? c.err : "parse failed"; return NULL; }
    skip_ws(&c);
    if (*c.p != '\0') {
        if (err) *err = "trailing data";
        qlcg_js_free(v);
        return NULL;
    }
    return v;
}

JsVal *qlcg_js_load(const char *path, const char **err) {
    if (err) *err = NULL;
    FILE *f = qlcg_platform_fopen(path, "rb");
    if (!f) { if (err) *err = "open failed"; return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); if (err) *err = "seek failed"; return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); if (err) *err = "tell failed"; return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); if (err) *err = "oom"; return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    JsVal *v = qlcg_js_parse(buf, err);
    free(buf);
    return v;
}

void qlcg_js_free(JsVal *v) {
    if (!v) return;
    for (int i = 0; i < v->count; i++) {
        if (v->keys && v->keys[i]) free(v->keys[i]);
        qlcg_js_free(v->items[i]);
    }
    free(v->items);
    free(v->keys);
    free(v->str);
    free(v);
}

const JsVal *qlcg_js_get(const JsVal *obj, const char *key) {
    if (!obj || obj->type != JS_OBJ || !key) return NULL;
    for (int i = 0; i < obj->count; i++) {
        if (obj->keys[i] && strcmp(obj->keys[i], key) == 0) return obj->items[i];
    }
    return NULL;
}

int qlcg_js_has(const JsVal *obj, const char *key) {
    return qlcg_js_get(obj, key) != NULL;
}

const JsVal *qlcg_js_at(const JsVal *v, int i) {
    if (!v || i < 0 || i >= v->count) return NULL;
    return v->items[i];
}

const char *qlcg_js_key(const JsVal *v, int i) {
    if (!v || v->type != JS_OBJ || i < 0 || i >= v->count) return NULL;
    return v->keys[i];
}

int qlcg_js_count(const JsVal *v) {
    return v ? v->count : 0;
}

double qlcg_js_num(const JsVal *v, double dflt) {
    if (!v) return dflt;
    if (v->type == JS_NUM) return v->num;
    if (v->type == JS_BOOL) return v->bval ? 1.0 : 0.0;
    return dflt;
}

const char *qlcg_js_str(const JsVal *v, const char *dflt) {
    if (!v || v->type != JS_STR) return dflt;
    return v->str;
}

int qlcg_js_bool(const JsVal *v, int dflt) {
    if (!v) return dflt;
    if (v->type == JS_BOOL) return v->bval;
    if (v->type == JS_NUM) return v->num != 0.0;
    return dflt;
}
