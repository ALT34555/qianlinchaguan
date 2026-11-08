#include "hua_model.h"

void qlcg_plant_hua_guanshanghe(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_LOTUS, .flower = HUA_DOUBLE, .bloom_season = PD_SEASON_SUMMER,
        .stems = 6, .petals = 9, .rings = 3,
        .crown_base = 0, .rise = 0, .leaf_size = 0, .leaf_width = 0,
        .winter_leaf = 0, .bloom_size = 0.198,
        .leaf_stem_width = .012, .flower_stem_width = .016,
        .seed = {HUA_RECEPTACLE, 7, .143, .88, "#8B983D"},
    };
    static const HuaShape shapes[] = {
        {"#EE93B0", 1.00, 1.00, 1.00, 0},
        {"#FFF8E8", 1.17, 0.88, 1.04, -1},
        {"#DE527D", 0.91, 1.22, 1.07, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
