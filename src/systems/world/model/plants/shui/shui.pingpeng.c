#include "shui_model.h"

/* 萍蓬草：浮叶小，夏开黄花 */
void qlcg_plant_shui_pingpeng(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_FLOATING, .form = 0, .cold = 2, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.45,
        .pads = 10, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.030, .arch = 1.00,
        .pad_radius = 0.28, .float_y = 0.03,
        .bloom_form = SHUI_HEAD_UMBEL, .seed_form = SHUI_HEAD_NONE,
        .bloom_size = 0.12, .seed_size = 0.05,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.16;
        profile.spread_scale = 0.81;
        profile.pads = 7;
        profile.pad_radius = 0.33;
        profile.blade_width = 0.032;
        profile.bloom_size = 0.135;
        break;
    case 2:
        profile.height_scale = 0.87;
        profile.spread_scale = 1.29;
        profile.pads = 12;
        profile.pad_radius = 0.25;
        profile.blade_width = 0.028;
        profile.bloom_size = 0.105;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
