#include "hua_model.h"

void qlcg_plant_hua_lan(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_ORCHID, .flower = HUA_LIP, .bloom_season = PD_SEASON_SPRING,
        .stems = 5, .petals = 5, .rings = 1,
        .crown_base = 0, .rise = 0, .leaf_size = 0, .leaf_width = 0,
        .winter_leaf = 0.45, .bloom_size = 0.093,
        .seed = {HUA_CAPSULE, 6, .037, 3.05, "#B6AD62"},
    };
    static const HuaShape shapes[] = {
        {"#FFF9E9", 1.00, 1.00, 1.00, 0},
        {"#F8DE82", 1.14, 0.88, 0.96, -1},
        {"#E68EB4", 0.92, 1.20, 1.08, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
