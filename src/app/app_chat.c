/* 斜杠指令分发 */
#include "app/app_state.h"
#include "core/dev_tools.h"
#include "systems/world/climate_season.h"

/* 世界生成参数查看与就地重建，对齐 base 创建器 */
static void world_param_command(const char *cmd) {
    WorldCreatorOptions *o = &g_app.creator_opt;
    const char *key = cmd + 7;
    char reply[256];
    bool changed = false;

    if (strncmp(key, "info", 4) == 0 || key[0] == '\0') {
        snprintf(reply, sizeof(reply),
                 tr("chat.world.info"),
                 o->seed, o->columns, o->columns / 2, o->chunks_per_cell,
                 qlcg_terrain_recipe_name(o->terrain_recipe), o->land_ratio * 100.0f,
                 o->precipitation, o->pole_temp, o->equator_temp,
                 o->tectonic_activity, o->monsoon_direction, o->axial_tilt_deg,
                 o->prograde ? tr("creator.rotation.prograde") : tr("creator.rotation.retrograde"));
        app_push_chat(reply);
        return;
    }

    const char *sp = strchr(key, ' ');
    float v = sp ? (float)atof(sp + 1) : 0.0f;

    if (strncmp(key, "seed", 4) == 0) {
        if (sp) { o->seed = (uint32_t)strtoul(sp + 1, NULL, 10); snprintf(o->seed_str, sizeof(o->seed_str), "%u", o->seed); changed = true; }
    } else if (strncmp(key, "size", 4) == 0) {
        if (sp) { o->macro_size_idx = qlcg_clampi((int)v, 0, 2); qlcg_sync_creator_dimensions(o); changed = true; }
    } else if (strncmp(key, "scale", 5) == 0) {
        if (sp) { o->macro_scale_idx = qlcg_clampi((int)v, 0, 6); qlcg_sync_creator_dimensions(o); changed = true; }
    } else if (strncmp(key, "recipe", 6) == 0) {
        if (sp) { o->terrain_recipe = (MacroTerrainRecipe)qlcg_clampi((int)v, 0, 3); changed = true; }
    } else if (strncmp(key, "land", 4) == 0) {
        if (sp) { o->land_ratio = qlcg_clampf(v, 0.0f, 1.0f); changed = true; }
    } else if (strncmp(key, "rain", 4) == 0) {
        if (sp) { o->precipitation = qlcg_clampf(v, 0.0f, 2.0f); changed = true; }
    } else if (strncmp(key, "tectonic", 8) == 0) {
        if (sp) { o->tectonic_activity = qlcg_clampi((int)v, 0, 9); changed = true; }
    } else if (strncmp(key, "pole", 4) == 0) {
        if (sp) { o->pole_temp = qlcg_clampf(v, -100.0f, 100.0f); changed = true; }
    } else if (strncmp(key, "equator", 7) == 0) {
        if (sp) { o->equator_temp = qlcg_clampf(v, -100.0f, 100.0f); changed = true; }
    } else if (strncmp(key, "monsoon", 7) == 0) {
        if (sp) { o->monsoon_direction = qlcg_clampi((int)v, 0, 359); changed = true; }
    } else if (strncmp(key, "tilt", 4) == 0) {
        if (sp) { o->axial_tilt_deg = qlcg_clampf(v, 0.0f, 60.0f); changed = true; }
    } else if (strncmp(key, "prograde", 8) == 0) {
        if (sp) { o->prograde = ((int)v != 0); changed = true; }
    } else if (strncmp(key, "flag", 4) == 0) {
        if (sp) { o->initial_civ_limit = qlcg_clampi((int)v, 0, 32); changed = true; }
    } else {
        app_push_chat(tr("chat.usage.world"));
        return;
    }

    if (!changed) {
        app_push_chat(tr("chat.world.unchanged"));
        return;
    }
    if (!g_app.world) {
        app_push_chat(tr("chat.world.noWorld"));
        return;
    }
    if (!app_rebuild_creator_world()) {
        app_push_toast_k(UI_TOAST_WARNING, NULL, tr("world.airdrop.noMemory"));
        return;
    }
    g_app.player_cx = g_app.world->spawn_cx;
    g_app.player_cz = g_app.world->spawn_cz;
    if (g_app.screen == SCREEN_IN_GAME) {
        app_airdrop_to_chunk(g_app.player_cx, g_app.player_cz, g_app.view_mode == VIEW_MODE_TERRAIN);
    }
    snprintf(reply, sizeof(reply), tr("chat.world.rebuilt"),
             o->seed, o->land_ratio * 100.0f, o->precipitation);
    app_push_chat(reply);
    app_push_toast(reply);
}


/* 历法设置：模式 / 起始日期 / 流速，改动后重建时钟 */
static void calendar_command(const char *cmd) {
    char reply[160];
    const char *arg = cmd + 10;   /* 跳过 "/calendar." */
    if (strncmp(arg, "mode", 4) == 0) {
        const char *v = arg + 4;
        while (*v == ' ') v++;
        if (strcmp(v, "yuan") == 0) g_app.creator_opt.calendar_mode = CALENDAR_YUAN;
        else if (strcmp(v, "gregorian") == 0) g_app.creator_opt.calendar_mode = CALENDAR_GREGORIAN;
        else if (strcmp(v, "lunar") == 0) g_app.creator_opt.calendar_mode = CALENDAR_LUNAR;
        else if (strcmp(v, "default") == 0 || strcmp(v, "real") == 0)
            g_app.creator_opt.calendar_mode = CALENDAR_DEFAULT;
        else { app_push_chat(tr("chat.usage.calendarMode")); return; }
    } else if (strncmp(arg, "date", 4) == 0) {
        int y = 0, m = 0, d = 0;
        if (sscanf(arg + 4, "%d-%d-%d", &y, &m, &d) != 3) {
            app_push_chat(tr("chat.usage.calendarDate"));
            return;
        }
        g_app.creator_opt.start_year = qlcg_clampi(y, 1000, 3000);
        g_app.creator_opt.start_month = qlcg_clampi(m, 1, 12);
        g_app.creator_opt.start_day = qlcg_clampi(d, 1, 31);
    } else if (strncmp(arg, "speed", 5) == 0) {
        double spd = atof(arg + 5);
        g_app.creator_opt.creation_time_speed = qlcg_clampf((float)spd, 0.0f, 240.0f);
        g_app.clock.time_speed = g_app.creator_opt.creation_time_speed;
        snprintf(reply, sizeof(reply), tr("chat.creator.speed"),
                 g_app.creator_opt.creation_time_speed);
        app_push_chat(reply);
        return;
    } else if (strncmp(arg, "info", 4) == 0) {
        snprintf(reply, sizeof(reply), tr("chat.creator.info"),
                 qlcg_calendar_mode_name(g_app.creator_opt.calendar_mode),
                 g_app.creator_opt.start_year, g_app.creator_opt.start_month,
                 g_app.creator_opt.start_day, g_app.creator_opt.creation_time_speed);
        app_push_chat(reply);
        return;
    } else {
        app_push_chat(tr("chat.usage.calendar"));
        return;
    }

    qlcg_calendar_init(&g_app.clock, g_app.creator_opt.calendar_mode,
                       g_app.creator_opt.start_year, g_app.creator_opt.start_month,
                       g_app.creator_opt.start_day, g_app.creator_opt.creation_time_speed);
    qlcg_calendar_snapshot(&g_app.clock, 0, &g_app.snap);
    if (g_app.region && g_app.world) {
        int season = g_app.world->plant_season_forced ? g_app.snap.season_index
                   : qlcg_climate_season_index(g_app.world, g_app.region->site_cell, &g_app.snap);
        g_app.region->climate_season = season;
        qlcg_region_recolor(g_app.region, g_app.world, season);
        qlcg_region_scatter_props(g_app.region, g_app.world, season, &g_app.settings);
        qlcg_terrain_set_season(&g_app.terrain, season);
        qlcg_terrain_upload_region(&g_app.terrain, g_app.region, &g_app.settings);
    }
    snprintf(reply, sizeof(reply), tr("chat.calendar.switched"),
             qlcg_calendar_mode_name(g_app.creator_opt.calendar_mode),
             g_app.creator_opt.start_year, g_app.creator_opt.start_month, g_app.creator_opt.start_day);
    app_push_chat(reply);
    app_push_toast(reply);
}

/* 发送当前聊天内容：Enter 与发送按钮共用 */
bool app_chat_send(void) {
    if (!g_app.chat_open) return false;
    size_t len = strlen(g_app.chat_input);
    app_exec_chat_command(g_app.chat_input);
    g_app.chat_input[0] = '\0';
    g_app.ui.edit_chat.caret  = g_app.ui.edit_chat.anchor = 0;
    g_app.ui.edit_chat.scroll = 0.0f;
    app_text_focus_clear();
    QLCG_DEV_INPUT_LOG("[in] send len %u\n", (unsigned)len);
    return true;
}

/* 斜杠指令分发，对齐 base ChatPanel */
void app_exec_chat_command(const char *cmd) {
    if (!cmd || !cmd[0]) return;
    /* 自己的发言先入日志，同 base player 格式 */
    {
        char who[QLCG_CHAT_LINE_BYTES];
        snprintf(who, sizeof(who), tr("chat.playerFmt"), g_app.settings.ren_ming, cmd);
        ui_u8_trim(who);   /* snprintf 可能切在字节中间，去掉残缺尾序列 */
        app_push_chat(who);
    }
    /* 玄虚解锁：必须排在 /world. 前缀分支之前 */
    if (strcmp(cmd, QLCG_UNLOCK_CMD) == 0) {
        g_app.cheats_unlocked = true;
        app_push_chat(tr("chat.unlock.done"));
        app_push_toast(tr("chat.unlock.toast"));
        return;
    }
    if (strncmp(cmd,"/world.river",12)==0&&(cmd[12]=='\0'||cmd[12]==' ')) {
        app_river_craft_command(cmd+12);
    } else if (strcmp(cmd, "/world.astra events") == 0) {
        app_astra_report();
    } else if (strcmp(cmd, "/world.astra") == 0) {
        if (!g_app.world || g_app.screen != SCREEN_IN_GAME) { app_push_chat(tr("chat.world.noWorld")); return; }
        if (!g_app.world->opt.astronomy_enabled) { app_push_chat(tr("chat.astra.disabled")); return; }
        g_app.terrain.astronomy.observing = !g_app.terrain.astronomy.observing;
        g_app.atlas.astronomy_overlay=g_app.terrain.astronomy.observing;
        app_push_chat(tr(g_app.terrain.astronomy.observing ? "chat.astra.on" : "chat.astra.off"));
    } else if (strncmp(cmd, "/world.weather", 14) == 0) {
        char reply[128];
        if (qlcg_weather_command(&g_app.terrain.weather_sys, cmd, reply, sizeof(reply))) {
            app_push_chat(reply);
            app_push_toast(reply);
        }
    } else if (strncmp(cmd, "/calendar.", 10) == 0) {
        calendar_command(cmd);
    } else if (strncmp(cmd, "/world.", 7) == 0) {
        world_param_command(cmd);
    } else if (strncmp(cmd, "/time.speed", 11) == 0) {
        float spd = (float)atof(cmd + 11);
        g_app.clock.time_speed = qlcg_clampf(spd, 0.0f, 240.0f);
        g_app.clock.paused = (spd <= 0.001f);
        char reply[96];
        snprintf(reply, sizeof(reply), tr("chat.speed.set"), g_app.clock.time_speed);
        app_push_chat(reply);
    } else if (strncmp(cmd, "/event", 6) == 0) {
        int et = qlcg_clampi(atoi(cmd + 6), 1, NAT_EVENT_COUNT - 1);
        if (g_app.view_mode == VIEW_MODE_TERRAIN && g_app.region) {
            int season = g_app.region->climate_season;
            qlcg_apply_region_event(g_app.region, g_app.world, (NaturalEventType)et,
                                    g_app.terrain.x, g_app.terrain.z, season, &g_app.settings);
            qlcg_terrain_set_season(&g_app.terrain, season);
            qlcg_terrain_upload_region(&g_app.terrain, g_app.region, &g_app.settings);
        } else if (g_app.world) {
            qlcg_trigger_macro_event(g_app.world, (NaturalEventType)et, g_app.atlas.cam_wx, g_app.atlas.cam_wz);
        }
        app_push_chat(g_app.world->last_event_msg);
    } else if (strncmp(cmd, "/system.gaming_info", 19) == 0) {
        /* 命令响应走 i18n，缺键回退中文 */
        const char *v = cmd + 19;
        while (*v == ' ') v++;
        if (strncmp(v, "on", 2) == 0)       g_app.show_debug_f12 = true;
        else if (strncmp(v, "off", 3) == 0) g_app.show_debug_f12 = false;
        else { app_push_chat(tr("chat.usage.gamingInfo")); return; }
        app_push_chat(tr(g_app.show_debug_f12 ? "chat.gamingInfo.on" : "chat.gamingInfo.off"));
    } else if (strncmp(cmd, "/visual.statu", 13) == 0 &&
               (cmd[13] == '\0' || cmd[13] == ' ')) {
        if (!app_cheats_allowed()) { app_push_chat(tr("chat.commandsBlocked")); return; }
        const char *v = cmd + 13;
        while (*v == ' ') v++;
        char reply[160];
        ui_status_feedback_command(&g_app.status_feedback, v, reply, sizeof(reply));
        app_push_chat(reply);
    } else if (strncmp(cmd, "/visual.status", 14) == 0) {
        const char *v = cmd + 14;
        while (*v == ' ') v++;
        if (strncmp(v, "on", 2) == 0)       g_app.settings.status_feedback = true;
        else if (strncmp(v, "off", 3) == 0) g_app.settings.status_feedback = false;
        else { app_push_chat(tr("chat.usage.visual")); return; }
        app_push_chat(tr(g_app.settings.status_feedback ? "chat.visual.on" : "chat.visual.off"));
    } else if (strncmp(cmd, "/help", 5) == 0) {
        app_push_chat(tr("chat.usage.all"));
        app_push_chat("/world.astra");
        app_push_chat("/world.river craft | /world.river");
        app_push_chat("/world.info | seed|size|scale|recipe|land|rain|tectonic|pole|equator|monsoon|flag");
        app_push_chat("/calendar.mode real|yuan | /calendar.date YYYY-M-D | /calendar.speed N | /calendar.info");
        app_push_chat("/system.gaming_info on|off | /visual.status on|off | /time.speed N | /event N");
        app_push_chat("/visual.statu 001|002|003|101|102|103|501|reset");
    }
}
