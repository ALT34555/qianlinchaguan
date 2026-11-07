#include "shui_model.h"

/* 大叶藻：耐寒宽带状沉水叶 */
void qlcg_plant_shui_dayezao(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_RIBBON, .form = 0, .cold = 0, .heat = 3,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.70,
        .blades = 30, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.016, .arch = 0.50,
        .bloom_form = SHUI_HEAD_NONE, .seed_form = SHUI_HEAD_NONE,
        .bloom_size = 0.05, .seed_size = 0.03,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.15;
        profile.spread_scale = 0.81;
        profile.blades = 25;
        profile.blade_len = 1.09;
        profile.blade_width = 0.013;
        profile.arch = 0.68;
        break;
    case 2:
        profile.height_scale = 0.86;
        profile.spread_scale = 1.28;
        profile.blades = 34;
        profile.blade_len = 0.92;
        profile.blade_width = 0.02;
        profile.arch = 0.37;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
