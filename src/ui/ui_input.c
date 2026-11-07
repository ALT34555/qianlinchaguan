/* 单行编辑内核：光标、选区、剪贴板、UTF-8 */
#include "ui/ui_input.h"
#include "ui/ui_core.h"
#include "ui/ui_widgets.h"
#include "core/math_util.h"
#include <stdlib.h>
#include <string.h>

/* 粘贴与复制的单次字节上限 */
#define UI_EDIT_TEXT_MAX 6144u

/* ===== UTF-8 ===== */


int ui_u8_step(const char *p) {
    unsigned char c = (unsigned char)*p;
    if ((c & 0xE0u) == 0xC0u) return 2;
    if ((c & 0xF0u) == 0xE0u) return 3;
    if ((c & 0xF8u) == 0xF0u) return 4;
    return 1;
}

size_t ui_u8_len(const char *s) { return (s && s[0]) ? strlen(s) : 0; }

uint32_t ui_u8_next(const char *s, uint32_t i) {
    size_t n = ui_u8_len(s);
    if ((size_t)i >= n) return (uint32_t)n;
    i++;
    while ((size_t)i < n && ((unsigned char)s[i] & 0xC0u) == 0x80u) i++;
    return i;
}

uint32_t ui_u8_prev(const char *s, uint32_t i) {
    if (!s || i == 0) return 0;
    i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0u) == 0x80u) i--;
    return i;
}

uint32_t ui_u8_count(const char *s) {
    uint32_t n = 0;
    for (uint32_t i = 0; s && s[i]; i = ui_u8_next(s, i)) n++;
    return n;
}

uint32_t ui_u8_floor(const char *s, uint32_t i) {
    size_t n = ui_u8_len(s);
    if ((size_t)i > n) i = (uint32_t)n;
    while (i > 0 && ((unsigned char)s[i] & 0xC0u) == 0x80u) i--;
    return i;
}

void ui_u8_copy(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = ui_u8_floor(src, (uint32_t)(cap - 1));
    memmove(dst, src, n);
    dst[n] = '\0';
}

/* 末尾若是被截断的多字节序列则削掉 */
void ui_u8_trim(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    size_t i = 0;
    while (i < n) {
        int step = ui_u8_step(s + i);
        if (i + (size_t)step > n) { s[i] = '\0'; return; }
        i += (size_t)step;
    }
}

/* ===== 选区与替换 ===== */

static uint32_t caret_of(const UiEditField *f) {
    uint32_t len = (uint32_t)ui_u8_len(f->buf);
    uint32_t c = f->ed->caret;
    if (c > len) c = len;
    return ui_u8_floor(f->buf, c);
}

static void sel_of(const UiEditField *f, uint32_t *a, uint32_t *b) {
    uint32_t c = caret_of(f);
    uint32_t n = ui_u8_floor(f->buf, f->ed->anchor);
    if (c < n) { *a = c; *b = n; } else { *a = n; *b = c; }
}

bool ui_edit_has_sel(const UiEditField *f) {
    if (!f || !f->buf || !f->ed) return false;
    uint32_t a, b;
    sel_of(f, &a, &b);
    return b > a;
}

void ui_edit_sel(const UiEditField *f, uint32_t *a, uint32_t *b) {
    if (!f || !f->buf || !f->ed) { if (a) *a = 0; if (b) *b = 0; return; }
    sel_of(f, a, b);
}

void ui_edit_reset(UiEditField *f) {
    if (!f || !f->ed) return;
    f->ed->caret    = 0;
    f->ed->anchor   = 0;
    f->ed->scroll   = 0.0f;
    f->ed->hold     = 0.0f;
    f->ed->dragging = false;
}

void ui_edit_touch(UiEditField *f) {
    if (f && f->ed) f->ed->hold = 0.45f;
}

static bool erase_range(UiEditField *f, uint32_t a, uint32_t b) {
    if (!f || !f->buf || !f->ed || b <= a) return false;
    size_t len = strlen(f->buf);
    if ((size_t)b > len) b = (uint32_t)len;
    memmove(f->buf + a, f->buf + b, len - (size_t)b + 1);
    f->ed->caret  = a;
    f->ed->anchor = a;
    ui_edit_touch(f);
    return true;
}

/* 区间内码点数 */
static uint32_t count_range(const char *buf, uint32_t from, uint32_t to) {
    uint32_t n = 0;
    for (uint32_t i = from; i < to && buf[i]; i = ui_u8_next(buf, i)) n++;
    return n;
}

/* 插入 UTF-8：滤控制符，截到字节/码点上限 */
static bool insert_text(UiEditField *f, const char *text) {
    if (!f || !f->buf || !f->ed || !text || !text[0]) return false;
    size_t tn = strlen(text);

    char add[UI_EDIT_TEXT_MAX];
    size_t an = 0;
    for (size_t i = 0; i < tn && an + 4 < sizeof(add); ) {
        int step = ui_u8_step(text + i);
        if (i + (size_t)step > tn) break;              /* 残缺尾序列 */
        unsigned char c = (unsigned char)text[i];
        bool ctrl = (step == 1) ? (c < 0x20u || c == 0x7Fu)
                                : (c == 0xC2u && (unsigned char)text[i + 1] < 0xA0u);
        if (!ctrl) {
            memcpy(add + an, text + i, (size_t)step);
            an += (size_t)step;
        }
        i += (size_t)step;
    }
    add[an] = '\0';
    if (an == 0) return false;

    size_t len = strlen(f->buf);
    uint32_t a, b;
    if (ui_edit_has_sel(f)) sel_of(f, &a, &b); else { a = b = caret_of(f); }

    /* 字节余量 */
    size_t room = (f->cap > 0) ? f->cap - 1 : 0;
    size_t used_after_del = len - (size_t)(b - a);
    room = (room > used_after_del) ? room - used_after_del : 0;
    size_t keep = an;
    if (keep > room) keep = (size_t)ui_u8_floor(add, (uint32_t)room);

    /* 码点余量 */
    if (f->max_chars > 0) {
        uint32_t used = ui_u8_count(f->buf) - count_range(f->buf, a, b);
        uint32_t left = (used < f->max_chars) ? f->max_chars - used : 0;
        uint32_t cnt = 0;
        size_t cut = 0;
        while (add[cut] && cnt < left) { cut = (size_t)ui_u8_next(add, (uint32_t)cut); cnt++; }
        if (cut < keep) keep = cut;
    }
    if (keep == 0) return false;

    memmove(f->buf + a + keep, f->buf + b, len - (size_t)b + 1);
    memcpy(f->buf + a, add, keep);
    f->ed->caret  = a + (uint32_t)keep;
    f->ed->anchor = f->ed->caret;
    ui_edit_touch(f);
    return true;
}

bool ui_edit_insert_cp(UiEditField *f, uint32_t cp) {
    char enc[5];
    int n;
    if (cp < 0x80u) {
        enc[0] = (char)cp; n = 1;
    } else if (cp < 0x800u) {
        enc[0] = (char)(0xC0u | (cp >> 6));
        enc[1] = (char)(0x80u | (cp & 0x3Fu)); n = 2;
    } else if (cp < 0x10000u) {
        enc[0] = (char)(0xE0u | (cp >> 12));
        enc[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        enc[2] = (char)(0x80u | (cp & 0x3Fu)); n = 3;
    } else {
        enc[0] = (char)(0xF0u | (cp >> 18));
        enc[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        enc[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        enc[3] = (char)(0x80u | (cp & 0x3Fu)); n = 4;
    }
    enc[n] = '\0';
    return insert_text(f, enc);
}

bool ui_edit_insert_utf8(UiEditField *f, const char *text) { return insert_text(f, text); }

bool ui_edit_backspace(UiEditField *f) {
    if (!f || !f->buf || !f->ed) return false;
    uint32_t a, b;
    if (ui_edit_has_sel(f)) { sel_of(f, &a, &b); return erase_range(f, a, b); }
    uint32_t c = caret_of(f);
    if (c == 0) return false;
    return erase_range(f, ui_u8_prev(f->buf, c), c);
}

bool ui_edit_delete_forward(UiEditField *f) {
    if (!f || !f->buf || !f->ed) return false;
    uint32_t a, b;
    if (ui_edit_has_sel(f)) { sel_of(f, &a, &b); return erase_range(f, a, b); }
    uint32_t c = caret_of(f);
    uint32_t n = ui_u8_next(f->buf, c);
    if (n == c) return false;
    return erase_range(f, c, n);
}

static bool word_byte(unsigned char c) {
    return !(c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
             c == ',' || c == '.' || c == ';' || c == ':' || c == '!' || c == '?' ||
             c == '"' || c == '\'' || c == '(' || c == ')' || c == '[' || c == ']');
}

static uint32_t word_step(const char *buf, uint32_t i, int dir) {
    uint32_t n = (uint32_t)ui_u8_len(buf);
    if (dir < 0) {
        while (i > 0) { uint32_t p = ui_u8_prev(buf, i); if (!word_byte((unsigned char)buf[p])) i = p; else break; }
        while (i > 0) { uint32_t p = ui_u8_prev(buf, i); if (word_byte((unsigned char)buf[p])) i = p; else break; }
        return i;
    }
    while (i < n) { if (!word_byte((unsigned char)buf[i])) i = ui_u8_next(buf, i); else break; }
    while (i < n) { if (word_byte((unsigned char)buf[i])) i = ui_u8_next(buf, i); else break; }
    return i;
}

bool ui_edit_move(UiEditField *f, int dir, bool extend, bool by_word) {
    if (!f || !f->buf || !f->ed) return false;
    uint32_t c = caret_of(f);
    if (!extend && ui_edit_has_sel(f)) {
        uint32_t a, b;
        sel_of(f, &a, &b);
        c = (dir < 0) ? a : b;
    } else if (by_word) {
        c = word_step(f->buf, c, dir);
    } else {
        c = (dir < 0) ? ui_u8_prev(f->buf, c) : ui_u8_next(f->buf, c);
    }
    f->ed->caret = c;
    if (!extend) f->ed->anchor = c;
    ui_edit_touch(f);
    return false;
}

bool ui_edit_home(UiEditField *f, bool extend) {
    if (!f || !f->ed) return false;
    f->ed->caret = 0;
    if (!extend) f->ed->anchor = 0;
    ui_edit_touch(f);
    return false;
}

bool ui_edit_end(UiEditField *f, bool extend) {
    if (!f || !f->buf || !f->ed) return false;
    f->ed->caret = (uint32_t)ui_u8_len(f->buf);
    if (!extend) f->ed->anchor = f->ed->caret;
    ui_edit_touch(f);
    return false;
}

bool ui_edit_select_all(UiEditField *f) {
    if (!f || !f->buf || !f->ed) return false;
    f->ed->anchor = 0;
    f->ed->caret  = (uint32_t)ui_u8_len(f->buf);
    ui_edit_touch(f);
    return false;
}

bool ui_edit_copy(const UiEditField *f) {
    if (!ui_edit_has_sel(f)) return false;
    uint32_t a, b;
    sel_of(f, &a, &b);
    char tmp[UI_EDIT_TEXT_MAX];
    size_t n = (size_t)(b - a);
    if (n >= sizeof(tmp)) n = (size_t)ui_u8_floor(f->buf, (uint32_t)(sizeof(tmp) - 1));
    memcpy(tmp, f->buf + a, n);
    tmp[n] = '\0';
    return ui_clip_set_utf8(tmp);
}

bool ui_edit_cut(UiEditField *f) {
    if (!ui_edit_has_sel(f)) return false;
    if (!ui_edit_copy(f)) return false;
    uint32_t a, b;
    sel_of(f, &a, &b);
    return erase_range(f, a, b);
}

bool ui_edit_paste(UiEditField *f) {
    char tmp[UI_EDIT_TEXT_MAX];
    if (!ui_clip_get_utf8(tmp, sizeof(tmp))) return false;
    return insert_text(f, tmp);
}

/* ===== 单行度量 ===== */

#define UI_METRIC_MAX 1536

typedef struct {
    bool     valid;
    uint32_t hash;
    int      face;
    int      px;
    bool     bold;
    int      count;
    uint32_t idx[UI_METRIC_MAX];   /* 各码点起始字节 */
    float    x[UI_METRIC_MAX];     /* 该码点之前的累计宽度 */
    float    total;
} UiMetrics;

static UiMetrics s_met;

void ui_text_metrics(UiContext *ui, int face, float px, bool bold, const char *buf) {
    int ipx = (int)(px + 0.5f);
    uint32_t h = qlcg_hash_str(buf ? buf : "");
    h ^= (uint32_t)face * 0x9E3779B9u ^ (uint32_t)ipx * 0x85EBCA6Bu
       ^ (bold ? 0xC2B2AE35u : 0u);
    if (s_met.valid && s_met.hash == h && s_met.face == face &&
        s_met.px == ipx && s_met.bold == bold) return;

    s_met.valid = true;
    s_met.hash  = h;
    s_met.face  = face;
    s_met.px    = ipx;
    s_met.bold  = bold;
    s_met.count = 0;
    s_met.total = 0.0f;
    if (!buf || !buf[0]) return;

    uint32_t bi = 0;
    for (const char *p = buf; *p && s_met.count < UI_METRIC_MAX; ) {
        int step = ui_u8_step(p);
        char g[5] = {0};
        memcpy(g, p, (size_t)step);
        s_met.idx[s_met.count] = bi;
        s_met.x[s_met.count]   = s_met.total;
        /* 单字槽宽含 4px 图集边距，扣掉才是真实步进 */
        s_met.total += qlcg_maxf(1.0f,
            (float)ui_text_width_px(ui, (UiFaceKind)face, px, bold, g) - 4.0f);
        s_met.count++;
        bi += (uint32_t)step;
        p  += step;
    }
}

float ui_text_metrics_x(uint32_t byte_idx) {
    if (s_met.count <= 0) return 0.0f;
    for (int i = s_met.count - 1; i >= 0; i--) {
        if (s_met.idx[i] <= byte_idx) {
            /* 末尾边界取总宽，否则少一个字 */
            if (i == s_met.count - 1 && byte_idx > s_met.idx[i]) return s_met.total;
            return s_met.x[i];
        }
    }
    return 0.0f;
}

float ui_text_metrics_total(void) { return s_met.total; }

/* 最近的码点数组下标（四舍五入到字符中线） */
static int metric_index_at_x(float rel_x) {
    if (s_met.count <= 0) return 0;
    int lo = 0, hi = s_met.count - 1, hit = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (s_met.x[mid] <= rel_x) { hit = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    float x0 = s_met.x[hit];
    float x1 = (hit + 1 < s_met.count) ? s_met.x[hit + 1] : s_met.total;
    if (rel_x - x0 > (x1 - x0) * 0.5f) hit++;
    return hit > s_met.count ? s_met.count : hit;
}

/* 起点不大于 rel_x 的码点下标，用于取绘制窗口 */
static int metric_index_before_x(float rel_x) {
    int i = metric_index_at_x(rel_x);
    while (i > 0 && s_met.x[i] > rel_x) i--;
    return i;
}

uint32_t ui_text_metrics_at_x(float rel_x) {
    if (s_met.count <= 0) return 0;
    int i = metric_index_before_x(rel_x);
    if (i >= s_met.count) return s_met.idx[s_met.count - 1];
    return s_met.idx[i];
}

uint32_t ui_edit_hit_line(UiContext *ui, const UiEditField *f, float tw,
                          int face, float px, bool bold, float rel_x) {
    (void)tw;
    if (!f || !f->buf) return 0;
    ui_text_metrics(ui, face, px, bold, f->buf);
    if (s_met.count <= 0) return 0;
    float rx = rel_x + (f->ed ? f->ed->scroll : 0.0f);
    int i = metric_index_at_x(rx);
    if (i >= s_met.count) return (uint32_t)ui_u8_len(f->buf);
    return s_met.idx[i];
}

bool ui_edit_draw_line(UiContext *ui, UiEditField *f, float x, float y,
                       float tw, float h, int face, float px, bool bold,
                       bool focused, bool active,
                       uint32_t col_text, uint32_t col_ph,
                       uint32_t col_sel, uint32_t col_caret,
                       const char *placeholder) {
    if (!f || !f->buf) return false;
    float z  = ui->met.size;
    float ty = y + (h - px * 1.30f) * 0.5f;
    float caret_w = qlcg_maxf(1.0f, 1.5f * z);
    float caret_h = px * 1.18f;
    if (f->ed) f->ed->hold = qlcg_maxf(0.0f, f->ed->hold - ui->dt);
    bool blink_on = (f->ed && f->ed->hold > 0.0f) || (((int)(ui->time * 2.0f)) & 1) == 0;

    if (!f->buf[0]) {
        if (placeholder && placeholder[0]) {
            ui_draw_text_ellipsis(ui, (UiFaceKind)face, px, bold, x, ty, tw, 0, placeholder, col_ph);
        }
        if (focused && f->ed) {
            if (blink_on) ui_draw_rect(ui, x, ty - 1.0f, caret_w, caret_h, col_caret);
            if (active) {
                ui->ime_caret_x = x;
                ui->ime_caret_y = ty;
                ui->ime_caret_h = caret_h;
                ui->ime_caret_valid = true;
            }
        }
        return false;
    }

    ui_text_metrics(ui, face, px, bold, f->buf);
    uint32_t len   = (uint32_t)ui_u8_len(f->buf);
    uint32_t caret = f->ed ? f->ed->caret : len;
    if (caret > len) caret = len;
    caret = ui_u8_floor(f->buf, caret);
    float cx    = ui_text_metrics_x(caret);
    float total = ui_text_metrics_total();

    /* 水平滚动：把光标框在可视区内 */
    float scroll = f->ed ? f->ed->scroll : 0.0f;
    if (!focused) {
        scroll = 0.0f;
    } else {
        float margin = 2.0f * z;
        if (cx - scroll + caret_w + margin > tw) scroll = cx - tw + caret_w + margin;
        if (cx - scroll < 0.0f) scroll = cx;
        float max_scroll = qlcg_maxf(0.0f, total - tw + caret_w + margin);
        if (scroll < 0.0f) scroll = 0.0f;
        if (scroll > max_scroll) scroll = max_scroll;
        f->ed->scroll = scroll;
    }

    /* 选区底色 */
    if (focused && f->ed && ui_edit_has_sel(f)) {
        uint32_t a, b;
        sel_of(f, &a, &b);
        float xa = x + (ui_text_metrics_x(a) - scroll);
        float xb = x + (ui_text_metrics_x(b) - scroll);
        float c0 = qlcg_maxf(xa, x);
        float c1 = qlcg_minf(xb, x + tw);
        if (c1 > c0) {
            ui_clip_push(ui, x, y, tw, h);
            ui_draw_rect(ui, c0, ty - 1.0f, c1 - c0, caret_h, col_sel);
            ui_clip_pop(ui);
        }
    }

    /* 文本：逐字形绘制，与度量严格对齐 */
    int start = metric_index_before_x(scroll);
    if (start >= s_met.count) start = s_met.count - 1;
    uint32_t bi = s_met.idx[start];
    ui_clip_push(ui, x, y, tw, h);
    for (int i = start; i < s_met.count; i++) {
        float gx = x + (s_met.x[i] - scroll);
        if (gx >= x + tw) break;
        const char *p = f->buf + bi;
        int step = ui_u8_step(p);
        char g[5] = {0};
        memcpy(g, p, (size_t)step);
        ui_draw_text_px(ui, (UiFaceKind)face, px, bold, gx, ty, g, col_text);
        bi += (uint32_t)step;
    }
    ui_clip_pop(ui);

    /* 光标竖线 */
    if (focused && blink_on) {
        float gx = x + (cx - scroll);
        if (gx >= x - caret_w && gx <= x + tw) {
            ui_clip_push(ui, x, y, tw, h);
            ui_draw_rect(ui, gx, ty - 1.0f, caret_w, caret_h, col_caret);
            ui_clip_pop(ui);
        }
    }
    if (focused && active) {
        ui->ime_caret_x = x + (cx - scroll);
        ui->ime_caret_y = ty;
        ui->ime_caret_h = caret_h;
        ui->ime_caret_valid = true;
    }
    return true;
}
