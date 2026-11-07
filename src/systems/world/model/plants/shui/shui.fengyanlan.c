#include "shui_model.h"

/* 凤眼蓝：浮水莲座，夏抽穗花 */
void qlcg_plant_shui_fengyanlan(LpBuilder *b, const PdDef *def, int season, int form) {
    ShuiProfile profile = {
        .habit = SHUI_HYACINTH, .form = 0, .cold = 3, .heat = 1,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.10,
        .blades = 32, .spread = 0.0,
        .blade_len = 1.00, .blade_width = 0.050, .arch = 1.00,
        .bloom_form = SHUI_HEAD_SPIKE, .seed_form = SHUI_HEAD_NONE,
        .bloom_size = 0.10, .seed_size = 0.05,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.18;
        profile.spread_scale = 0.81;
        profile.blades = 27;
        profile.blade_width = 0.043;
        profile.arch = 1.21;
        break;
    case 2:
        profile.height_scale = 0.84;
        profile.spread_scale = 1.29;
        profile.blades = 36;
        profile.blade_width = 0.061;
        profile.arch = 0.8;
        break;
    }
    qlcg_pa_build_shui(b, def, season, form, &profile);
}
