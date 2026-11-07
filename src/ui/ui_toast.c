/* 右上堆叠通知：语义色竖条、图标、标题正文与倒计时线 */
#include "ui_toast.h"
#include "ui_sprite.h"
#include "ui_widgets.h"
#include "../core/i18n.h"
#include <stdio.h>
#include <string.h>

/* 尺寸按 met.size，字号按 met.scale */
#define TS_PANEL_W   300.0f
#define TS_MARGIN_R   20.0f
#define TS_MARGIN_T   76.0f
#define TS_GAP        10.0f
#define TS_PAD_T      12.0f
#define TS_PAD_X      14.0f
#define TS_PAD_B      14.0f
#define TS_ICON        22.0f
#define TS_ICON_GAP     12.0f
#define TS_BAR           3.0f
#define TS_PROG          2.0f
#define TS_TITLE_PX     13.0f
#define TS_MSG_PX       12.0f
#define TS_IN_SEC      0.48f
#define TS_OUT_SEC     0.34f
#define TS_DRAG_RATIO  0.40f

/* 语义色：夜间提亮，保证在深色玻璃上可读 */
static uint32_t ts_tone(const UiContext *ui, int kind) {
    QlColor base;
    switch (kind) {
        case UI_TOAST_SUCCESS: base = ql_rgb(0x5F8A55); break;
        case UI_TOAST_WARNING: base = ql_rgb(0xC48A2A); break;
        case UI_TOAST_ERROR:   base = ql_rgb(0xA61B29); break;
        default:               base = ql_rgb(0x4D4030); break;
    }
    if (ui->tk.dark) base = ql_mix(base, 0.70f, ql_rgb(0xFBECDE), 0.30f);
    base.a = 1.0f;
    return (uint32_t)ql_to_rgba(base);
}

/* 按 fade 乘原 alpha，保留令牌自带透明度 */
static uint32_t ts_fade(uint32_t c, float fade) {
    uint32_t a = (c >> 24) & 0xFFu;
    float af = ((float)a / 255.0f) * fade;
    uint32_t av = (uint32_t)(qlcg_clampf(af, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (c & 0x00FFFFFFu) | (av << 24);
}

static const char *ts_icon_name(int kind) {
    switch (kind) {
        case UI_TOAST_SUCCESS: return "toast-success";
        case UI_TOAST_WARNING: return "toast-warning";
        case UI_TOAST_ERROR:   return "toast-error";
        default:               return "toast-info";
    }
}

static const char *ts_title_key(int kind) {
    switch (kind) {
        case UI_TOAST_SUCCESS: return "toast.success.title";
        case UI_TOAST_WARNING: return "toast.warning.title";
        case UI_TOAST_ERROR:   return "toast.error.title";
        default:               return "toast.info.title";
    }
}

void ui_toast_push(UiToastQueue *q, int kind, const char *title, const char *msg, float sec) {
    if (!q || !msg || !msg[0]) return;
    q->layout_frame = 0;

    /* 同类型同文案重复推送时只重置计时，避免连点刷屏 */
    for (int i = 0; i < UI_TOAST_SLOTS; i++) {
        UiToast *t = &q->item[i];
        if (t->used && t->kind == kind && strcmp(t->msg, msg) == 0) {
            t->t = 0.0f;
            t->hold = false;
            t->dragging = false;
            t->drag = 0.0f;
            return;
        }
    }

    for (int i = UI_TOAST_SLOTS - 1; i > 0; i--) {
        q->item[i] = q->item[i - 1];
    }
    UiToast *t = &q->item[0];
    memset(t, 0, sizeof(*t));
    t->used  = true;
    t->kind  = kind;
    t->life  = (sec > 0.5f) ? sec : 3.2f;
    snprintf(t->msg, sizeof(t->msg), "%s", msg);
    snprintf(t->title, sizeof(t->title), "%s", (title && title[0]) ? title : tr(ts_title_key(kind)));
}

void ui_toast_update(UiToastQueue *q, float dt) {
    if (!q) return;
    q->layout_frame = 0;
    for (int i = 0; i < UI_TOAST_SLOTS; i++) {
        UiToast *t = &q->item[i];
        if (!t->used) continue;
        /* 入场期不因悬停暂停，否则卡在透明首帧 */
        if (t->t >= TS_IN_SEC && (t->hold || t->dragging)) continue;
        t->t += dt;
        if (t->t < t->life) continue;
        /* 移除并把后面的往前挤，保持「新的在上」 */
        for (int k = i; k < UI_TOAST_SLOTS - 1; k++) q->item[k] = q->item[k + 1];
        memset(&q->item[UI_TOAST_SLOTS - 1], 0, sizeof(UiToast));
        i--;
    }
}

/* 指针位置：鼠标优先，鼠标不在时用触摸点 */
static bool ts_pointer(UiContext *ui, float *px, float *py, bool *down) {
    if (ui->touch_active) {
        *px = ui->touch_x;
        *py = ui->touch_y;
        *down = true;
        return true;
    }
    *px = (float)ui->mouse_x;
    *py = (float)ui->mouse_y;
    *down = ui->mouse_down;
    return true;
}

static void ts_layout(UiContext *ui, UiToastQueue *q) {
    if (q->layout_frame == ui->frame_idx && q->layout_frame != 0) return;
    float z = ui->met.size > 0 ? ui->met.size : 1.0f;
    float sc = ui->met.scale > 0 ? ui->met.scale : 1.0f;
    float w = qlcg_minf(TS_PANEL_W * z, (float)ui->win_w - 2 * TS_MARGIN_R * z);
    float text_w = qlcg_maxf(w - (2 * TS_PAD_X + TS_ICON + TS_ICON_GAP) * z, 40 * z);
    float title_lh = ui_line_height(UI_FACE_BODY, TS_TITLE_PX * sc);
    float msg_lh = ui_line_height(UI_FACE_BODY, TS_MSG_PX * sc);
    float y = TS_MARGIN_T * z;
    bool clipped = false;
    for (int i = 0; i < UI_TOAST_SLOTS; i++) {
        UiToast *t = &q->item[i];
        t->hit_w = t->hit_h = 0;
        if (!t->used || clipped || w < 120 * z) continue;
        int lines = ui_measure_text_wrapped(ui, UI_FACE_BODY, TS_MSG_PX * sc, false, text_w, t->msg);
        float h = (TS_PAD_T + TS_PAD_B) * z +
                  qlcg_maxf(title_lh + msg_lh * (float)(lines > 0 ? lines : 1), TS_ICON * z);
        if (y + h > (float)ui->win_h - 8 * z) {
            clipped = true;
            continue;
        }
        t->hit_x = (float)ui->win_w - TS_MARGIN_R * z - w;
        t->hit_y = y;
        t->hit_w = w;
        t->hit_h = h;
        y += h + TS_GAP * z;
    }
    q->layout_frame = ui->frame_idx;
}

bool ui_toast_over_ui(UiContext *ui, UiToastQueue *q) {
    if (!q) return false;
    ts_layout(ui, q);
    float x, y;
    bool down;
    ts_pointer(ui, &x, &y, &down);
    for (int i = 0; i < UI_TOAST_SLOTS; i++) {
        const UiToast *t = &q->item[i];
        if (t->used && (t->dragging ||
            (t->hit_w > 0 && x >= t->hit_x && x < t->hit_x + t->hit_w &&
             y >= t->hit_y && y < t->hit_y + t->hit_h))) return true;
    }
    return false;
}

void ui_toast_render(UiContext *ui, UiToastQueue *q) {
    if (!q) return;
    ts_layout(ui, q);
    float z  = (ui->met.size  > 0.0f) ? ui->met.size  : 1.0f;
    float sc = (ui->met.scale > 0.0f) ? ui->met.scale : 1.0f;
    float win_w = (float)ui->win_w;

    float panel_w = TS_PANEL_W * z;
    float max_w = win_w - 2.0f * TS_MARGIN_R * z;
    if (panel_w > max_w) panel_w = max_w;
    if (panel_w < 120.0f * z) return;

    uint32_t c_glass = tk_of(ui, QL_SURFACE_HI);
    uint32_t c_line  = tk_of(ui, QL_LINE_SOFT);
    uint32_t c_ink   = tk_of(ui, QL_INK);
    uint32_t c_ink2  = tk_of(ui, QL_INK_2);
    uint32_t c_hi    = with_alpha(0xFFFFFFFFu, 0.42f);

    float title_px = TS_TITLE_PX * sc;
    float msg_px   = TS_MSG_PX * sc;
    float title_lh = ui_line_height(UI_FACE_BODY, title_px);
    float msg_lh   = ui_line_height(UI_FACE_BODY, msg_px);
    float icon_sz  = TS_ICON * z;
    float pad_x    = TS_PAD_X * z;
    float pad_t    = TS_PAD_T * z;
    float gap      = TS_ICON_GAP * z;
    float text_x_off = pad_x + icon_sz + gap;
    float text_w  = panel_w - text_x_off - pad_x;
    if (text_w < 40.0f * z) text_w = 40.0f * z;

    float bar_h = TS_PROG * z;
    if (bar_h < 1.0f) bar_h = 1.0f;

    float x_base = win_w - TS_MARGIN_R * z - panel_w;
    float y = TS_MARGIN_T * z;

    float px = 0.0f, py = 0.0f;
    bool pdown = false;
    ts_pointer(ui, &px, &py, &pdown);

    for (int i = 0; i < UI_TOAST_SLOTS; i++) {
        UiToast *t = &q->item[i];
        if (!t->used || t->hit_h <= 0) continue;
        float h = t->hit_h;
        y = t->hit_y;

        /* 命中区固定在未位移的矩形上，入场动画期间也能悬停 */
        bool hover = (px >= x_base && px <= x_base + panel_w &&
                      py >= y && py <= y + h);

        /* 拖动与回弹 */
        if (t->dragging) {
            if (!pdown) {
                if (t->drag > TS_DRAG_RATIO * panel_w) {
                    t->t = t->life - TS_OUT_SEC;
                } else {
                    t->drag = 0.0f;
                }
                t->dragging = false;
            } else {
                t->drag = px - t->drag_x0;
                if (t->drag < 0.0f) t->drag = 0.0f;
                if (t->drag > TS_DRAG_RATIO * panel_w) t->t = t->life - TS_OUT_SEC;
            }
        } else if (pdown && hover) {
            t->dragging = true;
            t->drag_x0  = px;
            t->drag     = 0.0f;
        } else if (t->drag > 0.0f) {
            t->drag -= panel_w * 6.0f * ui->dt;
            if (t->drag < 0.0f) t->drag = 0.0f;
        }

        /* 出场期不受悬停牵制，否则划走后卡在屏上 */
        t->hold = hover && !t->dragging && (t->t < t->life - TS_OUT_SEC);

        /* 入场与出场位移 */
        float dx = 0.0f, fade = 1.0f;
        if (!ui->reduce_motion) {
            if (t->t < TS_IN_SEC) {
                float e = ui_ease_out(t->t / TS_IN_SEC);
                dx   = (1.0f - e) * panel_w * 1.05f;
                fade = qlcg_clampf(t->t / (TS_IN_SEC * 0.7f), 0.0f, 1.0f);
            } else if (t->t > t->life - TS_OUT_SEC) {
                float p = qlcg_clampf((t->t - (t->life - TS_OUT_SEC)) / TS_OUT_SEC, 0.0f, 1.0f);
                dx   = p * p * panel_w * 0.6f;
                fade = 1.0f - p;
            }
        }
        dx += t->drag;

        float x = x_base + dx;
        uint32_t tone = ts_tone(ui, t->kind);

        /* 底板半透明玻璃，不叠阴影，否则透出压暗 */
        ui_draw_rect_glass(ui, x, y, panel_w, h, ts_fade(c_glass, 0.78f * fade));
        ui_draw_border(ui, x, y, panel_w, h, 1.0f * z, ts_fade(c_line, fade));
        ui_draw_rect(ui, x + 1.0f * z, y + 1.0f * z,
                     panel_w - 2.0f * z, 1.0f * z, ts_fade(c_hi, fade));

        /* 左侧语义竖条 */
        ui_draw_rect(ui, x, y, TS_BAR * z, h, ts_fade(tone, fade));

        /* 图标：L0 淡底 / L1 语义色 / L2 空 / L3 原色不染 */
        const SpriteDef *sp = ui_sprite_by_name(ts_icon_name(t->kind));
        if (sp) {
            float ix = x + pad_x;
            float iy = y + pad_t + 1.0f * z;
            ui_sprite_layer(ui, sp, 0, ix, iy, icon_sz, icon_sz, ts_fade(tone, fade));
            ui_sprite_layer(ui, sp, 1, ix, iy, icon_sz, icon_sz, ts_fade(tone, fade));
            ui_sprite_layer(ui, sp, 3, ix, iy, icon_sz, icon_sz,
                            ts_fade(0xFFFFFFFFu, fade));
        }

        /* 标题与正文 */
        float tx = x + text_x_off;
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, title_px, true, tx, y + pad_t,
                              text_w, 0, t->title, ts_fade(c_ink, fade));
        ui_draw_text_wrapped(ui, UI_FACE_BODY, msg_px, false, tx,
                             y + pad_t + title_lh, text_w, msg_lh,
                             t->msg, ts_fade(c_ink2, fade));

        /* 底部倒计时线：左端固定，右端按剩余时间收缩 */
        float left = 1.0f - qlcg_clampf(t->t / t->life, 0.0f, 1.0f);
        float prog_w = (panel_w - 2.0f * z) * left;
        if (prog_w > 0.5f) {
            ui_draw_rect(ui, x + 1.0f * z, y + h - bar_h, prog_w, bar_h,
                         ts_fade(tone, fade));
        }

        y += h + TS_GAP * z;
    }
}
