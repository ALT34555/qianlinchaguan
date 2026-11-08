#include "hua_model.h"

void qlcg_plant_hua_ju(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_HERB, .flower = HUA_RAY, .bloom_season = PD_SEASON_AUTUMN,
        .stems = 6, .petals = 14, .rings = 2,
        .crown_base = 0, .rise = 0, .leaf_size = 0.41, .leaf_width = 0.58,
        .winter_leaf = 0, .bloom_size = 0.14,
        .seed = {HUA_ACHENES, 9, .075, .72, "#897341"},
    };
    static const HuaShape shapes[] = {
        {"#F3C02F", 1.00, 1.00, 1.00, 0},
        {"#F17E2D", 1.10, 0.86, 1.03, -1},
        {"#FFF9ED", 0.88, 1.19, 0.94, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
