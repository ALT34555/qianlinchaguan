/* 世界生命周期：生成 / 进入 / 载入 / 空降 */
#include "app/app_state.h"
#include "core/dev_tools.h"
#include "ui/ui_input.h"
#include "systems/world/ice_runtime.h"
#include "systems/world/precip.h"
#include "systems/world/river_archive.h"
#include "systems/world/climate_season.h"
#include "systems/world/world_infinite.h"
#include <limits.h>

/* 系统本地时区偏移（分钟，东正） */
static int local_offset_min(void) { return qlcg_platform_local_offset(); }

/* 新世界时钟：元历固定13-10-1，其余取本地 */
static void app_new_world_clock(CalendarClock *clk, WorldCreatorOptions *opt) {
    if (qlcg_dev_world_clock(clk, opt)) return;
    QlLocalTime st;
    memset(&st, 0, sizeof(st));
    qlcg_platform_local_time(&st);
    bool ok = (st.year >= 1970 && st.year <= 3000);
    int jdn;
    double frac;
    if (opt->calendar_mode == CALENDAR_YUAN) {
        opt->start_year = 13; opt->start_month = 10; opt->start_day = 1;
        jdn = qlcg_yuan_to_jdn(13, 10, 1);
        frac = ok ? ((double)st.hour * 3600.0 + st.minute * 60.0 + st.second) / 86400.0
                  : 8.0 / 24.0;
    } else if (ok) {
        opt->start_year = st.year; opt->start_month = st.month; opt->start_day = st.day;
        jdn = qlcg_gregorian_to_jdn(st.year, st.month, st.day);
        frac = ((double)st.hour * 3600.0 + st.minute * 60.0 + st.second) / 86400.0;
    } else {
        opt->start_year = 1979; opt->start_month = 3; opt->start_day = 6;
        jdn = qlcg_gregorian_to_jdn(1979, 3, 6);
        frac = 8.0 / 24.0;
    }
    /* 当地钟面 → UTC 日内比例，跨日进位 */
    double f = frac - (double)local_offset_min() / 1440.0;
    while (f >= 1.0) { f -= 1.0; jdn += 1; }
    while (f < 0.0)  { f += 1.0; jdn -= 1; }
    qlcg_calendar_init_jd(clk, opt->calendar_mode, jdn, f, opt->creation_time_speed);
}

void app_ice_sync(int forced_season) {
    if (!g_app.world) return;
    int force = (forced_season >= 0 && forced_season < 4) ? forced_season : qlcg_dev_forced_season();
    IceTime t = qlcg_ice_time(&g_app.clock, &g_app.snap, force);
    qlcg_ice_set_time(g_app.world, &t);
    g_app.world->ice_forced_season = force;
    qlcg_precip_sync(g_app.world, g_app.clock.unix_ms);
}

static void app_dev_rain_layer(void) {
    if (!qlcg_dev_atlas_rain()) return;
    for (int i = 0; i < LAYER_ID_COUNT; i++) {
        if (g_app.atlas.display.layers[i].id == LAYER_RAIN) {
            g_app.atlas.display.layers[i].on = true;
            g_app.atlas.display.layers[i].opacity = 1.0f;
        }
    }
}

void app_toggle_world_view(void) {
    if (!g_app.world) return;
    if (g_app.view_mode == VIEW_MODE_TERRAIN) {
        if (g_app.world->opt.play_mode == PLAY_MODE_SURVIVAL) {
            qlcg_atlas_center_on(&g_app.atlas, g_app.world, (g_app.player_cx + 0.5) * CHUNK_SIZE,
                                 (g_app.player_cz + 0.5) * CHUNK_SIZE, true);
        }
        g_app.view_mode = VIEW_MODE_ATLAS;
    } else if (!g_app.region || g_app.player_cx != g_app.region->center_cx ||
               g_app.player_cz != g_app.region->center_cz) {
        app_airdrop_to_chunk(g_app.player_cx,g_app.player_cz,true);
    } else {
        g_app.view_mode = VIEW_MODE_TERRAIN;
    }
}

void app_release_world(void) {
    app_lowres_release();
    qlcg_terrain_release_scene(&g_app.terrain);
    qlcg_atlas_release_world(&g_app.atlas);
    qlcg_region_free(g_app.region);
    g_app.region = NULL;
    qlcg_world_free(g_app.world);
    g_app.world = NULL;
}

/* 空降到指定区块并采样 3D 山川（< 1s） */
static bool airdrop_world(MacroWorld *target,int cx,int cz,bool switch_to_3d) {
    if (!g_app.world || !target) return false;
    MacroWorld *old=g_app.world;
    double t0 = qlcg_time_ms();
    /* 采样前先有有效冰时刻，否则第一帧会出现完整冰 */
    app_ice_sync(-1);
    target->plant_calendar = &g_app.snap;
    if(target!=old) {
        target->plant_season_forced=old->plant_season_forced;
        target->ice_time=old->ice_time;target->ice_time_valid=old->ice_time_valid;
        target->ice_forced_season=old->ice_forced_season;target->astronomy_broadcast=old->astronomy_broadcast;
        qlcg_precip_sync(target,g_app.clock.unix_ms);
    }
    int cell = qlcg_world_cell_at_wxz(target,
                                      ((float)cx + 0.5f) * (float)CHUNK_SIZE,
                                      ((float)cz + 0.5f) * (float)CHUNK_SIZE);
    int season = target->plant_season_forced ? g_app.snap.season_index
               : qlcg_climate_season_index(target, cell, &g_app.snap);
    RegionData *region = qlcg_region_sample(target, cx, cz, season, &g_app.settings);
    if (!region) {
        app_push_toast_k(UI_TOAST_WARNING, NULL, tr("world.airdrop.noMemory"));
        if (!g_app.region) g_app.view_mode = VIEW_MODE_ATLAS;
        return false;
    }
    qlcg_ice_visual_reset(&g_app.terrain);
    qlcg_terrain_clear_chunks(&g_app.terrain);
    qlcg_region_free(g_app.region);
    g_app.region = region;
    g_app.world=target;
    g_app.player_cx = cx;
    g_app.player_cz = cz;
    qlcg_world_reveal_chunk(g_app.world, cx, cz);
    qlcg_terrain_set_season(&g_app.terrain, season);
    qlcg_terrain_upload_region(&g_app.terrain, g_app.region, &g_app.settings);
    qlcg_terrain_place_player_at_spawn(&g_app.terrain, g_app.region, &g_app.settings);
    /* 对齐base：flying=0，双击跳跃起飞 */
    g_app.terrain.flying = false;
    g_app.airdrop_ms = qlcg_time_ms() - t0;
    if(target!=old) {
        qlcg_atlas_release_world(&g_app.atlas);qlcg_atlas_upload_world(&g_app.atlas,target);
        qlcg_atlas_center_on(&g_app.atlas,target,(cx+.5)*CHUNK_SIZE,(cz+.5)*CHUNK_SIZE,true);
        g_app.atlas.has_selection=false;g_app.atlas.hover_info=(ChunkInspectInfo){0};
        g_app.river_craft=(RiverCraft){0};
        qlcg_astra_upload(&g_app.terrain.astronomy,&target->opt);
    }

    if (switch_to_3d) {
        g_app.view_mode = VIEW_MODE_TERRAIN;
    }
    return true;
}

void app_airdrop_to_chunk(int cx,int cz,bool switch_to_3d) {
    airdrop_world(g_app.world,cx,cz,switch_to_3d);
}

bool app_atlas_airdrop(double wx,double wz) {
    double lx,lz;MacroWorld *w=qlcg_infinite_resolve(g_app.world,wx,wz,&lx,&lz);
    if(!w)return false;
    double cx=floor(lx/CHUNK_SIZE),cz=floor(lz/CHUNK_SIZE);
    if(cx<INT_MIN || cx>INT_MAX || cz<INT_MIN || cz>INT_MAX)return false;
    if(w->opt.play_mode==PLAY_MODE_SURVIVAL && !qlcg_world_chunk_explored(w,(int)cx,(int)cz))return false;
    return airdrop_world(w,(int)cx,(int)cz,true);
}

static void app_replace_world(MacroWorld *world) {
    g_app.atlas.astronomy_overlay=false;
    g_app.atlas.eclipse_ready=false;
    g_app.river_craft=(RiverCraft){0};
    qlcg_astra_release(&g_app.terrain.astronomy);
    if (g_app.region) qlcg_terrain_clear_chunks(&g_app.terrain);
    qlcg_region_free(g_app.region);
    g_app.region = NULL;
    qlcg_world_free(g_app.world);
    g_app.world = world;
    if (world && g_app.terrain.sky_prog && world->opt.astronomy_enabled)
        qlcg_astra_upload(&g_app.terrain.astronomy, &world->opt);
}

/* 重新生成预览世界 */
bool app_rebuild_creator_world(void) {
    bool playing = g_app.screen == SCREEN_IN_GAME;
    if (!playing) app_replace_world(NULL);
    MacroWorld *world = qlcg_world_generate(&g_app.creator_opt);
    if (!world) {
        if (!playing) qlcg_atlas_release_world(&g_app.atlas);
        return false;
    }
    app_replace_world(world);
    qlcg_atlas_upload_world(&g_app.atlas, g_app.world);
    qlcg_atlas_reset_view(&g_app.atlas, g_app.world, false);
    app_refresh_creator_clock();
    return true;
}

void app_refresh_creator_clock(void) {
    app_new_world_clock(&g_app.clock, &g_app.creator_opt);
    qlcg_calendar_snapshot(&g_app.clock, 0, &g_app.snap);
    if (g_app.world) {
        g_app.world->opt.calendar_mode = g_app.creator_opt.calendar_mode;
        g_app.world->plant_calendar = &g_app.snap;
    }
    app_ice_sync(-1);
}

/* 正式进入当前创建或载入的世界 */
void app_enter_world_from_creator(void) {
    g_app.terrain.astronomy.observing = false;
    g_app.atlas.astronomy_overlay=false;
    if (!g_app.world) app_rebuild_creator_world();
    if (!g_app.world) return;
    WorldCreatorOptions *opt = &g_app.world->opt;
    ui_u8_copy(opt->world_name, sizeof(opt->world_name), g_app.creator_opt.world_name);
    ui_u8_copy(opt->ren_ming, sizeof(opt->ren_ming), g_app.creator_opt.ren_ming);
    ui_u8_copy(opt->seed_str, sizeof(opt->seed_str), g_app.creator_opt.seed_str);
    opt->creation_time_speed=g_app.creator_opt.creation_time_speed;
    app_dev_rain_layer();
    g_app.atlas.layer_panel_open = false;
    g_app.atlas.layer_dragging = false;
    g_app.atlas.layer_scroll = 0.0f;
    app_new_world_clock(&g_app.clock, &g_app.world->opt);
    qlcg_calendar_snapshot(&g_app.clock, 0, &g_app.snap);
    if (g_app.world) g_app.world->plant_calendar = &g_app.snap;
    app_ice_sync(-1);
    g_app.world_menu_auto_pause = false;   /* 新建世界默认不自动暂停（同 base 默认玄虚） */
    snprintf(g_app.save_name, sizeof(g_app.save_name), "%s", g_app.world->opt.world_name);
    g_app.loaded_slot_name[0] = '\0';
    app_airdrop_to_chunk(g_app.world->spawn_cx, g_app.world->spawn_cz, false);
    qlcg_atlas_reset_view(&g_app.atlas, g_app.world, true);
    g_app.view_mode  = VIEW_MODE_ATLAS;
    g_app.screen     = SCREEN_IN_GAME;
    g_app.esc_paused = false;
    g_app.chat_log_count = 0;
    g_app.current_save_id[0] = '\0';
    g_app.save_idle_s = 0.0f;
    g_app.auto_save_flash = 0.0f;
    app_manual_slot_reset();
}

/* 从存档槽载入世界 */
bool app_load_from_save_slot(const SaveSlotInfo *slot, long offset, long size) {
    if (!slot || !slot->valid) return false;
    g_app.creator_opt = slot->opt;
    if (!g_app.creator_opt.world_name[0])
        ui_u8_copy(g_app.creator_opt.world_name, sizeof(g_app.creator_opt.world_name), slot->world_name);
    MacroWorld rivers={0}; rivers.opt=g_app.creator_opt;
    qlcg_sync_creator_dimensions(&rivers.opt); rivers.columns=rivers.opt.columns;
    if(!qlcg_saves_read_rivers(&rivers,slot,offset,size)) {
        app_push_toast_k(UI_TOAST_ERROR,NULL,tr("world.load.failed")); return false;
    }
    MacroWorld *loaded = qlcg_world_generate_with_rivers(&g_app.creator_opt,rivers.river_archive);
    if (!loaded || !qlcg_saves_restore_deltas(loaded, slot, offset, size)) {
        if (loaded) qlcg_world_free(loaded);
        app_push_toast_k(UI_TOAST_ERROR, NULL, tr("world.load.failed"));
        return false;
    }
    /* 冰编辑已在 restore 内读入；缺表时补建空编辑 */
    if (!loaded->ice_state) loaded->ice_state = qlcg_ice_world_create();
    app_replace_world(loaded);
    qlcg_world_rebuild_atlas_fields(g_app.world);

    g_app.atlas.display = slot->atlas_display;
    qlcg_normalize_atlas_display(&g_app.atlas.display);
    g_app.atlas.layer_panel_open = false;
    qlcg_atlas_reset_view(&g_app.atlas, g_app.world, true);
    app_dev_rain_layer();
    g_app.atlas.layer_dragging = false;
    g_app.atlas.layer_scroll = 0.0f;
    qlcg_atlas_upload_world(&g_app.atlas, g_app.world);

    /* 玄虚随存档恢复，不再读全局设置 */
    qlcg_calendar_init(&g_app.clock, slot->calendar_mode, slot->year, slot->month, slot->day,
                       slot->mystery_time_speed);
    g_app.world_menu_auto_pause = slot->mystery_menu_auto_pause;
    snprintf(g_app.save_name, sizeof(g_app.save_name), "%s",
             slot->world_name[0] ? slot->world_name : slot->opt.world_name);
    g_app.clock.unix_ms = slot->unix_ms;
    qlcg_calendar_advance(&g_app.clock, 0.0f);
    qlcg_calendar_snapshot(&g_app.clock, 0, &g_app.snap);
    if (g_app.world) g_app.world->plant_calendar = &g_app.snap;
    app_ice_sync(-1);

    float wx = slot->player_x, wz = slot->player_z;
    if (slot->save_version == 2u) {
        wx += (g_app.world->spawn_cx+0.5f)*CHUNK_SIZE;
        wz += (g_app.world->spawn_cz+0.5f)*CHUNK_SIZE;
    }
    app_airdrop_to_chunk((int)floorf(wx/CHUNK_SIZE), (int)floorf(wz/CHUNK_SIZE), false);
    if (!g_app.region) {
        app_release_world();
        return false;
    }
    g_app.terrain.x      = wx-g_app.region->center_x;
    g_app.terrain.y      = slot->player_y;
    g_app.terrain.z      = wz-g_app.region->center_z;
    /* 地形变化后旧档位姿可能落到地面以下，读档抬回地表 */
    float gh = qlcg_region_height_at(g_app.region, g_app.terrain.x, g_app.terrain.z);
    if (g_app.terrain.y < gh) g_app.terrain.y = gh + 1.6f;
    g_app.terrain.yaw    = slot->player_yaw;
    g_app.terrain.pitch  = slot->player_pitch;
    g_app.terrain.flying = slot->flying;
    qlcg_atlas_center_on(&g_app.atlas,g_app.world,wx,wz,true);

    g_app.screen     = SCREEN_IN_GAME;
    g_app.view_mode  = VIEW_MODE_TERRAIN;
    g_app.loaded_slot_name[0] = '\0';
    g_app.esc_paused = false;
    g_app.save_idle_s = 0.0f;
    g_app.auto_save_flash = 0.0f;
    char msg[128];
    snprintf(msg, sizeof(msg), tr("world.load.done"), slot->world_name);
    app_push_toast(msg);
    return true;
}

/* 读档入口：文件 + 槽号 */
bool app_load_from_save_file(const SaveFileInfo *file, int slot) {
    if (file && slot >= 0 && slot < QLCG_SAVE_SLOT_COUNT && file->slots[slot].present &&
        !qlcg_save_version_supported(file->slots[slot].disk_version)) {
        app_push_toast_k(UI_TOAST_ERROR, NULL, tr("saves.unsupported")); return false;
    }
    if (!file) return false;
    SaveSlotInfo info;
    long offset = 0, size = 0;
    if (!qlcg_saves_load_slot(file, slot, &info, &offset, &size)) {
        app_push_toast_k(UI_TOAST_ERROR, NULL, tr("world.load.failed"));
        return false;
    }
    if (!app_load_from_save_slot(&info, offset, size)) return false;
    ui_u8_copy(g_app.current_save_id, sizeof(g_app.current_save_id), file->id);
    ui_u8_copy(g_app.loaded_slot_name, sizeof(g_app.loaded_slot_name),
               qlcg_save_slot_display_name(&file->slots[slot]));
    app_manual_slot_reset();
    return true;
}
