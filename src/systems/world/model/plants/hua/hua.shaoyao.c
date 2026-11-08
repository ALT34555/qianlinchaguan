#include "hua_model.h"

void qlcg_plant_hua_shaoyao(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_HERB, .flower = HUA_DOUBLE, .bloom_season = PD_SEASON_SPRING,
        .stems = 5, .petals = 9, .rings = 2,
        .crown_base = 0, .rise = 0, .leaf_size = 0.55, .leaf_width = 0.59,
        .winter_leaf = 0, .bloom_size = 0.183,
        .seed = {HUA_FOLLICLES, 3, .088, 1.05, "#75523C"},
    };
    static const HuaShape shapes[] = {
        {"#E88AAA", 1.00, 1.00, 1.00, 0},
        {"#FFF4E9", 1.08, 0.89, 1.10, -1},
        {"#CC4E78", 0.89, 1.19, 0.92, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
