#include "hua_model.h"

void qlcg_plant_hua_mudan(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_SHRUB, .flower = HUA_DOUBLE, .bloom_season = PD_SEASON_SPRING,
        .stems = 4, .petals = 11, .rings = 3,
        .crown_base = 0.18, .rise = 0.73, .leaf_size = 0.41, .leaf_width = 0.72,
        .winter_leaf = 0, .bloom_size = 0.235,
        .seed = {HUA_FOLLICLES, 5, .12, .93, "#8B643B"},
    };
    static const HuaShape shapes[] = {
        {"#DC2236", 1.00, 1.00, 1.00, 0},
        {"#AD3672", 1.08, 0.90, 1.12, -1},
        {"#8F102D", 0.91, 1.18, 0.98, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
