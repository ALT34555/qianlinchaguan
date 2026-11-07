#include "shui_model.h"

/* 海三棱藨草：三棱茎，秋结小穗 */
void qlcg_plant_shui_biaocao(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_SEDGE, .form = 0, .cold = 1, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.50,
        .culms = 7, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.022, .arch = 1.00,
        .bloom_form = SHUI_HEAD_SPIKE, .seed_form = SHUI_HEAD_SPIKE,
        .bloom_size = 0.07, .seed_size = 0.04,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.18;
        profile.spread_scale = 0.76;
        profile.culms = 5;
        profile.blade_len = 1.12;
        profile.blade_width = 0.018;
        break;
    case 2:
        profile.height_scale = 0.86;
        profile.spread_scale = 1.28;
        profile.culms = 8;
        profile.blade_len = 0.9;
        profile.blade_width = 0.026;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
