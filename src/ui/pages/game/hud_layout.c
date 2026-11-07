/* HUD槽位布局：元素按槽位依次堆叠 */
#include "hud_layout.h"

/* 每个槽位一个游标；顶部向下、底部向上 */
static float g_cursor[HUD_SLOT_COUNT];
static bool  g_ready = false;

/* HUD 内边距：18 纵向 / 20 横向，再叠加安全区 */
static void hud_origin(UiContext *ui, float *pad_x, float *pad_y) {
    float z = ui->met.size;
    *pad_x = 20.0f * z + ui->safe_x_px;
    *pad_y = 18.0f * z + ui->safe_y_px;
}

void hud_reset(UiContext *ui) {
    float pad_x, pad_y;
    hud_origin(ui, &pad_x, &pad_y);
    (void)pad_x;
    for (int i = 0; i < HUD_SLOT_COUNT; i++) {
        g_cursor[i] = (i == HUD_BOTTOM_LEFT || i == HUD_BOTTOM_RIGHT || i == HUD_BOTTOM_CENTER)
                    ? ((float)ui->win_h - pad_y)   /* 底部槽从下往上 */
                    : pad_y;                       /* 顶部槽从上往下 */
    }
    g_ready = true;
}

void hud_reserve_top(UiContext *ui, float h) {
    if (!g_ready) hud_reset(ui);
    if (h <= 0.0f) return;
    g_cursor[HUD_TOP_LEFT]  += h + 8.0f * ui->met.size;
    g_cursor[HUD_TOP_RIGHT] += h + 8.0f * ui->met.size;
}

UiRect hud_take(UiContext *ui, int slot, float w, float h) {
    if (!g_ready) hud_reset(ui);
    if (slot < 0 || slot >= HUD_SLOT_COUNT) return (UiRect){0, 0, w, h};

    float z = ui->met.size;
    float pad_x, pad_y;
    hud_origin(ui, &pad_x, &pad_y);
    float gap = 8.0f * z;

    UiRect r = {0, 0, w, h};
    switch (slot) {
        case HUD_TOP_LEFT:
            r.x = pad_x;
            r.y = g_cursor[slot];
            g_cursor[slot] += h + gap;
            break;
        case HUD_TOP_RIGHT:
            r.x = (float)ui->win_w - pad_x - w;
            r.y = g_cursor[slot];
            g_cursor[slot] += h + gap;
            break;
        case HUD_BOTTOM_LEFT:
            r.x = pad_x;
            r.y = g_cursor[slot] - h;
            g_cursor[slot] -= h + gap;
            break;
        case HUD_BOTTOM_RIGHT:
            r.x = (float)ui->win_w - pad_x - w;
            r.y = g_cursor[slot] - h;
            g_cursor[slot] -= h + gap;
            break;
        case HUD_BOTTOM_CENTER:
        default:
            r.x = ((float)ui->win_w - w) * 0.5f;
            r.y = g_cursor[slot] - h;
            g_cursor[slot] -= h + gap;
            break;
    }
    return r;
}
