#include "cao_model.h"

/* 碱蓬：肉质分枝，秋红结籽 */
void qlcg_plant_cao_jianpeng(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_SUAEDA, .cold = 2, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.10,
        .stems = 6, .leaf_len = 1.00, .leaf_width = 0.020,
        .arch = 1.00, .plump = 1.20, .leaf_form = HERB_LEAF_SCALE,
        .bloom_form = HERB_HEAD_CYME, .fruit_form = HERB_HEAD_SPIKE,
        .bloom_size = 0.05, .fruit_size = 0.025,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.height_scale = 1.22;
        profile.spread_scale = 0.73;
        profile.stems = 5;
        profile.leaf_width = 0.016;
        break;
    case HERB_SHAPE_B:
        profile.height_scale = 0.82;
        profile.spread_scale = 1.36;
        profile.stems = 7;
        profile.leaf_width = 0.027;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
