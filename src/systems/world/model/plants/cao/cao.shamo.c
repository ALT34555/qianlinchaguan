#include "cao_model.h"

/* 荒漠多肉：巨刺莲座，盛夏开花 */
void qlcg_plant_cao_shamo(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_ROSETTE, .cold = 3, .heat = 0,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.55,
        .leaves = 30, .leaf_len = 1.00, .leaf_width = 0.160,
        .arch = 1.00, .plump = 1.20, .leaf_form = HERB_LEAF_STRAP,
        .bloom_form = HERB_HEAD_RACEME, .fruit_form = HERB_HEAD_NONE,
        .bloom_size = 0.12, .fruit_size = 0.04,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.height_scale = 1.09;
        profile.spread_scale = 0.80;
        profile.leaves = 25;
        profile.leaf_width = 0.128;
        profile.arch = 1.23;
        break;
    case HERB_SHAPE_B:
        profile.height_scale = 0.85;
        profile.spread_scale = 1.30;
        profile.leaves = 35;
        profile.leaf_width = 0.191;
        profile.arch = 0.77;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
