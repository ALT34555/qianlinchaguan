#include "cao_model.h"

/* 罗布麻：直立纤维茎，秋垂果荚 */
void qlcg_plant_cao_luobuma(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_FORB, .cold = 3, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.12,
        .stems = 4, .leaf_len = 1.00, .leaf_width = 0.045,
        .arch = 1.05, .plump = 1.00, .leaf_form = HERB_LEAF_ROUND,
        .bloom_form = HERB_HEAD_CYME, .fruit_form = HERB_HEAD_CYME,
        .bloom_size = 0.06, .fruit_size = 0.03,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.height_scale = 1.23;
        profile.spread_scale = 0.74;
        profile.stems = 3;
        profile.leaf_width = 0.036;
        break;
    case HERB_SHAPE_B:
        profile.height_scale = 0.85;
        profile.spread_scale = 1.32;
        profile.stems = 5;
        profile.leaf_width = 0.055;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
