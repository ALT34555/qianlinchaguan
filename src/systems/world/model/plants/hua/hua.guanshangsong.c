#include "hua_model.h"

void qlcg_plant_hua_guanshangsong(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_PINE, .flower = HUA_OPEN, .bloom_season = PD_SEASON_SUMMER,
        .stems = 6, .petals = 5, .rings = 1,
        .crown_base = 0, .rise = 0, .leaf_size = 0, .leaf_width = 0,
        .winter_leaf = 0.83, .bloom_size = 0.1,
        .seed = {HUA_CONE, 11, .10, 1.95, "#8D6038"},
    };
    static const HuaShape shapes[] = {
        {"#356450", 1.00, 1.00, 1.00, 0},
        {"#517354", 1.17, 0.82, 1.00, -1},
        {"#235953", 0.89, 1.24, 1.00, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
