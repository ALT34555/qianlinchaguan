/* HUD玻璃：取 SURFACE_GLASS 令牌 */
#include "hud_glass.h"

/* 顶栏通栏：外投影 + 玻璃底 + 白高光 + 顶亮线 + 底分隔 */
void hud_glass_strip(UiContext *ui, float x, float y, float w, float h) {
    float z = ui->met.size;
    ui_shadow_soft(ui, x, y, w, h, 28.0f * z, 10.0f * z,
                   with_alpha(tk_of(ui, QL_SHADOW_LG), 0.12f));
    ui_draw_rect_glass(ui, x, y, w, h, tk_of(ui, QL_SURFACE_GLASS));
    ui_draw_rect_grad(ui, x, y, w, h,
                      ui_rgba(255, 255, 255, 46), ui_rgba(255, 255, 255, 5));
    ui_draw_rect(ui, x, y, w, 1.0f, tk_of(ui, QL_AERO_LINE));
    ui_draw_rect(ui, x, y + h - 1.0f, w, 1.0f, tk_of(ui, QL_LINE_SOFT));
}

/* 面板：同料，不叠白高光，否则透明度对不齐 */
void hud_glass_panel(UiContext *ui, float x, float y, float w, float h) {
    float z = ui->met.size;
    ui_shadow_soft(ui, x, y, w, h, 28.0f * z, 0.0f,
                   with_alpha(tk_of(ui, QL_SHADOW_LG), 0.12f));
    ui_draw_rect_glass(ui, x, y, w, h, tk_of(ui, QL_SURFACE_GLASS));
    ui_draw_rect(ui, x, y, w, 1.0f, tk_of(ui, QL_LINE_SOFT));
    ui_draw_rect(ui, x + w - 1.0f, y, 1.0f, h, tk_of(ui, QL_LINE_SOFT));
}

/* 自定义前景色：按纸面对比度压深 */
uint32_t hud_ink_name(UiContext *ui, uint32_t hex, uint8_t alpha) {
    QlColor paper = ui->tk.c[QL_SURFACE_GLASS];
    QlColor fixed = ql_readable_on(ql_rgb(hex), &paper, 1, ui->tk.c[QL_INK]);
    return ql_to_rgba(ql_alpha(fixed, (float)alpha / 255.0f));
}
