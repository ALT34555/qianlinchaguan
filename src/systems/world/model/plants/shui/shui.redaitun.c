#include "shui_model.h"

/* 热带茨藻：羽状沉水叶，畏寒 */
void qlcg_plant_shui_redaitun(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_RIBBON, .form = 1, .cold = 3, .heat = 1,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.15,
        .blades = 12, .spread = 0.0,
        .blade_len = 0.95, .blade_width = 0.008, .arch = 0.50,
        .bloom_form = SHUI_HEAD_NONE, .seed_form = SHUI_HEAD_NONE,
        .bloom_size = 0.05, .seed_size = 0.03,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.17;
        profile.spread_scale = 0.81;
        profile.blades = 10;
        profile.blade_len = 1.05;
        profile.blade_width = 0.006;
        break;
    case 2:
        profile.height_scale = 0.87;
        profile.spread_scale = 1.3;
        profile.blades = 14;
        profile.blade_len = 0.86;
        profile.blade_width = 0.01;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
