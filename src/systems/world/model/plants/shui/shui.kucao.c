#include "shui_model.h"

/* 苦草：沉水带叶，冬季少叶 */
void qlcg_plant_shui_kucao(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_RIBBON, .form = 0, .cold = 1, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.45,
        .blades = 30, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.012, .arch = 0.55,
        .bloom_form = SHUI_HEAD_NONE, .seed_form = SHUI_HEAD_NONE,
        .bloom_size = 0.05, .seed_size = 0.03,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.21;
        profile.spread_scale = 0.78;
        profile.blades = 25;
        profile.blade_len = 1.09;
        profile.blade_width = 0.01;
        profile.arch = 0.76;
        break;
    case 2:
        profile.height_scale = 0.88;
        profile.spread_scale = 1.25;
        profile.blades = 35;
        profile.blade_len = 0.92;
        profile.blade_width = 0.015;
        profile.arch = 0.4;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
