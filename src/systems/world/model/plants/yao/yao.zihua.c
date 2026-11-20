#include "../hua/hua_model.h"

void qlcg_plant_yao_zihua(LpBuilder *b, const PdDef *def, int season, int form) {
    static const HuaProfile profile = {
        .habit = HUA_HERB, .flower = HUA_RAY, .bloom_season = PD_SEASON_AUTUMN,
        .stems = 6, .petals = 15, .rings = 2,
        .leaf_size = .64, .leaf_width = .70, .bloom_size = .155,
        .seed = {HUA_ACHENES, 8, .063, .71, "#8E7258"},
    };
    static const HuaShape shapes[] = {
        {"#A350BA", 1, 1, 1, 0},
        {"#784BAB", 1.16, .86, 1.13, -1},
        {"#C370C6", .86, 1.16, .95, 1},
    };
    qlcg_pa_build_ornamental(b, def, season, form, &profile, &shapes[qlcg_pa_shape_variant(def)]);
}
