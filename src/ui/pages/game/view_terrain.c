/* 游戏内山川视图：3D 场景与自然工程点选 */
#include "app/app_state.h"

void game_view_terrain(UiContext *ui) {
    float ui_z  = g_app.ui.met.size;
    /* HUD 改为浮层，场景自顶向下铺满 */
    float top_h = 0.0f;

    /* 3D 山川场景渲染与交互 */
    ui_flush(ui);
    /* 场景抗锯齿与分辨率缩放 */
    bool offscreen = g_app.region && app_lowres_begin(ui->win_w, ui->win_h);
    g_app.terrain.msaa_active = offscreen && app_lowres_msaa();
    qlcg_terrain_render(&g_app.terrain, g_app.region, g_app.world, &g_app.clock,
                        g_app.snap.utc_offset_min, &g_app.settings, ui->win_w, ui->win_h);
    if (offscreen) {
        app_lowres_end(ui->win_w, ui->win_h, ui->pix_w, ui->pix_h, app_lowres_pixelated());
    }
    /* 501：模糊场景，准心与 HUD 保持清晰 */
    float sfx_blur = ui_status_feedback_blur(&g_app.status_feedback);
    if (sfx_blur > 0.0f) ui_backdrop_blur_overlay(ui, sfx_blur);
    /* 场景已落地，抓一张背景供后续 HUD 的磨砂玻璃采样 */
    ui_backdrop_capture(ui);

    /* 处理 3D 山川内自然工程点击改造 */
    bool over_dock = ui_hit(ui, ((float)ui->win_w - 920.0f) * 0.5f, (float)ui->win_h - 80.0f, 920.0f, 80.0f) ||
                     ui_hit(ui, 0, 0, (float)ui->win_w, top_h);
    if (!over_dock && !game_scene_over_ui(ui) && ui->mouse_clicked &&
        g_app.terrain.eng_tool != ENG_TOOL_NONE && g_app.terrain.has_hover) {
        int season = g_app.region->climate_season;
        if (g_app.terrain.eng_tool == ENG_TOOL_RIVER_DIVERT) {
            int idx = g_app.terrain.river_pt_count;
            if (idx < 16) {
                g_app.terrain.river_pts_x[idx] = g_app.terrain.hover_lx;
                g_app.terrain.river_pts_z[idx] = g_app.terrain.hover_lz;
                g_app.terrain.river_pt_count++;
            }
            if (g_app.terrain.river_pt_count >= 2) {
                qlcg_eng_region_river(g_app.region, g_app.world,
                                      g_app.terrain.river_pts_x, g_app.terrain.river_pts_z,
                                      g_app.terrain.river_pt_count,
                                      g_app.terrain.river_width_m, g_app.terrain.river_depth_m,
                                      g_app.terrain.river_meander, season, &g_app.settings);
                qlcg_terrain_upload_region(&g_app.terrain, g_app.region, &g_app.settings);
                g_app.terrain.river_pt_count = 0;
                app_push_toast_k(UI_TOAST_SUCCESS, NULL, tr("terrain.river.done"));
            } else {
                app_push_toast_k(UI_TOAST_WARNING, NULL, tr("terrain.river.src"));
            }
        } else {
            qlcg_eng_region_brush(g_app.region, g_app.world, g_app.terrain.eng_tool,
                                  g_app.terrain.hover_lx, g_app.terrain.hover_lz,
                                  g_app.terrain.brush_radius, season, &g_app.settings);
            qlcg_terrain_upload_region(&g_app.terrain, g_app.region, &g_app.settings);
        }
    }

    /* base：暂停或设置抽屉打开时准星隐藏 */
    if (!g_app.esc_paused && !g_app.settings_in_drawer)
        ui_draw_crosshair(ui, g_app.settings.crosshair);
}
