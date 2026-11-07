/* 通用滚动区：裁剪 + 滚轮 + 指示条 */
#include "ui_scroll.h"
#include "ui_widgets.h"
#include "../core/math_util.h"

static UiScrollSlot g_slots[UI_SCROLL_SLOTS];

static UiScrollSlot *slot_of(UiContext *ui, uint32_t id) {
    (void)ui;
    for (int i = 0; i < UI_SCROLL_SLOTS; i++) {
        if (g_slots[i].id == id) return &g_slots[i];
    }
    for (int i = 0; i < UI_SCROLL_SLOTS; i++) {
        if (g_slots[i].id == 0) {
            g_slots[i].id = id;
            g_slots[i].off = 0.0f;
            g_slots[i].content_h = 0.0f;
            return &g_slots[i];
        }
    }
    return NULL;
}

float ui_scroll_begin(UiContext *ui, uint32_t id, float x, float y, float w, float h) {
    UiScrollSlot *s = slot_of(ui, id);
    if (!s) return 0.0f;

    s->x = x; s->y = y; s->w = w; s->h = h;

    float max_off = qlcg_maxf(s->content_h - h, 0.0f);
    if (max_off > 0.5f && ui_hit(ui, x, y, w, h) && ui->wheel_delta != 0) {
        s->off -= (float)ui->wheel_delta / 120.0f * 48.0f * ui->met.size;
    }
    s->off = qlcg_clampf(s->off, 0.0f, max_off);

    ui_clip_push(ui, x, y, w, h);
    return s->off;
}

void ui_scroll_end(UiContext *ui, uint32_t id, float content_h) {
    UiScrollSlot *s = slot_of(ui, id);
    ui_clip_pop(ui);
    if (!s) return;

    s->content_h = content_h;
    float view_h = s->h;
    if (view_h < 1.0f) return;
    float max_off = qlcg_maxf(content_h - view_h, 0.0f);
    s->off = qlcg_clampf(s->off, 0.0f, max_off);
    if (max_off <= 0.5f) return;

    /* 右缘指示条 */
    float z = ui->met.size;
    float bar_w = 3.0f * z;
    float bar_x = s->x + s->w - bar_w - 3.0f * z;
    float thumb_h = qlcg_maxf(view_h * (view_h / content_h), 18.0f * z);
    float thumb_y = s->y + (view_h - thumb_h) * (s->off / max_off);
    ui_draw_rect(ui, bar_x, s->y, bar_w, view_h, with_alpha(tk_of(ui, QL_LINE), 0.22f));
    ui_draw_rect(ui, bar_x, thumb_y, bar_w, thumb_h, with_alpha(tk_of(ui, QL_PRIMARY_UI), 0.55f));
}

void ui_scroll_reset(UiContext *ui, uint32_t id) {
    UiScrollSlot *s = slot_of(ui, id);
    if (s) s->off = 0.0f;
}

float ui_scroll_offset(UiContext *ui, uint32_t id) {
    UiScrollSlot *s = slot_of(ui, id);
    return s ? s->off : 0.0f;
}
