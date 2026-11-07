#include "shui_model.h"

/* 莲：高低立叶，秋结莲蓬 */
void qlcg_plant_shui_lian(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_LOTUS, .form = 0, .cold = 2, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.35,
        .pads = 14, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.050, .arch = 1.00,
        .pad_radius = 0.30, .float_y = 0.0,
        .bloom_form = SHUI_HEAD_UMBEL, .seed_form = SHUI_HEAD_POD,
        .bloom_size = 0.20, .seed_size = 0.10,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.19;
        profile.spread_scale = 0.8;
        profile.pads = 11;
        profile.pad_radius = 0.34;
        profile.blade_width = 0.052;
        profile.bloom_size = 0.22;
        break;
    case 2:
        profile.height_scale = 0.85;
        profile.spread_scale = 1.3;
        profile.pads = 16;
        profile.pad_radius = 0.27;
        profile.blade_width = 0.045;
        profile.bloom_size = 0.18;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
