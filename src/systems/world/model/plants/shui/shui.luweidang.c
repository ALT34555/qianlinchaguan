#include "shui_model.h"

/* 芦苇荡：走茎密丛，冬留枯秆 */
void qlcg_plant_shui_luweidang(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_REED, .form = 0, .cold = 2, .heat = 2,
        .bloom_season = PD_SEASON_AUTUMN, .winter_leaf = 0.65,
        .culms = 10, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.020, .arch = 1.00,
        .bloom_form = SHUI_HEAD_PANICLE, .seed_form = SHUI_HEAD_PANICLE,
        .bloom_size = 0.24, .seed_size = 0.13,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.13;
        profile.spread_scale = 0.82;
        profile.culms = 8;
        profile.blade_len = 1.06;
        profile.blade_width = 0.017;
        profile.bloom_size = 0.27;
        break;
    case 2:
        profile.height_scale = 0.91;
        profile.spread_scale = 1.32;
        profile.culms = 11;
        profile.blade_len = 0.92;
        profile.blade_width = 0.023;
        profile.bloom_size = 0.21;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
