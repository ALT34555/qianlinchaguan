#include "shui_model.h"

/* 热带睡莲：浮叶星花，畏寒 */
void qlcg_plant_shui_redaishuilian(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_LILY, .form = 0, .cold = 3, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.10,
        .pads = 10, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.040, .arch = 1.00,
        .pad_radius = 0.26, .float_y = 0.03,
        .bloom_form = SHUI_HEAD_UMBEL, .seed_form = SHUI_HEAD_NONE,
        .bloom_size = 0.16, .seed_size = 0.06,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.17;
        profile.spread_scale = 0.81;
        profile.pads = 8;
        profile.pad_radius = 0.3;
        profile.blade_width = 0.042;
        profile.bloom_size = 0.18;
        break;
    case 2:
        profile.height_scale = 0.86;
        profile.spread_scale = 1.28;
        profile.pads = 12;
        profile.pad_radius = 0.23;
        profile.blade_width = 0.037;
        profile.bloom_size = 0.14;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
