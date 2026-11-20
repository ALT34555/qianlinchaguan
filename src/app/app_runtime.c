#include "app/app_state.h"
#include "app/app_doll_control.h"
#include "core/dev_tools.h"
#include "systems/world/ice_runtime.h"
#include "systems/world/climate_season.h"
#include "systems/civilization/civ_content.h"
#include "systems/dolls/doll_ai.h"
#include "systems/dolls/doll_life.h"
#include "platform/android/bridge.h"

void app_shutdown(void) {
    game_build_close();
    app_doll_control_reset();
    app_set_mouse_lock(false);   /* 退出前还回光标与捕获 */

    if (g_app.region) qlcg_region_free(g_app.region);
    if (g_app.world)  qlcg_world_free(g_app.world);
    app_lowres_release();
    qlcg_terrain_shutdown(&g_app.terrain);
    qlcg_atlas_shutdown(&g_app.atlas);
    qlcg_civ_content_free();
    ui_shutdown(&g_app.ui);
    ui_font_mod_unload();
    qlcg_platform_window_close();
}

bool app_initialize(void) {
    qlcg_platform_prepare();
    char content_error[256];
    if(!qlcg_civ_content_load("content/data/civilization","userdata/mods/civilization",content_error,sizeof(content_error)))
        fprintf(stderr,"[civilization] %s\n",content_error);
    qlcg_doll_types_init();
    memset(&g_app, 0, sizeof(g_app));
    g_app.win_w = 1440;
    g_app.win_h = 840;
    g_app.running = true;
    g_app.save_filter_mode = -1;

    g_app.settings = qlcg_default_settings();
    qlcg_load_settings_disk(&g_app.settings);
    QLCG_DEV_ONLY(qlcg_dev_settings_override(&g_app.settings));
    qlcg_i18n_init(QLCG_L10N_DIR);
    qlcg_i18n_set_locale(g_app.settings.locale);
    if (!qlcg_platform_window_open(g_app.win_w, g_app.win_h)) return false;
    if (!ui_init(&g_app.ui, g_app.win_w, g_app.win_h)) { app_shutdown(); return false; }
    ui_touch_reset(&g_app.touch);
    /* 界面适配三轴：控件尺寸 / 文字缩放 / 安全区 */
    app_sync_layout_axes();
    /* mod 字体：注册后按设置项应用 */
    ui_font_mod_scan(QLCG_FONT_DIR, &g_app.mod_fonts);
    app_apply_font();
    ui_apply_theme(&g_app.ui, g_app.settings.theme);
    if(!qlcg_atlas_init(&g_app.atlas) || !qlcg_terrain_init(&g_app.terrain)) {
        app_shutdown(); return false;
    }

    app_refresh_saves();
    g_app.creator_opt = qlcg_default_creator_options(PLAY_MODE_LEISURE, &g_app.settings);
    g_app.fps_smooth  = 120.0f;

    /* 应用档案衣冠配色 */
    app_apply_ui_palette();

    return true;
}

void app_update(float dt) {
#ifdef __ANDROID__
    char kind[32],path[2048],error[256];
    while(qlcg_android_document_poll(kind,sizeof(kind),path,sizeof(path),error,sizeof(error))) {
        if(!strcmp(kind,"export"))app_push_toast_k(UI_TOAST_SUCCESS,NULL,tr("export.fileDone"));
        else if(!strcmp(kind,"error"))app_push_toast_k(UI_TOAST_WARNING,NULL,tr(error));
    }
#endif
    /* 按视图、菜单和触屏状态收敛鼠标锁 */
    app_set_mouse_lock(app_want_mouse_lock());

    /* 按玄虚暂停设置推进世界 */
    bool menu_stops_world = g_app.esc_paused && g_app.world_menu_auto_pause;
    if (g_app.screen == SCREEN_IN_GAME && g_app.world && !menu_stops_world) {
        double before = g_app.clock.unix_ms;
        qlcg_calendar_advance(&g_app.clock, dt);
        app_astra_advance(before);
        int utc_off = qlcg_world_flat(g_app.world)?0:qlcg_timezone_offset_min((float)g_app.player_cx * (float)CHUNK_SIZE, g_app.world->circumference);
        qlcg_calendar_snapshot(&g_app.clock, utc_off, &g_app.snap);
        qlcg_world_update_artificial(g_app.world, g_app.clock.unix_ms, g_app.player_cx, g_app.player_cz);

        if (!g_app.clock.paused && dt>0)
            qlcg_doll_life_update(g_app.world,g_app.clock.unix_ms,dt,g_app.player_cx,g_app.player_cz);

        /* 每次更新同步冰时刻 */
        app_ice_sync(-1);

        if (g_app.region) {
            int season = g_app.world->plant_season_forced
                       ? g_app.snap.season_index
                       : qlcg_climate_season_index(g_app.world, g_app.region->site_cell, &g_app.snap);
            if (season != g_app.region->climate_season) {
                g_app.region->climate_season = season;
                qlcg_terrain_set_season(&g_app.terrain, season);
            }
        }

        if (g_app.settings.natural_physics) {
            qlcg_tick_natural_events(g_app.world, g_app.region, dt,
                                     &g_app.terrain.cam_shake, &g_app.terrain.wind_strength, &g_app.terrain.rain_override);
        } else {
            qlcg_tick_natural_events(g_app.world,g_app.region,dt,NULL,NULL,NULL);
            g_app.terrain.cam_shake=g_app.terrain.rain_override=0.f;
            g_app.terrain.wind_strength=.2f;
        }

        if (!g_app.inventory_ui.open && g_app.view_mode == VIEW_MODE_ATLAS &&
            g_app.world->opt.play_mode == PLAY_MODE_LEISURE) {
            int step_x = 0, step_z = 0;
            if (app_key_down(QL_KEY_LEFT)  || app_key_down('A')) step_x -= 1;
            if (app_key_down(QL_KEY_RIGHT) || app_key_down('D')) step_x += 1;
            if (app_key_down(QL_KEY_UP)    || app_key_down('W')) step_z -= 1;
            if (app_key_down(QL_KEY_DOWN)  || app_key_down('S')) step_z += 1;
            if ((step_x != 0 || step_z != 0) && (g_app.ui.frame_idx % 4 == 0)) {
                g_app.player_cx += step_x;
                g_app.player_cz += step_z;
            }
        } else if (g_app.view_mode == VIEW_MODE_TERRAIN && !g_app.chat_open && !game_build_open()) {
            int old_cx = g_app.player_cx, old_cz = g_app.player_cz;
            PlayerInput pin = {0};
            if (!g_app.inventory_ui.open && !game_doll_panel_open()) app_build_player_input(&pin);
            qlcg_terrain_update_player(&g_app.terrain, g_app.region, g_app.world, &g_app.settings, &g_app.clock, &pin, dt);
            g_app.player_cx = g_app.region->center_cx;
            g_app.player_cz = g_app.region->center_cz;
            if (old_cx != g_app.player_cx || old_cz != g_app.player_cz)
                qlcg_world_reveal_chunk(g_app.world, g_app.player_cx, g_app.player_cz);
        }
    }

    app_doll_sync_pose();
    if(g_app.screen==SCREEN_IN_GAME && g_app.world && !g_app.esc_paused && !game_build_open()) app_infinite_step();
    /* 自动存档计时按现实时间走，含菜单暂停 */
    if (g_app.screen == SCREEN_IN_GAME && g_app.world) app_save_tick(dt);
    if (g_app.auto_save_flash > 0.0f) {
        g_app.auto_save_flash -= dt;
        if (g_app.auto_save_flash < 0.0f) g_app.auto_save_flash = 0.0f;
    }

}
