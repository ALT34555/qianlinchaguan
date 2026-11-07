/* 仿 CSS 流式/弹性盒布局器实现 */
#include "ui_layout.h"
#include "ui_widgets.h"
#include "../core/math_util.h"
#include "../core/i18n.h"
#include <string.h>
#include <stdio.h>

#define LAYOUT_LABEL_RATIO 0.40f

void ui_layout_begin(UiLayout *L, UiContext *ui, float x, float y, float w) {
    memset(L, 0, sizeof(*L));
    L->ui          = ui;
    L->x           = x;
    L->y           = y;
    L->w           = w;
    L->cursor      = y;
    L->indent      = 0.0f;
    L->label_ratio = LAYOUT_LABEL_RATIO;
    L->row_gap     = 7.0f * ui->met.size;
}

float ui_layout_end(UiLayout *L) {
    return L->cursor - L->y;
}

void ui_layout_gap(UiLayout *L, float h) {
    if (h > 0.0f) L->cursor += h;
}

void ui_layout_indent_push(UiLayout *L) {
    L->indent += 16.0f * L->ui->met.size;
}

void ui_layout_indent_pop(UiLayout *L) {
    L->indent -= 16.0f * L->ui->met.size;
    if (L->indent < 0.0f) L->indent = 0.0f;
}

void ui_layout_divider(UiLayout *L) {
    UiContext *ui = L->ui;
    float z = ui->met.size;
    L->cursor += 6.0f * z;
    ui_draw_rect(ui, L->x, L->cursor, L->w, 1.0f, tk_of(ui, QL_LINE_FAINT));
    L->cursor += 6.0f * z + L->row_gap * 0.5f;
}

/* 顶部提示横幅：随折行行数自动撑高 */
float ui_layout_banner(UiLayout *L, const char *text) {
    if (!text || !text[0]) return 0.0f;
    UiContext *ui = L->ui;
    float z   = ui->met.size;
    float sc  = ui->met.scale;
    float px  = 11.5f * sc;
    float lh  = ui_line_height(UI_FACE_BODY, px);
    float pad_x = 12.0f * z;
    float pad_y = 6.0f * z;
    float x   = L->x + L->indent;
    float w   = L->w - L->indent;
    float tw  = qlcg_maxf(20.0f, w - pad_x * 2.0f);
    int lines = ui_measure_text_wrapped(ui, UI_FACE_BODY, px, false, tw, text);
    if (lines < 1) lines = 1;
    float h = (float)lines * lh + pad_y * 2.0f;

    ui_draw_rect(ui, x, L->cursor, w, h, tk_of(ui, QL_PRIMARY_SOFT));
    ui_draw_rect(ui, x, L->cursor, 3.0f * z, h, tk_of(ui, QL_PRIMARY_UI));
    ui_draw_text_wrapped(ui, UI_FACE_BODY, px, false, x + pad_x, L->cursor + pad_y,
                         tw, lh, text, tk_of(ui, QL_PRIMARY_TEXT));
    L->cursor += h + L->row_gap + 4.0f * z;
    return h;
}

/* 开启自适应卡片容器 */
void ui_card_begin(UiLayout *L, UiCard *c, float pad_x, float pad_y, float min_h) {
    memset(c, 0, sizeof(*c));
    c->x            = L->x + L->indent;
    c->y            = L->cursor;
    c->w            = qlcg_maxf(10.0f, L->w - L->indent);
    c->pad_x        = pad_x;
    c->pad_y        = pad_y;
    c->min_h        = min_h;
    c->saved_x      = L->x;
    c->saved_w      = L->w;
    c->saved_indent = L->indent;
    c->v_mark       = L->ui->vert_count;

    L->x      = c->x + pad_x;
    L->w      = qlcg_maxf(4.0f, c->w - pad_x * 2.0f);
    L->indent = 0.0f;
    L->cursor = c->y + pad_y;
}

/* 结束自适应卡片容器并将背景插入子元素顶点之前 */
void ui_card_end(UiLayout *L, UiCard *c, float blur, uint32_t bg, uint32_t border, uint32_t shadow,
                 float *out_x, float *out_y, float *out_w, float *out_h) {
    UiContext *ui = L->ui;
    float inner_bottom = L->cursor;
    if (inner_bottom > c->y + c->pad_y) {
        inner_bottom -= L->row_gap;
        if (inner_bottom < c->y + c->pad_y) inner_bottom = c->y + c->pad_y;
    }
    float h = qlcg_maxf(c->min_h, (inner_bottom - c->y) + c->pad_y);

    int v_mid = ui->vert_count;
    ui_panel(ui, c->x, c->y, c->w, h, blur, bg, border, shadow);
    int v_end = ui->vert_count;

    int child_n = v_mid - c->v_mark;
    int panel_n = v_end - v_mid;
    if (c->v_mark >= 0 && child_n > 0 && panel_n > 0 && panel_n <= 512 && v_end <= UI_MAX_VERTS) {
        UiVertex tmp[512];
        memcpy(tmp, &ui->verts[v_mid], (size_t)panel_n * sizeof(UiVertex));
        memmove(&ui->verts[c->v_mark + panel_n], &ui->verts[c->v_mark],
                (size_t)child_n * sizeof(UiVertex));
        memcpy(&ui->verts[c->v_mark], tmp, (size_t)panel_n * sizeof(UiVertex));
    }

    L->x      = c->saved_x;
    L->w      = c->saved_w;
    L->indent = c->saved_indent;
    L->cursor = c->y + h + L->row_gap;

    if (out_x) *out_x = c->x;
    if (out_y) *out_y = c->y;
    if (out_w) *out_w = c->w;
    if (out_h) *out_h = h;
}

UiRow ui_row(UiLayout *L, float want_h) {
    UiContext *ui = L->ui;
    float z   = ui->met.size;
    float pad = 5.0f * z;

    UiRow r;
    memset(&r, 0, sizeof(r));
    r.text_px = 11.5f * ui->met.scale;
    float text_h = ui_line_height(UI_FACE_BODY, r.text_px);
    float ctl_h  = ui_ctl_height(want_h, r.text_px);
    r.h = qlcg_maxf(ctl_h, text_h + pad * 2.0f);

    r.x = L->x + L->indent;
    r.w = L->w - L->indent;

    float lw = r.w * L->label_ratio;
    lw = qlcg_maxf(lw, 90.0f * z);
    lw = qlcg_minf(lw, r.w * 0.55f);
    r.label_x = r.x;
    r.label_w = lw;
    r.ctl_x   = r.x + lw + 8.0f * z;
    r.ctl_w   = r.x + r.w - r.ctl_x;
    if (r.ctl_w < 0.0f) r.ctl_w = 0.0f;

    r.y       = L->cursor;
    r.inner_h = r.h - pad * 2.0f;
    L->cursor += r.h + L->row_gap;
    return r;
}

/* 自适应表单行：文字与最小控件宽冲突时自动换行 */
UiRow ui_row_auto(UiLayout *L, const char *label, bool dim, float min_ctl_w, float want_ctl_h) {
    UiContext *ui = L->ui;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float px = 11.5f * sc;
    float lh = ui_line_height(UI_FACE_BODY, px);
    float ctl_h = ui_ctl_height(want_ctl_h, 12.0f * sc);

    UiRow r;
    memset(&r, 0, sizeof(r));
    r.text_px = px;
    r.x = L->x + L->indent;
    r.w = qlcg_maxf(10.0f, L->w - L->indent);

    float tw = (label && label[0]) ? (float)ui_text_width_px(ui, UI_FACE_BODY, px, false, label) : 0.0f;
    float col_lw = qlcg_clampf(r.w * L->label_ratio, 86.0f * z, r.w * 0.52f);
    float eff_lw = qlcg_maxf(col_lw, tw + 4.0f * z);

    if (tw > 0.0f && (eff_lw + 10.0f * z + min_ctl_w > r.w)) {
        /* 放不下一行：标签独占上行，控件占下行 */
        int lines = ui_draw_text_wrapped(ui, UI_FACE_BODY, px, false, r.x, L->cursor,
                                         r.w, lh, label, dim ? tk_of(ui, QL_INK_2) : tk_of(ui, QL_INK));
        if (lines < 1) lines = 1;
        L->cursor += (float)lines * lh + 4.0f * z;
        r.y       = L->cursor;
        r.h       = ctl_h;
        r.inner_h = ctl_h;
        r.label_x = r.x;
        r.label_w = 0.0f;
        r.ctl_x   = r.x;
        r.ctl_w   = r.w;
        L->cursor += r.h + L->row_gap;
        return r;
    }

    r.h       = qlcg_maxf(ctl_h, lh + 8.0f * z);
    r.inner_h = ctl_h;
    r.y       = L->cursor;
    r.label_x = r.x;
    r.label_w = eff_lw;
    r.ctl_x   = r.x + eff_lw + 8.0f * z;
    r.ctl_w   = qlcg_maxf(0.0f, r.x + r.w - r.ctl_x);
    if (label && label[0]) {
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, px, false, r.label_x,
                              r.y + (r.h - lh) * 0.5f, r.label_w, 0, label,
                              dim ? tk_of(ui, QL_INK_2) : tk_of(ui, QL_INK));
    }
    L->cursor += r.h + L->row_gap;
    return r;
}

void ui_row_label(const UiLayout *L, const UiRow *r, const char *text, bool dim) {
    if (!text || !text[0]) return;
    UiContext *ui = L->ui;
    float h = ui_line_height(UI_FACE_BODY, r->text_px);
    ui_draw_text_ellipsis(ui, UI_FACE_BODY, r->text_px, false,
                          r->label_x, r->y + (r->h - h) * 0.5f, r->label_w, 0, text,
                          dim ? tk_of(ui, QL_INK_2) : tk_of(ui, QL_INK));
}

float ui_layout_note(UiLayout *L, const char *text, bool dim) {
    if (!text || !text[0]) return 0.0f;
    UiContext *ui = L->ui;
    float px = 10.5f * ui->met.scale;
    float lh = ui_line_height(UI_FACE_BODY, px);
    float x  = L->x + L->indent;
    float w  = L->w - L->indent;
    int n = ui_draw_text_wrapped(ui, UI_FACE_BODY, px, false, x, L->cursor, w, lh,
                                 text, dim ? tk_of(ui, QL_INK_3) : tk_of(ui, QL_INK_2));
    float used = (float)n * lh;
    if (used <= 0.0f) return 0.0f;
    L->cursor += used + L->row_gap;
    return used;
}

/* 仿 CSS flex-wrap 分段按钮组 */
int ui_flex_segments(UiContext *ui, float x, float *io_y, float w, float base_h, float gap,
                     const char *const *labels, int count, int active_idx, float font_px) {
    if (count <= 0 || !labels) return -1;
    float z = ui->met.size;
    float row_h = ui_ctl_height(base_h, font_px);
    float min_w[24];
    int n = count > 24 ? 24 : count;
    for (int i = 0; i < n; i++) {
        float tw = (float)ui_text_width_px(ui, UI_FACE_BODY, font_px, false, labels[i]);
        min_w[i] = tw + 18.0f * z;
    }

    int clicked_idx = -1;
    float cur_y = *io_y;
    int i = 0;
    while (i < n) {
        int j = i;
        float sum_min = 0.0f;
        float max_min = 0.0f;
        while (j < n) {
            float next_sum = sum_min + min_w[j] + (j > i ? gap : 0.0f);
            if (j > i && next_sum > w) break;
            sum_min += min_w[j];
            if (min_w[j] > max_min) max_min = min_w[j];
            j++;
        }
        int k = j - i;
        float avail = qlcg_maxf(10.0f, w - (float)(k - 1) * gap);
        float eq_w  = avail / (float)k;
        bool  use_eq = (max_min <= eq_w + 0.5f);
        float extra  = qlcg_maxf(0.0f, avail - sum_min) / (float)k;

        float cur_x = x;
        for (int m = i; m < j; m++) {
            float bw = use_eq ? eq_w : qlcg_minf(w, min_w[m] + extra);
            if (m == j - 1) bw = qlcg_maxf(12.0f, x + w - cur_x);
            if (ui_btn(ui, cur_x, cur_y, bw, row_h, labels[m],
                       (m == active_idx) ? BTN_SEGMENT_ON : BTN_SEGMENT,
                       false, font_px, false)) {
                clicked_idx = m;
            }
            cur_x += bw + gap;
        }
        cur_y += row_h + gap;
        i = j;
    }
    *io_y = cur_y - gap;
    return clicked_idx;
}

/* 仿 CSS flex-wrap 胶囊按钮组 */
int ui_flex_pills(UiContext *ui, float x, float *io_y, float w, float base_h, float gap,
                  const char *const *labels, int count, int active_idx, bool grow) {
    if (count <= 0 || !labels) return -1;
    float z = ui->met.size;
    float px = 12.0f * ui->met.scale;
    float row_h = ui_ctl_height(base_h, px);
    float min_w[24];
    int n = count > 24 ? 24 : count;
    for (int i = 0; i < n; i++) {
        float tw = (float)ui_text_width_px(ui, UI_FACE_BODY, px, false, labels[i]);
        min_w[i] = tw + 18.0f * z;
    }

    int clicked_idx = -1;
    float cur_y = *io_y;
    int i = 0;
    while (i < n) {
        int j = i;
        float sum_min = 0.0f;
        float max_min = 0.0f;
        while (j < n) {
            float next_sum = sum_min + min_w[j] + (j > i ? gap : 0.0f);
            if (j > i && next_sum > w) break;
            sum_min += min_w[j];
            if (min_w[j] > max_min) max_min = min_w[j];
            j++;
        }
        int k = j - i;
        float avail = qlcg_maxf(10.0f, w - (float)(k - 1) * gap);
        float eq_w  = avail / (float)k;
        bool  use_eq = (max_min <= eq_w + 0.5f);
        float extra  = qlcg_maxf(0.0f, avail - sum_min) / (float)k;

        float cur_x = x;
        for (int m = i; m < j; m++) {
            float bw = !grow ? qlcg_minf(w, min_w[m])
                             : (use_eq ? eq_w : qlcg_minf(w, min_w[m] + extra));
            if (grow && m == j - 1) bw = qlcg_maxf(12.0f, x + w - cur_x);
            if (ui_pill(ui, cur_x, cur_y, bw, row_h, labels[m], m == active_idx)) {
                clicked_idx = m;
            }
            cur_x += bw + gap;
        }
        cur_y += row_h + gap;
        i = j;
    }
    *io_y = cur_y - gap;
    return clicked_idx;
}

int ui_layout_segments(UiLayout *L, const char *const *labels, int count, int active_idx, float font_px) {
    UiContext *ui = L->ui;
    float z = ui->met.size;
    float y = L->cursor;
    int picked = ui_flex_segments(ui, L->x + L->indent, &y, L->w - L->indent,
                                  28.0f * z, 6.0f * z, labels, count, active_idx, font_px);
    L->cursor = y + L->row_gap;
    return picked;
}

int ui_layout_labeled_segments(UiLayout *L, const char *label, const char *const *items,
                               int count, int active_idx) {
    UiContext *ui = L->ui;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float px = 11.5f * sc;
    float sum_w = 0.0f;
    for (int i = 0; i < count; i++) {
        sum_w += (float)ui_text_width_px(ui, UI_FACE_BODY, 11.0f * sc, false, items[i]) + 20.0f * z;
    }
    sum_w += (float)(count > 1 ? count - 1 : 0) * 6.0f * z;

    UiRow r = ui_row_auto(L, label, false, sum_w, 26.0f * z);
    float y = r.y;
    int picked = ui_flex_segments(ui, r.ctl_x, &y, r.ctl_w, 26.0f * z, 6.0f * z,
                                  items, count, active_idx, 11.0f * sc);
    if (y + L->row_gap > L->cursor) {
        L->cursor = y + L->row_gap;
    }
    (void)px;
    return picked;
}

bool ui_layout_checkbox(UiLayout *L, const char *label, bool *val) {
    UiContext *ui = L->ui;
    float h = ui_checkbox_height(ui);
    bool chg = ui_checkbox(ui, L->x + L->indent, L->cursor, label, val);
    L->cursor += h + L->row_gap;
    return chg;
}

bool ui_layout_row_checkbox(UiLayout *L, const char *row_label, const char *box_label, bool *val) {
    UiContext *ui = L->ui;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float box_h = ui_checkbox_height(ui);
    float min_w = 22.0f * z + 10.0f * z
                + (float)ui_text_width_px(ui, UI_FACE_BODY, 13.0f * sc, false, box_label ? box_label : "");
    UiRow r = ui_row_auto(L, row_label, false, min_w, box_h);
    /* 复选框贴右，与下拉框、输入框同一右边缘 */
    float bx = qlcg_maxf(r.ctl_x, r.ctl_x + r.ctl_w - min_w);
    return ui_checkbox(ui, bx, r.y + (r.h - box_h) * 0.5f, box_label ? box_label : "", val);
}

/* 控件贴右：宽度不超过控件区时靠右边缘，避免右侧留白 */
void ui_row_fit_right(const UiRow *r, float want_w, float *out_x, float *out_w) {
    float w = qlcg_minf(r->ctl_w, qlcg_maxf(0.0f, want_w));
    *out_w = w;
    *out_x = r->ctl_x + r->ctl_w - w;
}

bool ui_layout_select(UiLayout *L, uint32_t id, const char *label,
                      const char *const *items, int count, int current) {
    UiContext *ui = L->ui;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float sh = ui_select_height(ui, 28.0f * z);
    float max_tw = 80.0f * z;
    for (int i = 0; i < count; i++) {
        float tw = (float)ui_text_width_px(ui, UI_FACE_BODY, 12.5f * sc, false, items[i]) + 36.0f * z;
        if (tw > max_tw) max_tw = tw;
    }
    float min_ctl = qlcg_minf(max_tw, (L->w - L->indent) * 0.55f);
    UiRow r = ui_row_auto(L, label, false, min_ctl, sh);
    float sx, sw;
    ui_row_fit_right(&r, qlcg_maxf(260.0f * z, max_tw), &sx, &sw);
    return ui_select(ui, id, sx, r.y + (r.h - sh) * 0.5f, sw, sh, items, count, current);
}

bool ui_layout_height_input(UiLayout *L, uint32_t id, const char *label,
                            MeasurementSystem sys, int *height_cm, int min_cm, int max_cm) {
    UiContext *ui = L->ui;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float ih = ui_ctl_height(28.0f * z, 13.0f * sc);
    UiRow r  = ui_row_auto(L, label, false, 160.0f * z, ih);
    float iw = qlcg_minf(r.ctl_w, qlcg_maxf(220.0f * z, 13.0f * sc * 11.0f));
    float ix = r.ctl_x + r.ctl_w - iw;
    return ui_height_input(ui, id, ix, r.y + (r.h - ih) * 0.5f, iw, ih, sys, height_cm, min_cm, max_cm);
}

bool ui_layout_number_input(UiLayout *L, uint32_t id, const char *label,
                            float *value, float min_value, float max_value, float step,
                            int decimals, const char *suffix) {
    UiContext *ui = L->ui;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float ih = ui_ctl_height(28.0f * z, 13.0f * sc);
    UiRow r  = ui_row_auto(L, label, false, 160.0f * z, ih);
    float iw = qlcg_minf(r.ctl_w, qlcg_maxf(220.0f * z, 13.0f * sc * 11.0f));
    float ix = r.ctl_x + r.ctl_w - iw;
    return ui_number_input(ui, id, ix, r.y + (r.h - ih) * 0.5f, iw, ih,
                           value, min_value, max_value, step, decimals, suffix);
}

bool ui_layout_text_input(UiLayout *L, uint32_t id, const char *label,
                          char *buf, size_t cap, uint32_t max_chars,
                          const char *placeholder) {
    UiContext *ui = L->ui;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float ih = ui_ctl_height(28.0f * z, 13.0f * sc);
    UiRow r  = ui_row_auto(L, label, false, 160.0f * z, ih);
    float iw = qlcg_minf(r.ctl_w, qlcg_maxf(240.0f * z, 13.0f * sc * 12.0f));
    float ix = r.ctl_x + r.ctl_w - iw;
    return ui_text_input(ui, id, ix, r.y + (r.h - ih) * 0.5f, iw, ih,
                         buf, cap, max_chars, placeholder);
}

/* 准心样例：标签独占一行，下方排方框 */
int ui_layout_crosshair(UiLayout *L, const char *label, int current) {
    UiContext *ui = L->ui;
    float z   = ui->met.size;
    float sc  = ui->met.scale;
    float bs  = 48.0f * z;
    float gap = 12.0f * z;

    ui_row_auto(L, label, false, 0.0f, 0.0f);

    float x0 = L->x + L->indent;
    float y  = L->cursor;
    int picked = -1;
    for (int i = 0; i < 5; i++) {
        float bx  = x0 + (float)i * (bs + gap);
        bool  sel = (i == current);
        bool  hov = ui_hit(ui, bx, y, bs, bs);
        ui_draw_rect(ui, bx, y, bs, bs, tk_of(ui, sel ? QL_PRIMARY_SOFT : QL_SURFACE));
        ui_draw_border(ui, bx, y, bs, bs, sel ? 1.5f : 1.0f,
                       (sel || hov) ? tk_of(ui, QL_LINE_PRIMARY) : tk_of(ui, QL_LINE_SOFT));
        if (i == CROSSHAIR_NONE) {
            float px = 11.5f * sc;
            ui_draw_text_px_center(ui, UI_FACE_BODY, px, false, bx + bs * 0.5f,
                                   y + (bs - px * 1.30f) * 0.5f,
                                   tr("settings.crosshair.none"), tk_of(ui, QL_INK_3));
        } else {
            ui_crosshair_glyph(ui, bx + bs * 0.5f, y + bs * 0.5f, bs * 0.60f, i,
                               tk_of(ui, QL_INK));
        }
        if (hov && ui->mouse_clicked) picked = i;
    }
    L->cursor = y + bs + L->row_gap;
    return picked;
}

bool ui_layout_slider(UiLayout *L, const char *label, float *val,
                      float vmin, float vmax, const char *fmt) {
    UiContext *ui = L->ui;
    float x = L->x + L->indent;
    float w = L->w - L->indent;
    bool chg = ui_slider_float(ui, x, L->cursor, w, label, val, vmin, vmax, fmt);
    L->cursor += ui_slider_height(ui) + L->row_gap;
    return chg;
}

/* 步进滑块：整数值只能在档位之间跳，不做连续取值 */
bool ui_layout_slider_steps(UiLayout *L, const char *label, float *val,
                            float vmin, float vmax, int steps, float limit_max,
                            const char *const *step_labels, const char *fmt) {
    UiContext *ui = L->ui;
    float x = L->x + L->indent;
    float w = L->w - L->indent;
    bool chg = ui_slider_float_ex(ui, x, L->cursor, w, label, val, vmin, vmax,
                                  steps, limit_max, step_labels, fmt);
    L->cursor += ui_slider_height(ui) + L->row_gap;
    return chg;
}

void ui_layout_kv(UiLayout *L, const char *label, const char *value) {
    UiContext *ui = L->ui;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float px = 11.0f * sc;
    float lh = ui_line_height(UI_FACE_BODY, px);
    float x  = L->x + L->indent;
    float w  = L->w - L->indent;
    float lw = (float)ui_text_width_px(ui, UI_FACE_BODY, px, false, label ? label : "");
    float vw = (float)ui_text_width_px(ui, UI_FACE_BODY, px, false, value ? value : "");

    if (lw + vw + 12.0f * z <= w) {
        ui_draw_text_px(ui, UI_FACE_BODY, px, false, x, L->cursor, label, tk_of(ui, QL_INK_3));
        ui_draw_text_px_right(ui, UI_FACE_BODY, px, false, x + w, L->cursor, value, tk_of(ui, QL_INK));
        L->cursor += lh + 4.0f * z;
    } else {
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, px, false, x, L->cursor, w, 0, label, tk_of(ui, QL_INK_3));
        L->cursor += lh;
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, px, false, x, L->cursor, w, 2, value, tk_of(ui, QL_INK));
        L->cursor += lh + 4.0f * z;
    }
    ui_draw_rect(ui, x, L->cursor - 2.0f * z, w, 1.0f, with_alpha(tk_of(ui, QL_LINE_SOFT), 0.55f));
    L->cursor += 4.0f * z;
}

bool ui_layout_color(UiLayout *L, const char *title, uint32_t *color) {
    UiContext *ui = L->ui;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float x  = L->x + L->indent;
    float w  = L->w - L->indent;
    bool changed = false;

    float tpx = 11.5f * sc;
    float tlh = ui_line_height(UI_FACE_BODY, tpx);
    char hex[16];
    snprintf(hex, sizeof(hex), "#%06X", *color & 0xFFFFFFu);
    float hex_w = (float)ui_text_width_px(ui, UI_FACE_MONO, 11.0f * sc, false, hex);
    float box_w = 22.0f * z;
    float box_h = qlcg_maxf(20.0f * z, tlh * 0.85f);
    float right_w = box_w + 6.0f * z + hex_w;
    float title_w = (float)ui_text_width_px(ui, UI_FACE_BODY, tpx, false, title ? title : "");

    if (title_w + 12.0f * z + right_w <= w) {
        float row_h = qlcg_maxf(tlh, box_h);
        ui_draw_text_px(ui, UI_FACE_BODY, tpx, false, x, L->cursor + (row_h - tlh) * 0.5f,
                        title, tk_of(ui, QL_INK_2));
        float swx = x + w - right_w;
        float swy = L->cursor + (row_h - box_h) * 0.5f;
        ui_draw_rect(ui, swx, swy, box_w, box_h, ui_rgb_hex(*color, 255));
        ui_draw_border(ui, swx, swy, box_w, box_h, 1.0f, tk_of(ui, QL_LINE_SOFT));
        ui_draw_text_px_right(ui, UI_FACE_MONO, 11.0f * sc, false, x + w,
                              L->cursor + (row_h - tlh) * 0.5f, hex, tk_of(ui, QL_INK));
        L->cursor += row_h + 6.0f * z;
    } else {
        int nl = ui_draw_text_wrapped(ui, UI_FACE_BODY, tpx, false, x, L->cursor, w, tlh,
                                      title, tk_of(ui, QL_INK_2));
        L->cursor += (float)(nl > 0 ? nl : 1) * tlh + 4.0f * z;
        ui_draw_rect(ui, x, L->cursor, box_w, box_h, ui_rgb_hex(*color, 255));
        ui_draw_border(ui, x, L->cursor, box_w, box_h, 1.0f, tk_of(ui, QL_LINE_SOFT));
        ui_draw_text_px(ui, UI_FACE_MONO, 11.0f * sc, false, x + box_w + 6.0f * z,
                        L->cursor + (box_h - tlh) * 0.5f, hex, tk_of(ui, QL_INK));
        L->cursor += box_h + 6.0f * z;
    }

    const char *names[3] = {"R", "G", "B"};
    float sh = ui_slider_height(ui);
    for (int i = 0; i < 3; i++) {
        float v = (float)((*color >> (16 - 8 * i)) & 0xFF);
        char fmt[8];
        snprintf(fmt, sizeof(fmt), "%d", (int)(v + 0.5f));
        if (ui_slider_float(ui, x + 12.0f * z, L->cursor, qlcg_maxf(20.0f, w - 12.0f * z),
                            names[i], &v, 0.0f, 255.0f, fmt)) {
            int iv = qlcg_clampi((int)(v + 0.5f), 0, 255);
            uint32_t mask = ~(0xFFu << (16 - 8 * i));
            *color = (*color & mask) | ((uint32_t)iv << (16 - 8 * i));
            changed = true;
        }
        L->cursor += sh + 4.0f * z;
    }
    L->cursor += L->row_gap * 0.5f;
    return changed;
}

bool ui_layout_fold(UiLayout *L, uint32_t id, const char *title, bool *expanded) {
    (void)id;
    UiContext *ui = L->ui;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float px = 12.0f * sc;
    float lh = ui_line_height(UI_FACE_BODY, px);
    float h  = qlcg_maxf(30.0f * z, lh + 10.0f * z);

    float x = L->x + L->indent;
    float w = L->w - L->indent;
    bool hov = ui_hit(ui, x, L->cursor, w, h);
    if (hov) {
        ui_draw_rect(ui, x, L->cursor, w, h, tk_of(ui, QL_SURFACE_HI));
    }
    ui_draw_rect(ui, x, L->cursor + h - 1.0f, w, 1.0f, tk_of(ui, QL_LINE_FAINT));

    char buf[160];
    snprintf(buf, sizeof(buf), "%s  %s", *expanded ? "\xE2\x96\xBE" : "\xE2\x96\xB8", title);
    ui_draw_text_ellipsis(ui, UI_FACE_BODY, px, true, x + 6.0f * z,
                          L->cursor + (h - lh) * 0.5f, qlcg_maxf(20.0f, w - 12.0f * z), 0,
                          buf, tk_of(ui, QL_INK));
    if (hov && ui->mouse_clicked) {
        *expanded = !*expanded;
    }
    L->cursor += h + 4.0f * z;
    return *expanded;
}

float ui_layout_split_head(UiContext *ui, float x, float y, float w,
                           float title_px, const char *title,
                           float sub_px, const char *subtitle) {
    float z = ui->met.size;
    float tlh = ui_line_height(UI_FACE_DISPLAY, title_px);
    float slh = ui_line_height(UI_FACE_BODY, sub_px);
    float tw = (title && title[0]) ? (float)ui_text_width_px(ui, UI_FACE_DISPLAY, title_px, false, title) : 0.0f;
    float sw = (subtitle && subtitle[0]) ? (float)ui_text_width_px(ui, UI_FACE_BODY, sub_px, false, subtitle) : 0.0f;

    ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, title_px, false, x, y, w, 0, title, tk_of(ui, QL_INK));
    float bottom = y + tlh;

    if (subtitle && subtitle[0]) {
        if (tw + 16.0f * z + sw <= w) {
            ui_draw_text_px_right(ui, UI_FACE_BODY, sub_px, false, x + w,
                                  y + qlcg_maxf(0.0f, tlh - slh), subtitle, tk_of(ui, QL_INK_3));
        } else {
            int nl = ui_draw_text_wrapped(ui, UI_FACE_BODY, sub_px, false, x, bottom + 2.0f * z,
                                          w, slh, subtitle, tk_of(ui, QL_INK_3));
            bottom += 2.0f * z + (float)(nl > 0 ? nl : 1) * slh;
        }
    }
    bottom += 6.0f * z;
    ui_draw_rect(ui, x, bottom, w, 1.0f, tk_of(ui, QL_LINE_SOFT));
    return bottom;
}
