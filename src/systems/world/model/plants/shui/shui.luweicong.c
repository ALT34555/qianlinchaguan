#include "shui_model.h"

/* 芦苇丛：中密，秋穗压低 */
void qlcg_plant_shui_luweicong(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_REED, .form = 0, .cold = 2, .heat = 2,
        .bloom_season = PD_SEASON_AUTUMN, .winter_leaf = 0.62,
        .culms = 8, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.020, .arch = 1.00,
        .bloom_form = SHUI_HEAD_PANICLE, .seed_form = SHUI_HEAD_PANICLE,
        .bloom_size = 0.22, .seed_size = 0.12,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.14;
        profile.spread_scale = 0.79;
        profile.culms = 7;
        profile.blade_len = 1.1;
        profile.blade_width = 0.017;
        profile.bloom_size = 0.25;
        break;
    case 2:
        profile.height_scale = 0.88;
        profile.spread_scale = 1.27;
        profile.culms = 9;
        profile.blade_len = 0.9;
        profile.blade_width = 0.024;
        profile.bloom_size = 0.2;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
