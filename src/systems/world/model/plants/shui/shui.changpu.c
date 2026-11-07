#include "shui_model.h"

/* 水菖蒲：剑叶挺水，春抽肉穗 */
void qlcg_plant_shui_changpu(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_SWORD, .form = 0, .cold = 1, .heat = 2,
        .bloom_season = PD_SEASON_SPRING, .winter_leaf = 0.55,
        .blades = 24, .spread = 0.0,
        .blade_len = 1.05, .blade_width = 0.035, .arch = 1.05,
        .bloom_form = SHUI_HEAD_SPADIX, .seed_form = SHUI_HEAD_SPADIX,
        .bloom_size = 0.10, .seed_size = 0.05,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.16;
        profile.spread_scale = 0.81;
        profile.blades = 20;
        profile.blade_len = 1.12;
        profile.blade_width = 0.029;
        profile.arch = 1.2;
        break;
    case 2:
        profile.height_scale = 0.87;
        profile.spread_scale = 1.27;
        profile.blades = 28;
        profile.blade_len = 0.94;
        profile.blade_width = 0.043;
        profile.arch = 0.84;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
