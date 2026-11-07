#include "cao_model.h"

/* 蕨：林下羽状复叶，春卷拳芽 */
void qlcg_plant_cao_jue(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_FERN, .cold = 3, .heat = 3,
        .bloom_season = PD_SEASON_SPRING, .winter_leaf = 0.20,
        .leaves = 3, .leaf_len = 1.00, .leaf_width = 0.180,
        .arch = 1.00, .plump = 1.00, .leaf_form = HERB_LEAF_FERN,
        .bloom_form = HERB_HEAD_NONE, .fruit_form = HERB_HEAD_NONE,
        .bloom_size = 0.08, .fruit_size = 0.03,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.height_scale = 1.12;
        profile.spread_scale = 0.77;
        profile.leaves = 2;
        profile.arch = 1.18;
        break;
    case HERB_SHAPE_B:
        profile.height_scale = 0.90;
        profile.spread_scale = 1.29;
        profile.leaves = 4;
        profile.arch = 0.76;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
