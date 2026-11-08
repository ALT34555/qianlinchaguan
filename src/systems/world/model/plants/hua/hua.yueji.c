#include "hua_model.h"

void qlcg_plant_hua_yueji(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_SHRUB, .flower = HUA_DOUBLE, .bloom_season = PD_SEASON_SUMMER,
        .stems = 5, .petals = 8, .rings = 3,
        .crown_base = 0.09, .rise = 0.86, .leaf_size = 0.33, .leaf_width = 0.49,
        .winter_leaf = 0.18, .bloom_size = 0.15,
        .seed = {HUA_HIP, 5, .077, 1.20, "#C35331"},
    };
    static const HuaShape shapes[] = {
        {"#D32B50", 1.00, 1.00, 1.00, 0},
        {"#F394AD", 1.12, 0.86, 1.12, -1},
        {"#F7C164", 0.90, 1.23, 0.91, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
