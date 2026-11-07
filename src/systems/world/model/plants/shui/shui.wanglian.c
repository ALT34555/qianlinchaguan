#include "shui_model.h"

/* 王莲：巨型浮叶带立边，夏花 */
void qlcg_plant_shui_wanglian(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_FLOATING, .form = 2, .cold = 3, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.08,
        .pads = 9, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.050, .arch = 1.00,
        .pad_radius = 0.55, .float_y = 0.04,
        .bloom_form = SHUI_HEAD_UMBEL, .seed_form = SHUI_HEAD_NONE,
        .bloom_size = 0.20, .seed_size = 0.08,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.14;
        profile.spread_scale = 0.86;
        profile.pads = 7;
        profile.pad_radius = 0.61;
        profile.blade_width = 0.052;
        profile.bloom_size = 0.22;
        break;
    case 2:
        profile.height_scale = 0.88;
        profile.spread_scale = 1.26;
        profile.pads = 11;
        profile.pad_radius = 0.48;
        profile.blade_width = 0.045;
        profile.bloom_size = 0.18;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
