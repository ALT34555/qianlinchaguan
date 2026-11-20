/* 游戏内山川视图：3D 场景与自然工程点选 */
#include "app/app_state.h"
#include "terrain3d/terrain_metrics.h"
#include "terrain3d/terrain_dolls.h"
#include "terrain3d/terrain_tools.h"

static bool terrain_tools_available(void) {
    return g_app.screen == SCREEN_IN_GAME && g_app.view_mode == VIEW_MODE_TERRAIN &&
           g_app.world && g_app.region && !g_app.esc_paused && !g_app.settings_in_drawer &&
           !g_app.inventory_ui.open && !g_app.chat_open && !g_app.touch_layout_edit &&
           !game_doll_panel_open() && !game_build_open() && !settings_confirm_active() && g_app.terrain.eng_tool == ENG_TOOL_NONE;
}

void game_terrain_use_hand(bool off_hand) {
    if (!terrain_tools_available()) return;
    if (g_app.terrain.external_camera && !g_app.terrain.camera_reticle_visible) return;
    bool worked;
    const char *message = qlcg_terrain_tool_use(&g_app.terrain, g_app.region, g_app.world,
                                               &g_app.settings, off_hand, &worked);
    if (message) app_push_toast_k(worked ? UI_TOAST_SUCCESS : UI_TOAST_WARNING, NULL, message);
}

static void terrain_tool_input(UiContext *ui) {
    if (!g_app.terrain.mouse_captured || game_scene_over_ui(ui)) return;
    if (ui->mouse_clicked) game_terrain_use_hand(false);
    if (ui->right_clicked) game_terrain_use_hand(true);
}

void game_view_terrain(UiContext *ui) {
    qlcg_terrain_prepare_frame(&g_app.terrain,g_app.region,&g_app.settings);
    TerrainHandSprite hand_sprites[2];
    unsigned mask=g_app.settings.camera_view==CAMERA_FIRST_PERSON
        ? qlcg_terrain_hand_sprites(&g_app.terrain,g_app.world,(float)g_app.settings.fov,
                                   (float)ui->win_w,(float)ui->win_h,hand_sprites) : 0;
    if (mask && !game_inventory_atlas_ready(ui)) mask=0;
    g_app.terrain.tool_sprite_mask=mask;
    /* 3D 山川场景渲染与交互 */
    ui_flush(ui);
    /* 场景抗锯齿与分辨率缩放 */
    qlcg_metrics.samples=0;
    bool offscreen = g_app.region && app_lowres_begin(ui->win_w, ui->win_h);
    GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);qlcg_metrics.width=viewport[2];qlcg_metrics.height=viewport[3];
    g_app.terrain.msaa_active = offscreen && app_lowres_msaa();
    if(g_app.terrain.msaa_active)qlcg_metrics.samples=qlcg_gl_caps.max_samples>=4?4:2;
    g_app.terrain.dolls_paused = g_app.esc_paused || g_app.settings_in_drawer;
    qlcg_terrain_render(&g_app.terrain, g_app.region, g_app.world, &g_app.clock,
                        g_app.snap.utc_offset_min, &g_app.settings, ui->win_w, ui->win_h);
    g_app.terrain.tool_sprite_mask=0;
    QlcgDollSpeech speech;
    while (qlcg_terrain_dolls_take_speech(&g_app.terrain,&speech)) {
        char line[QLCG_CHAT_LINE_BYTES];
        snprintf(line,sizeof(line),"【%s】%s",qlcg_i18n_content_text(speech.speaker),qlcg_i18n_content_text(speech.text));
        app_push_chat(line);
    }
    if (offscreen) {
        app_lowres_end(ui->win_w, ui->win_h, ui->pix_w, ui->pix_h, app_lowres_pixelated());
    }
    /* 501：模糊场景，准心与 HUD 保持清晰 */
    float sfx_blur = ui_status_feedback_blur(&g_app.status_feedback);
    if (sfx_blur > 0.0f) ui_backdrop_blur_overlay(ui, sfx_blur);
    ui_backdrop_capture(ui);
    if (mask) {
        ui_use_sprite_tex(ui,ui->inventory_tex);
        for (int i=0;i<2;++i) if (mask & (1u<<i)) {
            const TerrainHandSprite *sprite=hand_sprites+i;
            float uv[4];
            game_inventory_tile(sprite->item,false,false,uv);
            ui_draw_sprite_rect_rot(ui,sprite->cx,sprite->cy,sprite->size,sprite->size,sprite->radians,
                                    uv[0],uv[1],uv[2],uv[3],ui_rgba(255,255,255,255));
        }
    }
    game_doll_panel_input(ui);
    terrain_tool_input(ui);

    /* 处理 3D 山川内自然工程点击改造 */
    if (!game_scene_over_ui(ui) && ui->mouse_clicked &&
        (!g_app.terrain.external_camera || g_app.terrain.camera_reticle_visible) &&
        g_app.terrain.eng_tool != ENG_TOOL_NONE && g_app.terrain.has_hover) {
        int season = g_app.region->climate_season;
        if (g_app.terrain.eng_tool == ENG_TOOL_RIVER_DIVERT) {
            int idx = g_app.terrain.river_pt_count ? 1 : 0;
            g_app.terrain.river_pts_x[idx] = g_app.terrain.hover_lx;
            g_app.terrain.river_pts_z[idx] = g_app.terrain.hover_lz;
            g_app.terrain.river_pt_count = idx+1;
            if (g_app.terrain.river_pt_count >= 2) {
                EngResult result=qlcg_eng_region_river(g_app.region, g_app.world,
                                      g_app.terrain.river_pts_x, g_app.terrain.river_pts_z,
                                      g_app.terrain.river_pt_count,
                                      g_app.terrain.river_width_m, g_app.terrain.river_depth_m,
                                      g_app.terrain.river_meander, season, &g_app.settings);
                if (result==ENG_APPLIED) {
                    qlcg_terrain_upload_region(&g_app.terrain, g_app.region, &g_app.settings);
                    g_app.terrain.river_pt_count = 0;
                    app_push_toast_k(UI_TOAST_SUCCESS, NULL, tr("terrain.river.done"));
                } else {
                    app_push_toast_k(UI_TOAST_WARNING,NULL,tr(result==ENG_CAPACITY?
                                     "engineering.capacity":"engineering.river.noChange"));
                }
            } else {
                app_push_toast_k(UI_TOAST_WARNING, NULL, tr("terrain.river.src"));
            }
        } else {
            EngResult result=qlcg_eng_region_brush(g_app.region, g_app.world, g_app.terrain.eng_tool,
                                  g_app.terrain.hover_lx, g_app.terrain.hover_lz,
                                  g_app.terrain.brush_radius, season, &g_app.settings);
            if (result==ENG_APPLIED) qlcg_terrain_upload_region(&g_app.terrain,g_app.region,&g_app.settings);
            if (result==ENG_CAPACITY) app_push_toast_k(UI_TOAST_WARNING,NULL,tr("engineering.capacity"));
        }
    }

    /* base：暂停或设置抽屉打开时准星隐藏 */
    if (!g_app.esc_paused && !g_app.settings_in_drawer && !game_doll_panel_open()) {
        if (!g_app.terrain.external_camera) ui_draw_crosshair(ui,g_app.settings.crosshair);
        else if (g_app.terrain.camera_reticle_visible)
            ui_draw_crosshair_at(ui,g_app.settings.crosshair,
                                 g_app.terrain.camera_reticle[0]*ui->win_w,g_app.terrain.camera_reticle[1]*ui->win_h);
    }
}
