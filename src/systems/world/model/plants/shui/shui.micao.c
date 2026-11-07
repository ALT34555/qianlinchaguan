#include "shui_model.h"

/* 盐沼米草：耐寒密丛，夏抽米穗 */
void qlcg_plant_shui_micao(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_REED, .form = 0, .cold = 0, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.78,
        .culms = 5, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.018, .arch = 1.00,
        .bloom_form = SHUI_HEAD_PANICLE, .seed_form = SHUI_HEAD_PANICLE,
        .bloom_size = 0.15, .seed_size = 0.09,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.17;
        profile.spread_scale = 0.76;
        profile.culms = 4;
        profile.blade_len = 1.11;
        profile.blade_width = 0.015;
        profile.bloom_size = 0.17;
        break;
    case 2:
        profile.height_scale = 0.85;
        profile.spread_scale = 1.31;
        profile.culms = 6;
        profile.blade_len = 0.92;
        profile.blade_width = 0.023;
        profile.bloom_size = 0.13;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
