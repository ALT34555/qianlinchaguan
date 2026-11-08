#include "hua_model.h"

void qlcg_plant_hua_tao(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_TREE, .flower = HUA_OPEN, .bloom_season = PD_SEASON_SPRING,
        .stems = 6, .petals = 5, .rings = 1,
        .crown_base = 0.38, .rise = 0.52, .leaf_size = 0.27, .leaf_width = 0.36,
        .winter_leaf = 0, .bloom_size = 0.205,
        .seed = {HUA_DRUPE, 2, .136, 1.03, "#E19A67"},
    };
    static const HuaShape shapes[] = {
        {"#E9989F", 1.00, 1.00, 1.00, 0},
        {"#EC78AB", 1.10, 0.88, 1.08, -1},
        {"#FFF8EB", 0.89, 1.20, 0.92, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
