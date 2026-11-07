#include "shui_model.h"

/* 芦苇：疏秆，秋抽圆锥穗 */
void qlcg_plant_shui_luwei(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_REED, .form = 0, .cold = 2, .heat = 2,
        .bloom_season = PD_SEASON_AUTUMN, .winter_leaf = 0.60,
        .culms = 7, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.020, .arch = 1.00,
        .bloom_form = SHUI_HEAD_PANICLE, .seed_form = SHUI_HEAD_PANICLE,
        .bloom_size = 0.18, .seed_size = 0.10,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.19;
        profile.spread_scale = 0.73;
        profile.culms = 5;
        profile.blade_len = 1.1;
        profile.blade_width = 0.017;
        profile.bloom_size = 0.2;
        break;
    case 2:
        profile.height_scale = 0.87;
        profile.spread_scale = 1.27;
        profile.culms = 8;
        profile.blade_len = 0.89;
        profile.blade_width = 0.024;
        profile.bloom_size = 0.16;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
