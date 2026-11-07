/* planet_meta */
#include "planet_macro.h"
#include "../../core/i18n.h"
#include "world_internal.h"
#include "../../core/math_util.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

const int MACRO_SIZES[3] = {256, 512, 1024};
const int MACRO_SCALES[7] = {128, 200, 320, 500, 720, 960, 1250};
const char *FLOW_DIR_NAMES[8] = {"东","东北","北","西北","西","西南","南","东南"};

const char *qlcg_world_preset_name(WorldPresetId p) {
    static const char *const KEYS[5] = {
        "creator.preset.habitable", "creator.preset.primordial", "creator.preset.frozen",
        "creator.preset.archipelago", "creator.preset.redhot"
    };
    return tr(KEYS[qlcg_clampi((int)p, 0, 4)]);
}

const char *qlcg_terrain_recipe_name(MacroTerrainRecipe r) {
    static const char *const KEYS[4] = {
        "creator.macroTerrain.balanced", "creator.macroTerrain.archipelago",
        "creator.macroTerrain.mountains", "creator.macroTerrain.rift"
    };
    return tr(KEYS[qlcg_clampi((int)r, 0, 3)]);
}

const char *qlcg_climate_rule_name(MacroClimateRule c) {
    static const char *const KEYS[7] = {
        "climate.rule.0", "climate.rule.1", "climate.rule.2",
        "climate.rule.3", "climate.rule.4", "climate.rule.5", "climate.rule.6"
    };
    return tr(KEYS[qlcg_clampi((int)c, 0, 6)]);
}

const char *qlcg_macro_size_label(int idx) {
    static char buf[64];
    static const char *const KEYS[3] = {
        "creator.macroSize.0", "creator.macroSize.1", "creator.macroSize.2"
    };
    static const int SZ[3] = {256, 512, 1024};
    int i = qlcg_clampi(idx, 0, 2);
    snprintf(buf, sizeof(buf), "%s (%d)", tr(KEYS[i]), SZ[i]);
    return buf;
}

int qlcg_macro_scale_chunks(int idx) {
    return MACRO_SCALES[qlcg_clampi(idx, 0, 6)];
}

void qlcg_sync_creator_dimensions(WorldCreatorOptions *opt) {
    opt->columns = opt->map_mode == WORLD_MAP_INFINITE ? 100 : MACRO_SIZES[qlcg_clampi(opt->macro_size_idx, 0, 2)];
    opt->chunks_per_cell = MACRO_SCALES[qlcg_clampi(opt->macro_scale_idx, 0, 6)];
    opt->equator_chunks = opt->columns * opt->chunks_per_cell;
}

/* 五大星球预设，对齐 WorldPresets.ts */
void qlcg_apply_world_preset(WorldCreatorOptions *opt, WorldPresetId preset) {
    opt->preset = preset;
    /* base 预设只改星球参数与陆比/降水，不动地形配方 */
    switch (preset) {
        case WORLD_PRESET_PRIMORDIAL:
            opt->land_ratio        = 0.8f;
            opt->precipitation     = 0.50f;
            opt->equator_temp      = 30.0f;
            opt->pole_temp         = -25.0f;
            opt->tectonic_activity = 0;
            break;
        case WORLD_PRESET_FROZEN:
            opt->land_ratio        = 0.5f;
            opt->precipitation     = 0.30f;
            opt->equator_temp      = -20.0f;
            opt->pole_temp         = -80.0f;
            opt->tectonic_activity = 5;
            break;
        case WORLD_PRESET_HABITABLE:
            opt->land_ratio        = 0.3f;
            opt->precipitation     = 1.00f;
            opt->equator_temp      = 30.0f;
            opt->pole_temp         = -25.0f;
            opt->tectonic_activity = 5;
            break;
        case WORLD_PRESET_ARCHIPELAGO:
            opt->land_ratio        = 0.2f;
            opt->precipitation     = 0.80f;
            opt->equator_temp      = 35.0f;
            opt->pole_temp         = -15.0f;
            opt->tectonic_activity = 9;
            break;
        case WORLD_PRESET_REDHOT:
            opt->land_ratio        = 0.8f;
            opt->precipitation     = 0.0f;
            opt->equator_temp      = 90.0f;
            opt->pole_temp         = 40.0f;
            opt->tectonic_activity = 9;
            break;
        default:
            opt->preset            = WORLD_PRESET_HABITABLE;
            opt->land_ratio        = 0.3f;
            opt->precipitation     = 1.00f;
            opt->equator_temp      = 30.0f;
            opt->pole_temp         = -25.0f;
            opt->tectonic_activity = 5;
            break;
    }
}

static void default_world_name(char *buf, size_t cap) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(buf, cap, "%s%02d%02d", tr("creator.defaultWorldName.prefix"),
             st.wMonth, st.wDay);
}

/* 默认世界创建器参数（对齐 WorldCr */
WorldCreatorOptions qlcg_default_creator_options(PlayMode mode, const GameSettings *settings) {
    WorldCreatorOptions opt;
    memset(&opt, 0, sizeof(opt));
    default_world_name(opt.world_name, sizeof(opt.world_name));
    strncpy(opt.ren_ming, settings ? settings->ren_ming : "赤", sizeof(opt.ren_ming) - 1);
    opt.height_cm = settings ? settings->profile.height_cm : 170;
    opt.play_mode = mode;

    opt.seed = 20261004u;
    snprintf(opt.seed_str, sizeof(opt.seed_str), "%u", opt.seed);
    opt.macro_size_idx  = 1;
    opt.macro_scale_idx = 3;
    opt.climate_rule    = CLIMATE_WIND_FIELD;
    opt.terrain_recipe  = TERRAIN_BALANCED;
    opt.monsoon_direction = 90;
    opt.axial_tilt_deg  = 23.5f;
    opt.prograde        = true;
    opt.astronomy_enabled = true;
    opt.astronomy_version = 1u;
    qlcg_apply_world_preset(&opt, WORLD_PRESET_HABITABLE);
    qlcg_sync_creator_dimensions(&opt);

    /* 玄虚归世界：流速由创建页决定，全局设置不再持有 */
    opt.creation_time_speed = 72.0f;
    (void)settings;
    opt.calendar_mode       = CALENDAR_DEFAULT;
    opt.start_year          = 2026;
    opt.start_month         = 10;
    opt.start_day           = 27;

    opt.artificial_enabled = true;
    opt.spawn_as_flag      = true;
    strncpy(opt.faction_name, "缀红", sizeof(opt.faction_name) - 1);
    opt.faction_main_color = settings ? settings->profile.main_color : 0xC2333FFFu;
    opt.faction_trim_color = settings ? settings->profile.trim_color : 0xE0B04AFFu;
    opt.initial_civ_limit  = 24;
    opt.natural_expansion  = 1.0f;
    opt.natural_evolution  = 1.0f;
    opt.flag_independence  = true;
    return opt;
}
