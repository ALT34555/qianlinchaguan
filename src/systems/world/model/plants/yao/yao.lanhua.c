#include "../hua/hua_model.h"

void qlcg_plant_yao_lanhua(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_ORCHID, .flower = HUA_LIP, .bloom_season = PD_SEASON_SPRING,
        .stems = 5, .petals = 5, .rings = 1,
        .winter_leaf = .56, .bloom_size = .115,
        .seed = {HUA_CAPSULE, 6, .041, 2.7, "#A6A066"},
    };
    static const HuaShape shapes[] = {
        {"#6089D6", 1, 1, 1, 0},
        {"#3A67BD", 1.13, .85, 1.08, -1},
        {"#78ACDA", .90, 1.18, 1.06, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
