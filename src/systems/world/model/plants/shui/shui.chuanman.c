#include "shui_model.h"

/* 川蔓藻：羽状沉水叶，冬保残叶 */
void qlcg_plant_shui_chuanman(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_RIBBON, .form = 1, .cold = 1, .heat = 3,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.65,
        .blades = 16, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.008, .arch = 0.50,
        .bloom_form = SHUI_HEAD_NONE, .seed_form = SHUI_HEAD_NONE,
        .bloom_size = 0.05, .seed_size = 0.03,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.14;
        profile.spread_scale = 0.8;
        profile.blades = 13;
        profile.blade_len = 1.11;
        profile.blade_width = 0.007;
        break;
    case 2:
        profile.height_scale = 0.88;
        profile.spread_scale = 1.31;
        profile.blades = 19;
        profile.blade_len = 0.92;
        profile.blade_width = 0.01;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
