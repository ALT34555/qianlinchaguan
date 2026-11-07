#include "core/dev_tools.h"
/* 组件库：按钮、滑条、勾选、页签、面板与阴影 */
#include "ui_widgets.h"
#include "ui_core.h"
#include "ui_sprite.h"
#include "../core/math_util.h"
#include "../core/i18n.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif

/* 柔和外阴影：由外向内叠层 */
void ui_shadow_offset(UiContext *ui, float x, float y, float w, float h, float blur,
                      float dx, float dy, uint32_t color) {
    int steps = (int)qlcg_clampf(blur * 0.6f, 3.0f, 22.0f);
    uint32_t alpha = (color >> 24) & 0xFF;
    for (int i = steps; i >= 1; i--) {
        float t = (float)i / (float)steps;
        float grow = blur * t;
        uint8_t a = (uint8_t)((float)alpha * (1.0f - t) * 0.55f);
        if (a == 0) continue;
        uint32_t c = (color & 0x00FFFFFFu) | ((uint32_t)a << 24);
        ui_draw_rect(ui, x - grow + dx * t, y - grow + dy * t,
                     w + grow * 2.0f, h + grow * 2.0f, c);
    }
}

void ui_shadow_soft(UiContext *ui, float x, float y, float w, float h, float blur, float dy, uint32_t color) {
    ui_shadow_offset(ui, x, y, w, h, blur, 0.0f, dy, color);
}

/* 浮雕：右下暗影 + 左上亮影 */
void ui_neu(UiContext *ui, float x, float y, float w, float h, float blur, uint32_t dark, uint32_t light) {
    ui_shadow_offset(ui, x, y, w, h, blur, blur * 0.55f, blur * 0.75f, dark);
    ui_shadow_offset(ui, x, y, w, h, blur, -blur * 0.55f, -blur * 0.75f, light);
}

/* 内凹新拟态：偏移阴影裁进矩形，仅留内侧 */
void ui_neu_in(UiContext *ui, float x, float y, float w, float h, float blur,
               uint32_t dark, uint32_t light) {
    ui_clip_push(ui, x, y, w, h);
    ui_shadow_offset(ui, x, y, w, h, blur, blur * 0.55f, blur * 0.75f, dark);
    ui_shadow_offset(ui, x, y, w, h, blur, -blur * 0.55f, -blur * 0.75f, light);
    ui_clip_pop(ui);
}

/* 纸面卡片 */
void ui_panel(UiContext *ui, float x, float y, float w, float h, float blur,
              uint32_t bg, uint32_t border, uint32_t shadow) {
    if (blur > 0.0f) ui_shadow_soft(ui, x, y, w, h, blur, 2.0f, shadow);
    ui_draw_rect(ui, x, y, w, h, bg);
    if (border) ui_draw_border(ui, x, y, w, h, 1.0f, border);
}

void ui_edge_light(UiContext *ui, float x, float y, float w, float h, uint32_t light, uint32_t dark) {
    if (light) ui_draw_rect(ui, x, y, w, 1.0f, light);
    if (dark)  ui_draw_rect(ui, x, y + h - 1.0f, w, 1.0f, dark);
}

/* 裁剪区（嵌套时与父裁剪求交集） */
void ui_clip_push(UiContext *ui, float x, float y, float w, float h) {
    ui_flush(ui);
    if (ui->clip_depth >= 8) return;
    if (ui->clip_depth > 0) {
        float *p = ui->clip_stack[ui->clip_depth - 1];
        float x0 = qlcg_maxf(x, p[0]);
        float y0 = qlcg_maxf(y, p[1]);
        float x1 = qlcg_minf(x + w, p[0] + p[2]);
        float y1 = qlcg_minf(y + h, p[1] + p[3]);
        x = x0; y = y0;
        w = qlcg_maxf(0.0f, x1 - x0);
        h = qlcg_maxf(0.0f, y1 - y0);
    }
    ui->clip_stack[ui->clip_depth][0] = x;
    ui->clip_stack[ui->clip_depth][1] = y;
    ui->clip_stack[ui->clip_depth][2] = w;
    ui->clip_stack[ui->clip_depth][3] = h;
    ui->clip_depth++;
    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)floorf(x + ui->global_dx),
              (GLint)floorf((float)ui->win_h - (y + h + ui->global_dy)),
              (GLsizei)qlcg_maxf(w, 1.0f), (GLsizei)qlcg_maxf(h, 1.0f));
}

void ui_clip_pop(UiContext *ui) {
    ui_flush(ui);
    if (ui->clip_depth > 0) ui->clip_depth--;
    if (ui->clip_depth == 0) {
        glDisable(GL_SCISSOR_TEST);
    } else {
        /* 恢复到上一层裁剪 */
        float *r = ui->clip_stack[ui->clip_depth - 1];
        glScissor((GLint)floorf(r[0] + ui->global_dx),
                  (GLint)floorf((float)ui->win_h - (r[1] + r[3] + ui->global_dy)),
                  (GLsizei)qlcg_maxf(r[2], 1.0f), (GLsizei)qlcg_maxf(r[3], 1.0f));
    }
}

float ui_ctl_height(float base_h, float font_px) {
    float need_h = font_px * 1.72f;
    return base_h > need_h ? base_h : need_h;
}

float ui_checkbox_height(const UiContext *ui) {
    float box = 20.0f * ui->met.size;
    float lh  = ui_line_height(UI_FACE_BODY, 13.0f * ui->met.scale);
    return box > lh ? box : lh;
}

float ui_select_height(const UiContext *ui, float base_h) {
    float need = 12.5f * ui->met.scale * 1.9f;
    return base_h > need ? base_h : need;
}

static bool has_active_ripple(const UiContext *ui) {
    for (int i = 0; i < UI_RIPPLE_SLOTS; i++) {
        if (ui->ripples[i].active) return true;
    }
    return false;
}

/* 按钮：主 / 次 / 文字 / 危险，含悬停位移与水波 */
/* 标签逐字推进 0.1em，对齐 base button */
static int wgt_utf8_step(const char *p) {
    unsigned char c = (unsigned char)*p;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static void btn_label_tracked(UiContext *ui, float x, float y, float w, float px,
                              bool align_left, bool bold, const char *text, uint32_t color) {
    if (!text || !text[0]) return;
    float track = px * 0.1f;
    float total = 0.0f;
    int   n     = 0;
    for (const char *p = text; *p; p += wgt_utf8_step(p)) {
        int step = wgt_utf8_step(p);
        char g[5] = {0};
        memcpy(g, p, (size_t)step);
        total += (float)ui_text_width_px(ui, UI_FACE_BODY, px, bold, g);
        n++;
    }
    total += track * (float)n;
    if (total > w + 2.0f) {   /* 超宽退回省略号版本 */
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, px, bold, x, y, w,
                              align_left ? 0 : 1, text, color);
        return;
    }
    float cx = align_left ? x : (x + (w - total) * 0.5f);
    for (const char *p = text; *p; p += wgt_utf8_step(p)) {
        int step = wgt_utf8_step(p);
        char g[5] = {0};
        memcpy(g, p, (size_t)step);
        ui_draw_text_px(ui, UI_FACE_BODY, px, bold, cx, y, g, color);
        cx += (float)ui_text_width_px(ui, UI_FACE_BODY, px, bold, g) + track;
    }
}

bool ui_btn(UiContext *ui, float x, float y, float w, float h, const char *label,
            UiButtonKind kind, bool align_left, float font_px, bool bold) {
    h = ui_ctl_height(h, font_px);
    uint32_t id = qlcg_hash_str(label)
                ^ ((uint32_t)(x * 3.0f) << 9)
                ^ ((uint32_t)(y * 3.0f) << 17)
                ^ ((uint32_t)kind << 25);
    bool hov = ui_hit(ui, x, y, w, h);
    bool down = hov && ui->mouse_down;
    UiMotion *m = ui_motion_step(ui, id, hov, down, false);
    bool clicked = hov && ui->mouse_clicked;

    if (clicked) {
        ui_ripple_push(ui, (float)ui->mouse_x, (float)ui->mouse_y, qlcg_maxf(w, h) * 1.15f);
    }

    float dy = 0.0f;
    uint32_t bg = 0, fg = tk_of(ui, QL_INK), bd = 0;
    switch (kind) {
        case BTN_PRIMARY:
            bg = tk_of(ui, QL_PRIMARY_UI);
            fg = tk_of(ui, QL_ON_PRIMARY);
            dy = -1.0f * m->hover + 1.4f * m->press;
            ui_shadow_offset(ui, x, y + dy, w, h, 6.0f + 4.0f * m->hover, 0.6f + 1.4f * m->hover,
                             2.0f + 2.4f * m->hover,
                             with_alpha(tk_of(ui, QL_PRIMARY), 0.22f + 0.14f * m->hover));
            ui_draw_rect(ui, x, y + dy, w, h, bg);
            ui_draw_rect(ui, x, y + dy, w, 1.0f, ui_rgba(255, 255, 255, 56));
            ui_draw_rect(ui, x, y + dy + h - 2.0f, w, 2.0f, ui_rgba(0, 0, 0, 30));
            break;
        case BTN_GHOST:
            bg = tk_of(ui, QL_SURFACE);
            fg = hov ? tk_of(ui, QL_PRIMARY_TEXT) : tk_of(ui, QL_INK);
            bd = hov ? tk_of(ui, QL_LINE_PRIMARY) : tk_of(ui, QL_LINE_SOFT);
            dy = -1.0f * m->hover;
            ui_neu(ui, x, y + dy, w, h, 4.0f + 3.0f * m->hover,
                   tk_of(ui, QL_NEU_DARK), tk_of(ui, QL_NEU_LIGHT));
            ui_draw_rect(ui, x, y + dy, w, h, bg);
            ui_draw_border(ui, x, y + dy, w, h, 1.0f, bd);
            break;
        case BTN_DANGER:
            bg = tk_of(ui, QL_HIGHLIGHT);
            fg = ui_rgba(255, 255, 255, 255);
            dy = -1.0f * m->hover;
            ui_draw_rect(ui, x, y + dy, w, h, bg);
            break;
        case BTN_SEGMENT:
            bg = with_alpha(tk_of(ui, QL_PRIMARY), 0.06f * m->hover);
            fg = hov ? tk_of(ui, QL_INK) : tk_of(ui, QL_INK_2);
            bd = tk_of(ui, QL_LINE_SOFT);
            dy = 0.0f;
            if (m->hover > 0.01f) ui_draw_rect(ui, x, y, w, h, bg);
            ui_draw_border(ui, x, y, w, h, 1.0f, bd);
            break;
        case BTN_SEGMENT_ON:
            bg = tk_of(ui, QL_PRIMARY_SOFT);
            fg = tk_of(ui, QL_PRIMARY_TEXT);
            bd = tk_of(ui, QL_LINE_PRIMARY);
            dy = 0.0f;
            ui_draw_rect(ui, x, y, w, h, bg);
            ui_draw_border(ui, x, y, w, h, 1.0f, bd);
            break;
        case BTN_QUIET:
        default:
            bg = with_alpha(tk_of(ui, QL_ACCENT_SOFT), 0.0f + m->hover);
            fg = hov ? tk_of(ui, QL_INK) : tk_of(ui, QL_INK_2);
            if (m->hover > 0.01f) ui_draw_rect(ui, x, y, w, h, bg);
            break;
    }

    if (has_active_ripple(ui)) {
        if (kind == BTN_PRIMARY || kind == BTN_GHOST || kind == BTN_DANGER) {
            ui_clip_push(ui, x, y + dy, w, h);
            ui_ripple_draw(ui, with_alpha(tk_of(ui, QL_ON_PRIMARY), 0.28f));
            ui_clip_pop(ui);
        } else {
            ui_ripple_draw(ui, with_alpha(tk_of(ui, QL_PRIMARY), 0.18f));
        }
    }

    float ty = y + dy + (h - font_px) * 0.5f - font_px * 0.12f;
    float pad = align_left ? 18.0f * ui->met.size : 6.0f * ui->met.size;
    if (w > pad * 2.0f) {
        btn_label_tracked(ui, x + pad, ty, w - pad * 2.0f, font_px, align_left, bold, label, fg);
    } else {
        btn_label_tracked(ui, x + 2.0f, ty, qlcg_maxf(4.0f, w - 4.0f), font_px, false, bold, label, fg);
    }
    return clicked;
}

bool ui_pointer_blocked(const UiContext *ui) {
    return ui->input_blocked || ui->toast_blocked ||
           (ui->select_id != 0 && !ui->select_input);
}

void ui_pointer_consume(UiContext *ui) {
    ui->mouse_clicked = false;
    ui->mouse_dblclicked = false;
    ui->right_clicked = false;
    ui->wheel_delta = 0;
    ui->last_click_ms = 0;
}

bool ui_pointer_in_rect(const UiContext *ui, float x, float y, float w, float h) {
    return w > 0 && h > 0 && (float)ui->mouse_x >= x && (float)ui->mouse_x < x + w &&
           (float)ui->mouse_y >= y && (float)ui->mouse_y < y + h;
}

bool ui_hit(const UiContext *ui, float x, float y, float w, float h) {
    if (ui_pointer_blocked(ui)) return false;
    float mx = (float)ui->mouse_x, my = (float)ui->mouse_y;
    if (ui->clip_depth > 0) {
        const float *r = ui->clip_stack[ui->clip_depth - 1];
        if (mx < r[0] || mx >= r[0] + r[2] ||
            my < r[1] || my >= r[1] + r[3]) return false;
    }
    return ui_pointer_in_rect(ui, x, y, w, h);
}

bool ui_button(UiContext *ui, float x, float y, float w, float h, const char *label, bool primary, bool active) {
    bool hov = ui_hit(ui, x, y, w, h);
    uint32_t bg = ui->pal.bg_card;
    uint32_t bd = ui->pal.border_soft;
    uint32_t fg = ui->pal.text_main;

    if (primary) {
        bg = hov ? ui_rgba(78, 128, 95, 255) : ui->pal.accent_tea;
        bd = ui->pal.border_gold;
        fg = ui_rgba(250, 247, 240, 255);
    } else if (active) {
        bg = ui->tk.dark ? ui_rgba(58, 68, 60, 255) : ui_rgba(222, 212, 192, 255);
        bd = ui->pal.border_gold;
    } else if (hov) {
        bg = ui->pal.bg_card_hover;
        bd = ui->pal.border_gold;
    }

    ui_draw_rect(ui, x, y, w, h, bg);
    ui_draw_border(ui, x, y, w, h, active || primary ? 1.5f : 1.0f, bd);
    ui_draw_text_center(ui, primary ? FONT_BOLD : FONT_BODY, x + w * 0.5f, y + (h - 20.0f) * 0.5f, label, fg);
    return hov && ui->mouse_clicked;
}

bool ui_pill(UiContext *ui, float x, float y, float w, float h, const char *label, bool active) {
    float px = 12.0f * ui->met.scale;
    h = ui_ctl_height(h, px);
    bool hov = ui_hit(ui, x, y, w, h);
    uint32_t bg = active ? ui->pal.accent_tea : (hov ? ui->pal.bg_card_hover : ui->pal.bg_card);
    uint32_t bd = active ? ui->pal.border_gold : ui->pal.border_soft;
    uint32_t fg = active ? ui_rgba(250, 247, 240, 255) : ui->pal.text_main;
    ui_draw_rect(ui, x, y, w, h, bg);
    ui_draw_border(ui, x, y, w, h, 1.0f, bd);
    ui_draw_text_ellipsis(ui, UI_FACE_BODY, px, false, x + 4.0f,
                          y + (h - px * 1.35f) * 0.5f, qlcg_maxf(4.0f, w - 8.0f), 1, label, fg);
    return hov && ui->mouse_clicked;
}

/* 复选框动画槽：按位置记录切换，供逐帧插值 */
#define CB_ANIM_SLOTS 32
typedef struct { float x, y, t0; bool to; } CbAnim;
static CbAnim s_cb[CB_ANIM_SLOTS];
static int    s_cb_next = 0;

static int cb_anim_slot(float x, float y) {
    for (int i = 0; i < CB_ANIM_SLOTS; i++)
        if (s_cb[i].t0 > 0.0f && s_cb[i].x == x && s_cb[i].y == y) return i;
    return -1;
}

/* 两段折线按进度截断画勾（同 dashoffset） */
static void cb_draw_tick(UiContext *ui, float x, float y, float box, float prog, uint32_t color) {
    if (prog <= 0.0f) return;
    float ax = x + box * 0.22f, ay = y + box * 0.52f;
    float bx = x + box * 0.42f, by = y + box * 0.74f;
    float cx = x + box * 0.78f, cy = y + box * 0.26f;
    float l1 = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    float l2 = sqrtf((cx - bx) * (cx - bx) + (cy - by) * (cy - by));
    float total = l1 + l2;
    float want  = total * qlcg_clampf(prog, 0.0f, 1.0f);
    if (want <= 0.0f) return;
    if (want <= l1) {
        float t = want / l1;
        ui_draw_line(ui, ax, ay, ax + (bx - ax) * t, ay + (by - ay) * t, 2.2f * ui->met.size, color);
        return;
    }
    ui_draw_line(ui, ax, ay, bx, by, 2.2f * ui->met.size, color);
    float t = (want - l1) / l2;
    ui_draw_line(ui, bx, by, bx + (cx - bx) * t, by + (cy - by) * t, 2.2f * ui->met.size, color);
}

bool ui_checkbox(UiContext *ui, float x, float y, const char *label, bool *val) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float box = 20.0f * z;          /* base .q-checkbox-art 为 20×20 */
    float label_px = 13.0f * sc;
    float row_h = ui_checkbox_height(ui);
    float by = y + (row_h - box) * 0.5f;
    int tw = ui_text_width_px(ui, UI_FACE_BODY, label_px, false, label);
    bool hov = ui_hit(ui, x, y, box + 10.0f * z + (float)tw, row_h);

    /* 进度：勾选 1.6s、取消 0.6s，简化时直接到终态 */
    float p = -1.0f;
    int   slot = cb_anim_slot(x, y);
    if (slot >= 0) {
        float dur = s_cb[slot].to ? 1.6f : 0.6f;
        p = (ui->time - s_cb[slot].t0) / dur;
        if (p >= 1.0f || ui->reduce_motion) { s_cb[slot].t0 = 0.0f; p = -1.0f; }
    }
    bool  checked = *val;
    float fill_a  = checked ? 1.0f : 0.0f;      /* 填充透明度 */
    float tick_p  = checked ? 1.0f : 0.0f;      /* 勾绘制进度 */
    float box_mix = checked ? 1.0f : 0.0f;      /* 描边主色混合 */
    if (p >= 0.0f) {
        if (s_cb[slot].to) {                    /* 勾选：20~40% 填充，26~44% 勾 */
            fill_a  = (p < 0.20f) ? 0.0f : (p < 0.40f ? (p - 0.20f) / 0.20f : 1.0f);
            box_mix = fill_a;
            tick_p  = (p < 0.26f) ? 0.0f : (p < 0.44f ? (p - 0.26f) / 0.18f : 1.0f);
        } else {                                /* 取消：线性回退 */
            fill_a  = 1.0f - p;
            box_mix = 1.0f - p;
            tick_p  = 1.0f - p;
        }
    }

    if (fill_a > 0.01f)
        ui_draw_rect(ui, x, by, box, box, with_alpha(tk_of(ui, QL_PRIMARY_UI), fill_a));

    uint32_t edge_idle = with_alpha(tk_of(ui, QL_INK), hov ? 0.55f : 0.36f);
    uint32_t edge = edge_idle;
    if (box_mix > 0.01f) {
        QlColor a = ql_rgba_f(edge_idle & 0x00FFFFFFu, (float)((edge_idle >> 24) & 0xFF) / 255.0f);
        QlColor b = ql_rgba_f(tk_of(ui, QL_PRIMARY_TEXT) & 0x00FFFFFFu, 1.0f);
        edge = (uint32_t)ql_to_rgba(ql_mix(a, 1.0f - box_mix, b, box_mix));
    }
    ui_draw_border(ui, x, by, box, box, checked || box_mix > 0.5f ? 1.0f : 1.5f * z, edge);

    if (tick_p > 0.01f)
        cb_draw_tick(ui, x, by, box, tick_p, tk_of(ui, QL_ON_PRIMARY));

    if (label[0]) {
        ui_draw_text_px(ui, UI_FACE_BODY, label_px, false, x + box + 10.0f * z,
                        y + (row_h - label_px * 1.30f) * 0.5f, label, tk_of(ui, QL_INK));
    }
    if (hov && ui->mouse_clicked) {
        *val = !(*val);
        int s = s_cb_next++ % CB_ANIM_SLOTS;
        s_cb[s].x = x; s_cb[s].y = y; s_cb[s].t0 = ui->time; s_cb[s].to = *val;
        return true;
    }
    return false;
}

/* 滑条：细轨道 + 主色进度 + 圆滑钮 */
bool ui_slider_at_ex(UiContext *ui, float x, float ty, float w, float *val,
                     float vmin, float vmax, int steps, float limit_max,
                     const char *const *step_labels) {
    float z  = ui->met.size;
    float th = 6.0f * z;
    int   my = ui->slider_seq++;

    /* 拖动锁定在按下那一刻 */
    bool dragging = (ui->drag_slider_id == my);
    float hx = dragging ? ui->drag_sx : x;
    float hy = dragging ? ui->drag_sy : ty;
    float hw = dragging ? ui->drag_sw : w;
    float hit_h = qlcg_maxf(22.0f * z, 14.0f * z);

    bool hov = ui_hit(ui, hx - 4.0f, hy - hit_h * 0.5f, hw + 8.0f, hit_h);
    bool changed = false;
    if (ui->mouse_down && !ui_pointer_blocked(ui) &&
        (dragging || (hov && ui->mouse_clicked))) {
        if (ui->drag_slider_id < 0) {
            ui->drag_slider_id = my;
            ui->drag_sx = x;
            ui->drag_sy = ty;
            ui->drag_sw = w;
        }
        float t = qlcg_clampf(((float)ui->mouse_x - hx) / qlcg_maxf(hw, 1.0f), 0.0f, 1.0f);
        float nv;
        if (steps > 1) {
            /* 步进：吸附到最近档位，不做连续取值 */
            int idx = (int)(t * (float)(steps - 1) + 0.5f);
            idx = qlcg_clampi(idx, 0, steps - 1);
            nv = vmin + (float)idx;
        } else {
            nv = vmin + t * (vmax - vmin);
        }
        if (limit_max > vmin && nv > limit_max) nv = limit_max;
        if (fabsf(nv - *val) > 1e-4f) {
            *val = nv;
            changed = true;
        }
    }
    float ratio = (vmax > vmin) ? qlcg_clampf((*val - vmin) / (vmax - vmin), 0.0f, 1.0f) : 0.0f;
    /* base：左 6px 主色实心条，右 1px line 细线 */
    float mid_y = ty + th * 0.5f;
    if (w * (1.0f - ratio) > 0.0f)
        ui_draw_rect(ui, x + w * ratio, mid_y - 0.5f * z, w * (1.0f - ratio), 1.0f * z,
                     tk_of(ui, QL_LINE));

    /* 步进档位：细线上标竖点，档位名写在线下方 */
    if (steps > 1) {
        float sc = ui->met.scale;
        float tpx = 10.5f * sc;
        for (int i = 0; i < steps; i++) {
            float rx = x + w * (float)i / (float)(steps - 1);
            ui_draw_rect(ui, rx - 0.5f * z, mid_y - 3.0f * z, 1.0f * z, 6.0f * z,
                         tk_of(ui, QL_LINE));
            if (step_labels && step_labels[i] && step_labels[i][0]) {
                float tw = (float)ui_text_width_px(ui, UI_FACE_BODY, tpx, false, step_labels[i]);
                float tx = rx - tw * 0.5f;
                if (tx < x) tx = x;
                if (tx + tw > x + w) tx = x + w - tw;
                ui_draw_text_px(ui, UI_FACE_BODY, tpx, false, tx,
                                mid_y + 8.0f * z, step_labels[i], tk_of(ui, QL_INK_2));
            }
        }
    }

    if (w * ratio > 0.0f)
        ui_draw_rect(ui, x, mid_y - th * 0.5f, w * ratio, th, tk_of(ui, QL_PRIMARY_UI));

    /* 滑块：14px 方块转 45° 菱形，2px 浅边 */
    float kx  = x + w * ratio;
    float ro  = 9.9f * z;   /* 14 × √2 / 2 */
    float ri  = 7.1f * z;
    float ox4[4] = { kx, kx + ro, kx, kx - ro };
    float oy4[4] = { mid_y - ro, mid_y, mid_y + ro, mid_y };
    float ix4[4] = { kx, kx + ri, kx, kx - ri };
    float iy4[4] = { mid_y - ri, mid_y, mid_y + ri, mid_y };
    ui_draw_poly(ui, ox4, oy4, 4, tk_of(ui, QL_SURFACE_HI));
    ui_draw_poly(ui, ix4, iy4, 4, tk_of(ui, QL_PRIMARY_UI));
    return changed;
}

bool ui_slider_at(UiContext *ui, float x, float ty, float w, float *val, float vmin, float vmax) {
    return ui_slider_at_ex(ui, x, ty, w, val, vmin, vmax, 0, vmin, NULL);
}

/* 完整滑块高度：标签行 + 间距 + 轨道 + 下留白 */
float ui_slider_height(UiContext *ui) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    return ui_line_height(UI_FACE_BODY, 11.5f * sc) + 10.0f * z + 8.0f * z;
}

bool ui_slider_float_ex(UiContext *ui, float x, float y, float w, const char *label,
                        float *val, float vmin, float vmax, int steps, float limit_max,
                        const char *const *step_labels, const char *fmt_val) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float lh = ui_line_height(UI_FACE_BODY, 11.5f * sc);
    float vw = 0.0f;
    if (fmt_val && fmt_val[0]) {
        vw = (float)ui_text_width_px(ui, UI_FACE_MONO, 11.0f * sc, false, fmt_val) + 8.0f * z;
        ui_draw_text_ellipsis(ui, UI_FACE_MONO, 11.0f * sc, false, x, y, w, 2, fmt_val,
                              tk_of(ui, QL_INK));
    }
    if (label && label[0]) {
        float avail_lw = qlcg_maxf(24.0f, w - vw);
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, 11.5f * sc, false, x, y, avail_lw, 0, label,
                              tk_of(ui, QL_INK_2));
    }
    return ui_slider_at_ex(ui, x, y + lh + 6.0f * z, w, val, vmin, vmax, steps, limit_max,
                           step_labels);
}

bool ui_slider_float(UiContext *ui, float x, float y, float w, const char *label,
                     float *val, float vmin, float vmax, const char *fmt_val) {
    return ui_slider_float_ex(ui, x, y, w, label, val, vmin, vmax, 0, vmin, NULL, fmt_val);
}

/* 折叠组标题栏（对齐创世页 地理 / 天文 */
bool ui_fold_header(UiContext *ui, float x, float y, float w, const char *title, bool *expanded) {
    float h = 30.0f;
    bool hov = ui_hit(ui, x, y, w, h);
    ui_draw_rect(ui, x, y, w, h, hov ? ui->pal.bg_card_hover : ui->pal.bg_rail);
    ui_draw_border(ui, x, y, w, h, 1.0f, ui->pal.border_soft);
    char buf[96];
    snprintf(buf, sizeof(buf), "%s  %s", *expanded ? "▾" : "▸", title);
    ui_draw_text(ui, FONT_BOLD, x + 10.0f, y + 5.0f, buf, ui->pal.text_main);
    if (hov && ui->mouse_clicked) {
        *expanded = !(*expanded);
        return true;
    }
    return false;
}

/* 绘制 3D 准星（点 / 十字 / 隐去 */
/* 准心形状：设置页预览与游戏内共用，保证两处一致 */
void ui_crosshair_glyph(UiContext *ui, float cx, float cy, float size, int kind, uint32_t solid) {
    if (kind == CROSSHAIR_NONE || size <= 0.0f) return;
    if (kind == CROSSHAIR_DOT) {
        ui_draw_circle(ui, cx, cy, qlcg_maxf(1.5f, size * 0.10f), solid);
        return;
    }
    if (kind == CROSSHAIR_CROSS) {
        /* 实心十字：中心 2px，臂长各 11/20 直径 */
        float t = qlcg_maxf(2.0f, size * 0.05f);
        float l = size * 0.28f;
        ui_draw_rect(ui, cx - t * 0.5f, cy - l, t, l * 2.0f, solid);
        ui_draw_rect(ui, cx - l, cy - t * 0.5f, l * 2.0f, t, solid);
        return;
    }
    /* 四层合成：米色描边/主色点/原色主体 */
    const SpriteDef *sp = ui_sprite_by_name(kind == CROSSHAIR_AIM ? "crosshair" : "crosshair-ring");
    if (sp) ui_sprite4(ui, sp, cx - size * 0.5f, cy - size * 0.5f, size, size,
                       ui_rgba(241, 229, 199, 255), tk_of(ui, QL_PRIMARY_UI),
                       tk_of(ui, QL_ACCENT_UI));
}

void ui_draw_crosshair(UiContext *ui, CrosshairStyle style) {
    if (style == CROSSHAIR_NONE) return;
    float cx = (float)ui->win_w * 0.5f;
    float cy = (float)ui->win_h * 0.5f;
    if (style == CROSSHAIR_DOT) {
        /* base：5px 米白圆 +1px 深棕外圈，先外后内 */
        ui_draw_circle(ui, cx, cy, 3.5f, with_alpha(tk_of(ui, QL_INK), 0.60f));
        ui_draw_circle(ui, cx, cy, 2.5f, ui_rgba(251, 244, 227, 235));
    } else if (style == CROSSHAIR_CROSS) {
        /* base：2px 白十字臂 22px，外 1px 深描边 */
        uint32_t edge = tk_of(ui, QL_INK);
        uint32_t body = tk_of(ui, QL_INK_INV);
        ui_draw_rect(ui, cx - 2.0f, cy - 12.0f, 4.0f, 24.0f, edge);
        ui_draw_rect(ui, cx - 12.0f, cy - 2.0f, 24.0f, 4.0f, edge);
        ui_draw_rect(ui, cx - 1.0f, cy - 11.0f, 2.0f, 22.0f, body);
        ui_draw_rect(ui, cx - 11.0f, cy - 1.0f, 22.0f, 2.0f, body);
    } else {
        ui_crosshair_glyph(ui, cx, cy, 40.0f, (int)style, ui_rgba(255, 255, 255, 215));
    }
}

/* 书签：纸面+垂绳玉珠，viewBox 200×180 */
void ui_bookmark(UiContext *ui, float x, float y, float w, float h,
                 uint32_t wood, uint32_t wood_deep, uint32_t paper, uint32_t accent) {
    (void)wood; (void)wood_deep;
    float sx = w / 200.0f;
    float sy = h / 180.0f;

    float xs[5] = { 22, 178, 178, 100, 22 };
    float ys[5] = { 20, 20, 129, 112, 129 };
    float px[5], py[5];
    for (int i = 0; i < 5; i++) { px[i] = x + xs[i] * sx; py[i] = y + ys[i] * sy; }

    ui_draw_poly(ui, px, py, 5, paper);
    for (int i = 0; i < 5; i++) {
        int j = (i + 1) % 5;
        ui_draw_line(ui, px[i], py[i], px[j], py[j], 1.2f, accent);
    }
    /* 垂绳、玉珠与流苏 */
    float cw = qlcg_maxf(1.0f, 2.0f * sx);
    ui_draw_line(ui, x + 100.0f * sx, y + 129.0f * sy, x + 100.0f * sx, y + 156.0f * sy, cw, accent);
    ui_draw_circle(ui, x + 100.0f * sx, y + 156.0f * sy, 7.0f * sx, accent);
}

/* 横书签：纸面+绳珠，viewBox 260×120 */
void ui_bookmark_side(UiContext *ui, float x, float y, float w, float h,
                      uint32_t wood, uint32_t wood_deep, uint32_t paper, uint32_t accent) {
    (void)wood; (void)wood_deep;
    float sx = w / 260.0f;
    float sy = h / 120.0f;

    float xs[5] = { 20, 204, 190, 204, 20 };
    float ys[5] = { 20, 20, 60, 100, 100 };
    float px[5], py[5];
    for (int i = 0; i < 5; i++) { px[i] = x + xs[i] * sx; py[i] = y + ys[i] * sy; }

    ui_draw_poly(ui, px, py, 5, paper);
    for (int i = 0; i < 5; i++) {
        int j = (i + 1) % 5;
        ui_draw_line(ui, px[i], py[i], px[j], py[j], 1.2f, accent);
    }
    float lw = qlcg_maxf(1.0f, 2.0f * sy);
    ui_draw_line(ui, x + 204.0f * sx, y + 60.0f * sy, x + 236.0f * sx, y + 60.0f * sy, lw, accent);
    ui_draw_circle(ui, x + 236.0f * sx, y + 60.0f * sy, 6.0f * sy, accent);
}

/* 空状态示意：开口朝下的虚线半圆 */
void ui_empty_arc(UiContext *ui, float cx, float cy, float r, uint32_t color) {
    const int segs = 12;
    for (int i = 0; i < segs; i += 2) {
        float a0 = 3.14159265f + (float)i / (float)segs * 3.14159265f;
        float a1 = 3.14159265f + (float)(i + 1) / (float)segs * 3.14159265f;
        ui_draw_line(ui, cx + cosf(a0) * r, cy + sinf(a0) * r,
                     cx + cosf(a1) * r, cy + sinf(a1) * r, 1.6f, color);
    }
}

/* 下拉选择器：收起态绘制与触发 */
/* 展开中的下拉弹层矩形；未展开返回 false */
static bool select_popup_rect(const UiContext *ui, float *ox, float *oy, float *ow, float *oh) {
    if (ui->select_id == 0 || ui->select_count <= 0) return false;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float px = 12.5f * sc;
    float row_h = qlcg_maxf(28.0f * z, px * 1.9f);
    float ph = row_h * (float)ui->select_count;
    float x = ui->select_x, y = ui->select_y, w = ui->select_w;
    if (y + ph > (float)ui->win_h - 6.0f * z)
        y = qlcg_maxf(6.0f * z, (float)ui->win_h - ph - 6.0f * z);
    *ox = x; *oy = y; *ow = w; *oh = ph;
    return true;
}

bool ui_select_sized(UiContext *ui, uint32_t id, float x, float y, float w, float h,
                     const char *const *items, int count, int current, float px) {
    float z = ui->met.size;
    (void)0;
    h = ui_select_height(ui, h);

    bool open = (ui->select_id == id);
    bool saved_scope = ui->select_input;
    ui->select_input = open;
    bool hov = ui_hit(ui, x, y, w, h);
    ui->select_input = saved_scope;

    open = qlcg_dev_select_open(ui, id, open);
    if (open) {
        ui->select_x = x;
        ui->select_y = y + h + 2.0f * z;
        ui->select_w = w;
        ui->select_count = (count > 12) ? 12 : count;
        ui->select_current = current;
        for (int i = 0; i < ui->select_count; i++) {
            snprintf(ui->select_labels[i], 64, "%s", items[i]);
        }
        float gx, gy, gw, gh;
        if (select_popup_rect(ui, &gx, &gy, &gw, &gh) &&
            ui_pointer_in_rect(ui, gx, gy, gw, gh)) hov = false;
    }

    ui_draw_rect(ui, x, y, w, h, tk_of(ui, QL_SURFACE));
    ui_draw_border(ui, x, y, w, h, 1.0f,
                   (hov || open) ? tk_of(ui, QL_LINE_PRIMARY) : tk_of(ui, QL_LINE_SOFT));

    const char *label = (current >= 0 && current < count) ? items[current] : "";
    ui_draw_text_ellipsis(ui, UI_FACE_BODY, px, false, x + 10.0f * z,
                          y + (h - px * 1.35f) * 0.5f, qlcg_maxf(8.0f, w - 30.0f * z), 0,
                          label, tk_of(ui, QL_INK));

    /* 右侧箭头：展开时朝上 */
    float ax = x + w - 16.0f * z;
    float ay = y + h * 0.5f;
    float ar = 4.0f * z;
    if (open) {
        ui_draw_line(ui, ax - ar, ay + ar * 0.5f, ax, ay - ar * 0.5f, 1.4f, tk_of(ui, QL_INK_2));
        ui_draw_line(ui, ax, ay - ar * 0.5f, ax + ar, ay + ar * 0.5f, 1.4f, tk_of(ui, QL_INK_2));
    } else {
        ui_draw_line(ui, ax - ar, ay - ar * 0.5f, ax, ay + ar * 0.5f, 1.4f, tk_of(ui, QL_INK_2));
        ui_draw_line(ui, ax, ay + ar * 0.5f, ax + ar, ay - ar * 0.5f, 1.4f, tk_of(ui, QL_INK_2));
    }

    if (hov && ui->mouse_clicked) {
        ui->capture_action = -1;
        ui->input_id = 0;
        ui->drag_slider_id = -1;
        if (open) {
            ui->select_id = 0;
        } else {
            ui->select_id = id;
            ui->select_frame = ui->frame_idx;
            ui->select_x = x;
            ui->select_y = y + h + 2.0f * z;
            ui->select_w = w;
            ui->select_count = (count > 12) ? 12 : count;
            ui->select_current = current;
            for (int i = 0; i < ui->select_count; i++) {
                snprintf(ui->select_labels[i], 64, "%s", items[i]);
            }
        }
        ui_pointer_consume(ui);
        return true;
    }
    return false;
}

/* 表单默认字号同 base .input：12.5px */
bool ui_select(UiContext *ui, uint32_t id, float x, float y, float w, float h,
               const char *const *items, int count, int current) {
    return ui_select_sized(ui, id, x, y, w, h, items, count, current,
                           12.5f * ui->met.scale);
}

/* 页面末尾画展开浮层并返回选中索引 */
int ui_select_popup(UiContext *ui) {
    if (ui->select_id == 0) return -1;
    float x, y, w, ph;
    if (!select_popup_rect(ui, &x, &y, &w, &ph)) return -1;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float px = 12.5f * sc;
    float row_h = qlcg_maxf(28.0f * z, px * 1.9f);

    bool saved_scope = ui->select_input;
    ui->select_input = true;
    bool enabled = !ui_pointer_blocked(ui);
    bool hov = ui_hit(ui, x, y, w, ph);
    if (enabled && ui->frame_idx > ui->select_frame && ui->mouse_clicked && !hov) {
        ui->select_id = 0;
        ui_pointer_consume(ui);
        ui->select_input = saved_scope;
        return -1;
    }

    ui_panel(ui, x, y, w, ph, 8.0f * z, tk_of(ui, QL_SURFACE_HI),
             tk_of(ui, QL_LINE_SOFT), tk_of(ui, QL_SHADOW));

    int result = -1;
    for (int i = 0; i < ui->select_count; i++) {
        float ry = y + (float)i * row_h;
        bool rh = ui_hit(ui, x, ry, w, row_h);
        if (i == ui->select_current) {
            ui_draw_rect(ui, x + 2.0f * z, ry, w - 4.0f * z, row_h,
                         with_alpha(tk_of(ui, QL_PRIMARY), 0.12f));
        } else if (rh) {
            ui_draw_rect(ui, x + 2.0f * z, ry, w - 4.0f * z, row_h,
                         with_alpha(tk_of(ui, QL_PRIMARY), 0.06f));
        }
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, px, false, x + 10.0f * z,
                              ry + (row_h - px * 1.35f) * 0.5f, qlcg_maxf(8.0f, w - 20.0f * z), 0,
                              ui->select_labels[i], tk_of(ui, QL_INK));
        if (rh && ui->mouse_clicked && ui->frame_idx > ui->select_frame) {
            result = i;
        }
    }
    if (result >= 0) {
        ui->select_result_id = ui->select_id;
        ui->select_id = 0;
        ui_pointer_consume(ui);
    }
    ui->select_input = saved_scope;
    return result;
}

/* 身高→显示单位；通用控件只认显示值 */
static float height_scale(MeasurementSystem sys) {
    if (sys == MEAS_MING) return 32.0f;
    if (sys == MEAS_IMPERIAL) return 2.54f;
    return 1.0f;
}

static int height_decimals(MeasurementSystem sys) {
    if (sys == MEAS_MING) return 2;
    if (sys == MEAS_IMPERIAL) return 1;
    return 0;
}

static int height_step_cm(MeasurementSystem sys) {
    if (sys == MEAS_MING) return 3;
    if (sys == MEAS_IMPERIAL) return 2;
    return 1;
}

/* 身高数字输入框：复用通用浮点数字控件 */
bool ui_height_input(UiContext *ui, uint32_t id, float x, float y, float w, float h,
                     MeasurementSystem sys, int *height_cm, int min_cm, int max_cm) {
    float scale = height_scale(sys);
    float value = (float)*height_cm / scale;
    char hint[32] = {0};
    if (sys != MEAS_METRIC) snprintf(hint, sizeof(hint), "≈ %d cm", *height_cm);
    bool changed = ui_number_input(ui, id, x, y, w, h,
                                   &value,
                                   (float)min_cm / scale,
                                   (float)max_cm / scale,
                                   (float)height_step_cm(sys) / scale,
                                   height_decimals(sys), hint[0] ? hint : NULL);
    if (!changed) return false;
    int cm = qlcg_clampi((int)roundf(value * scale), min_cm, max_cm);
    if (cm == *height_cm) return false;
    *height_cm = cm;
    return true;
}

static void fmt_number_val(float v, int decimals, char *buf, size_t cap) {
    if (decimals <= 0) snprintf(buf, cap, "%.0f", v);
    else if (decimals == 1) snprintf(buf, cap, "%.1f", v);
    else if (decimals == 2) snprintf(buf, cap, "%.2f", v);
    else snprintf(buf, cap, "%.3f", v);
}

/* 浮点数字输入：键盘+步进，带范围与后缀 */
bool ui_number_input(UiContext *ui, uint32_t id, float x, float y, float w, float h,
                     float *value, float min_value, float max_value, float step,
                     int decimals, const char *suffix) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float px = 13.0f * sc;
    h = ui_ctl_height(h, px);
    decimals = qlcg_clampi(decimals, 0, 3);

    float step_w = qlcg_clampf(h * 0.90f, 24.0f * z, 42.0f * z);
    float field_w = qlcg_maxf(40.0f, w - step_w * 2.0f - 4.0f * z);
    bool hov_field = ui_hit(ui, x, y, field_w, h);
    bool focused   = (ui->input_id == id);
    bool changed   = false;
    const char *sfx = (suffix && suffix[0]) ? suffix : "";

    float sub_px = 10.5f * sc;
    float sub_w = sfx[0] ? ((float)ui_text_width_px(ui, UI_FACE_MONO, sub_px, false, sfx) + 12.0f * z) : 0.0f;
    if (sub_w > 0.0f && field_w < sub_w + 48.0f * z) sub_w = 0.0f;

    float ftx = x + 10.0f * z;
    float ftw = qlcg_maxf(12.0f, field_w - sub_w - 16.0f * z);
    UiEditField ef = { ui->input_buf, sizeof(ui->input_buf), 0, &ui->edit_field };

    if (focused && ui->input_sys != -1) {
        fmt_number_val(*value, decimals, ui->input_buf, sizeof(ui->input_buf));
        ui->input_sys   = -1;
        ui->input_dirty = false;
        ui_edit_end(&ef, false);
    }

    if (hov_field && ui->mouse_clicked && !focused) {
        ui->input_id          = id;
        ui->input_focus_frame = ui->frame_idx;
        ui->input_sys         = -1;
        ui->input_mode        = UI_INPUT_NUM;
        ui->input_max_bytes   = 0;
        ui->input_max_chars   = 0;
        ui->input_dirty       = false;
        ui->input_commit      = false;
        fmt_number_val(*value, decimals, ui->input_buf, sizeof(ui->input_buf));
        ui_edit_reset(&ef);
        ui_edit_end(&ef, false);
        focused = true;
    }

    if (focused) {
        ui->input_seen_frame = ui->frame_idx;
        if (hov_field && ui->mouse_clicked) {
            ui->edit_field.caret = ui_edit_hit_line(ui, &ef, ftw, UI_FACE_MONO, px, false,
                                                    (float)ui->mouse_x - ftx);
            ui->edit_field.anchor = ui->edit_field.caret;
        }
        if (ui->input_dirty && ui->input_buf[0] != '\0') {
            float v = qlcg_clampf((float)atof(ui->input_buf), min_value, max_value);
            if (fabsf(v - *value) > 0.0001f) {
                *value = v;
                changed = true;
            }
        }
        bool click_outside = (!ui_pointer_blocked(ui) && ui->frame_idx > ui->input_focus_frame &&
                              ui->mouse_clicked && !hov_field);
        if (ui->input_commit || click_outside) {
            if (ui->input_dirty && ui->input_buf[0] != '\0') {
                float v = qlcg_clampf((float)atof(ui->input_buf), min_value, max_value);
                if (fabsf(v - *value) > 0.0001f) {
                    *value = v;
                    changed = true;
                }
            }
            ui->input_id     = 0;
            ui->input_dirty  = false;
            ui->input_commit = false;
            focused = false;
        }
    }

    ui_draw_rect(ui, x, y, field_w, h, tk_of(ui, focused ? QL_SURFACE_HI : QL_BG_DEEP));
    ui_draw_border(ui, x, y, field_w, h, focused ? 1.5f : 1.0f,
                   (focused || hov_field) ? tk_of(ui, QL_LINE_PRIMARY) : tk_of(ui, QL_LINE_SOFT));

    if (sub_w > 0.0f) {
        ui_draw_text_px_right(ui, UI_FACE_MONO, sub_px, false, x + field_w - 8.0f * z,
                              y + (h - sub_px * 1.30f) * 0.5f, sfx, tk_of(ui, QL_INK_3));
    }

    char disp[80];
    if (!focused) fmt_number_val(*value, decimals, disp, sizeof(disp));
    if (focused) {
        uint32_t col_sel = (tk_of(ui, QL_PRIMARY_UI) & 0x00FFFFFFu) | (0x38u << 24);
        ui_edit_draw_line(ui, &ef, ftx, y, ftw, h, UI_FACE_MONO, px, false, true, true,
                          tk_of(ui, QL_INK), tk_of(ui, QL_INK_3), col_sel,
                          tk_of(ui, QL_PRIMARY_TEXT), NULL);
    } else {
        UiEdit idle = {0};
        UiEditField v = { disp, sizeof(disp), 0, &idle };
        ui_edit_draw_line(ui, &v, ftx, y, ftw, h, UI_FACE_MONO, px, false, false, false,
                          tk_of(ui, QL_INK), tk_of(ui, QL_INK_3), 0, 0, NULL);
    }

    float bx_minus = x + field_w + 2.0f * z;
    float bx_plus  = bx_minus + step_w + 2.0f * z;
    float step_px = qlcg_minf(12.0f * sc, step_w * 0.52f);
    if (ui_btn(ui, bx_minus, y, step_w, h, tr("ui.minus"), BTN_GHOST, false, step_px, false)) {
        float next = qlcg_clampf(*value - step, min_value, max_value);
        if (fabsf(next - *value) > 0.0001f) { *value = next; changed = true; }
        if (focused) { fmt_number_val(*value, decimals, ui->input_buf, sizeof(ui->input_buf)); ui_edit_end(&ef, false); }
    }
    if (ui_btn(ui, bx_plus, y, step_w, h, tr("ui.plus"), BTN_GHOST, false, step_px, false)) {
        float next = qlcg_clampf(*value + step, min_value, max_value);
        if (fabsf(next - *value) > 0.0001f) { *value = next; changed = true; }
        if (focused) { fmt_number_val(*value, decimals, ui->input_buf, sizeof(ui->input_buf)); ui_edit_end(&ef, false); }
    }
    return changed;
}

/* 文本输入框：点击定位光标，Enter/点外提交 */
bool ui_text_input(UiContext *ui, uint32_t id, float x, float y, float w, float h,
                   char *buf, size_t cap, uint32_t max_chars, const char *placeholder) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float px = 13.0f * sc;
    h = ui_ctl_height(h, px);
    bool hov     = ui_hit(ui, x, y, w, h);
    bool focused = (ui->input_id == id);
    bool changed = false;

    size_t byte_cap = (cap > 1) ? (cap - 1) : 0;
    if (byte_cap > sizeof(ui->input_buf) - 1) byte_cap = sizeof(ui->input_buf) - 1;

    float tx = x + 10.0f * z;
    float tw = qlcg_maxf(12.0f, w - 20.0f * z);

    UiEditField f = { ui->input_buf, byte_cap + 1, max_chars, &ui->edit_field };
    QLCG_DEV_ONLY({
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            QLCG_DEV_INPUT_LOG("[in] field id %u rect %.0f,%.0f %.0fx%.0f win %dx%d\n",
                   id, x, y, w, h, ui->win_w, ui->win_h);
        }
    })

    if (hov && ui->mouse_clicked && !focused) {
        ui->input_id          = id;
        ui->input_focus_frame = ui->frame_idx;
        ui->input_mode        = UI_INPUT_TEXT;
        ui->input_max_bytes   = byte_cap;
        ui->input_max_chars   = max_chars;
        ui->input_utf16_hi    = 0;
        ui->input_dirty       = false;
        ui->input_commit      = false;
        ui_u8_copy(ui->input_buf, sizeof(ui->input_buf), buf);
        ui_edit_reset(&f);
        ui_edit_end(&f, false);
        QLCG_DEV_ONLY(QLCG_DEV_INPUT_LOG("[in] focus id %u cap %u len %u\n",
                   id, (unsigned)byte_cap, (unsigned)strlen(ui->input_buf));)
        focused = true;
    }

    if (focused) {
        ui->input_seen_frame = ui->frame_idx;
        /* 鼠标：点选定位光标、拖动扩选、双击全选 */
        if (hov && (ui->mouse_dblclicked || ui->mouse_clicked)) {
            if (ui->mouse_dblclicked) {
                ui_edit_select_all(&f);
            } else {
                ui->edit_field.caret = ui_edit_hit_line(ui, &f, tw, UI_FACE_BODY, px, false,
                                                        (float)ui->mouse_x - tx);
                ui->edit_field.anchor   = ui->edit_field.caret;
                ui->edit_field.dragging = true;
            }
        } else if (ui->mouse_down && ui->edit_field.dragging) {
            ui->edit_field.caret = ui_edit_hit_line(ui, &f, tw, UI_FACE_BODY, px, false,
                                                    (float)ui->mouse_x - tx);
        }
        if (!ui->mouse_down) ui->edit_field.dragging = false;

        if (ui->input_dirty) {
            if (strcmp(buf, ui->input_buf) != 0) {
                ui_u8_copy(buf, cap, ui->input_buf);
                changed = true;
            }
            ui->input_dirty = false;
        }
        bool click_outside = (!ui_pointer_blocked(ui) && ui->frame_idx > ui->input_focus_frame &&
                              ui->mouse_clicked && !hov);
        if (ui->input_commit || click_outside) {
            ui->input_id       = 0;
            ui->input_dirty    = false;
            ui->input_commit   = false;
            ui->input_utf16_hi = 0;
            focused = false;
        }
    }

    /* .input：inset 内凹 + surface 底 */
    ui_draw_rect(ui, x, y, w, h, tk_of(ui, focused ? QL_SURFACE_HI : QL_SURFACE));
    ui_neu_in(ui, x, y, w, h, 4.0f * z, tk_of(ui, QL_NEU_DARK), tk_of(ui, QL_NEU_LIGHT));
    ui_draw_border(ui, x, y, w, h, 1.0f,
                   focused ? tk_of(ui, QL_PRIMARY_TEXT)
                           : (hov ? tk_of(ui, QL_LINE_SOFT) : tk_of(ui, QL_LINE_FAINT)));

    uint32_t col_sel = (tk_of(ui, QL_PRIMARY_UI) & 0x00FFFFFFu) | (0x38u << 24);
    if (focused) {
        ui_edit_draw_line(ui, &f, tx, y, tw, h, UI_FACE_BODY, px, false, true, true,
                          tk_of(ui, QL_INK), tk_of(ui, QL_INK_3), col_sel,
                          tk_of(ui, QL_PRIMARY_TEXT), placeholder);
    } else {
        UiEdit idle = {0};
        UiEditField v = { buf, cap, 0, &idle };
        ui_edit_draw_line(ui, &v, tx, y, tw, h, UI_FACE_BODY, px, false, false, false,
                          tk_of(ui, QL_INK), tk_of(ui, QL_INK_3), 0, 0, placeholder);
    }
    return changed;
}

/* 虚线边框：逐段短矩形，四边同相位 */
void ui_draw_dashed_border(UiContext *ui, float x, float y, float w, float h,
                           float thick, float dash, float gap, uint32_t color) {
    if (thick <= 0.0f) thick = 1.0f;
    if (dash  <= 0.5f) dash  = 4.0f * ui->met.size;
    if (gap   <= 0.5f) gap   = 4.0f * ui->met.size;
    float step = dash + gap;

    for (float px = x; px < x + w; px += step) {
        float len = qlcg_minf(dash, x + w - px);
        if (len <= 0.5f) continue;
        ui_draw_rect(ui, px, y, len, thick, color);
        ui_draw_rect(ui, px, y + h - thick, len, thick, color);
    }
    for (float py = y; py < y + h; py += step) {
        float len = qlcg_minf(dash, y + h - py);
        if (len <= 0.5f) continue;
        ui_draw_rect(ui, x, py, thick, len, color);
        ui_draw_rect(ui, x + w - thick, py, thick, len, color);
    }
}
