#include "shui_model.h"

/* 蒲草：三棱高秆，夏抽蒲棒 */
void qlcg_plant_shui_pucao(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_SEDGE, .form = 0, .cold = 2, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.50,
        .culms = 8, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.022, .arch = 1.10,
        .bloom_form = SHUI_HEAD_SPIKE, .seed_form = SHUI_HEAD_SPIKE,
        .bloom_size = 0.12, .seed_size = 0.06,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.17;
        profile.spread_scale = 0.78;
        profile.culms = 6;
        profile.blade_len = 1.1;
        profile.blade_width = 0.019;
        break;
    case 2:
        profile.height_scale = 0.85;
        profile.spread_scale = 1.26;
        profile.culms = 9;
        profile.blade_len = 0.92;
        profile.blade_width = 0.026;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
