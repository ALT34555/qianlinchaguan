#include "hua_model.h"

void qlcg_plant_hua_mujin(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_SHRUB, .flower = HUA_COLUMN, .bloom_season = PD_SEASON_SUMMER,
        .stems = 5, .petals = 5, .rings = 1,
        .crown_base = 0.22, .rise = 0.78, .leaf_size = 0.3, .leaf_width = 0.63,
        .winter_leaf = 0, .bloom_size = 0.18,
        .seed = {HUA_CAPSULE, 5, .073, 1.31, "#9B7043"},
    };
    static const HuaShape shapes[] = {
        {"#B68DCA", 1.00, 1.00, 1.00, 0},
        {"#F3C0CE", 1.13, 0.86, 1.06, -1},
        {"#FFF4F0", 0.91, 1.15, 0.98, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
