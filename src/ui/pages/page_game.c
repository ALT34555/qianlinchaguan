/* 游戏内页面骨架：按视图分发并叠加各层 HUD */
#include "app/app_state.h"
#include "game/hud_layout.h"

bool game_scene_over_ui(UiContext *ui) {
    return ui_pointer_blocked(ui) || g_app.esc_paused || g_app.settings_in_drawer ||
           g_app.chat_open || g_app.touch_layout_edit || settings_confirm_active() ||
           game_chat_over_ui(ui) || game_debug_over_ui(ui) ||
           ui_pointer_in_rect(ui, 0, 0, (float)ui->win_w,
                              ui->safe_y_px + game_top_bar_height(ui));
}

void render_screen_in_game(UiContext *ui) {
    if (!g_app.esc_paused || g_app.settings_in_drawer) g_app.manual_slots_open = false;
    bool saved_blocked = ui->input_blocked;
    ui->input_blocked = game_scene_over_ui(ui);
    if (g_app.view_mode == VIEW_MODE_ATLAS) {
        if (game_view_atlas(ui)) {
            ui->input_blocked = saved_blocked;
            return;
        }
    } else {
        game_view_terrain(ui);
    }
    ui->input_blocked = saved_blocked;

    /* 状态层在场景之上、HUD 之下 */
    ui_status_feedback_draw(ui, &g_app.status_feedback);

    hud_reset(ui);
    hud_reserve_top(ui, game_top_bar_height(ui));   /* 通栏顶栏先让位 */
    ui->input_blocked = saved_blocked || g_app.esc_paused || g_app.settings_in_drawer;
    render_debug_overlay_f12(ui);
    ui_touch_draw(&g_app.touch, ui);   /* 虚拟按键压在 HUD 之上 */
    ui->input_blocked = saved_blocked || settings_confirm_active() || g_app.touch_layout_edit;
    game_chat_panel(ui);
    ui->input_blocked = saved_blocked;
    /* 抽屉叠于场景与聊天之上，取代ESC菜单 */
    if (g_app.settings_in_drawer) render_screen_settings(ui);
    else                          game_esc_menu(ui);

    /* 顶栏压在抽屉之上（z 10011）*/
    game_top_bar(ui);
    /* 自动存档图标贴右上角 */
    game_save_indicator(ui);
    /* 菜单钮常驻最上层，展开时点它即关 */
    ui->input_blocked = saved_blocked || settings_confirm_active() || g_app.touch_layout_edit;
    game_menu_button(ui);
    ui->input_blocked = saved_blocked;
    if (ui->select_id == 9101 && g_app.view_mode == VIEW_MODE_ATLAS &&
        !g_app.esc_paused && !g_app.settings_in_drawer) {
        int picked = ui_select_popup(ui);
        if (picked >= 0) qlcg_atlas_set_projection(&g_app.atlas, g_app.world, (PlanetView)picked);
    }
}
