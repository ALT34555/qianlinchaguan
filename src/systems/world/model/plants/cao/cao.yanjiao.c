#include "cao_model.h"

/* 盐角草：关节肉质茎，秋转红 */
void qlcg_plant_cao_yanjiao(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_SALICORNIA, .cold = 1, .heat = 3,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.22,
        .stems = 5, .leaf_len = 1.00, .leaf_width = 0.020,
        .arch = 1.00, .plump = 1.20, .leaf_form = HERB_LEAF_SCALE,
        .bloom_form = HERB_HEAD_SPIKE, .fruit_form = HERB_HEAD_SPIKE,
        .bloom_size = 0.04, .fruit_size = 0.02,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.height_scale = 1.22;
        profile.spread_scale = 0.76;
        profile.stems = 4;
        profile.leaf_width = 0.016;
        break;
    case HERB_SHAPE_B:
        profile.height_scale = 0.83;
        profile.spread_scale = 1.33;
        profile.stems = 6;
        profile.leaf_width = 0.027;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
