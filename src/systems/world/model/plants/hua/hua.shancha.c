#include "hua_model.h"

void qlcg_plant_hua_shancha(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_SHRUB, .flower = HUA_DOUBLE, .bloom_season = PD_SEASON_SPRING,
        .stems = 6, .petals = 8, .rings = 2,
        .crown_base = 0.18, .rise = 0.73, .leaf_size = 0.43, .leaf_width = 0.68,
        .winter_leaf = 0.86, .bloom_size = 0.165,
        .seed = {HUA_CAPSULE, 3, .085, 1.17, "#826245"},
    };
    static const HuaShape shapes[] = {
        {"#C93E4D", 1.00, 1.00, 1.00, 0},
        {"#EF9BB0", 1.10, 0.90, 1.07, -1},
        {"#FFF8E6", 0.92, 1.16, 0.94, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
