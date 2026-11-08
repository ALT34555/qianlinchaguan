#include "hua_model.h"

void qlcg_plant_hua_mei(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_TREE, .flower = HUA_OPEN, .bloom_season = PD_SEASON_SPRING,
        .stems = 5, .petals = 5, .rings = 1,
        .crown_base = 0.31, .rise = 0.69, .leaf_size = 0.24, .leaf_width = 0.55,
        .winter_leaf = 0, .bloom_size = 0.22,
        .seed = {HUA_DRUPE, 1, .10, 1.02, "#A7A443"},
    };
    static const HuaShape shapes[] = {
        {"#C92237", 1.00, 1.00, 1.00, 0},
        {"#E88CA8", 1.08, 0.88, 1.06, -1},
        {"#FFF9F1", 0.91, 1.18, 0.93, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
