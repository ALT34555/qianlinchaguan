#include "cao_model.h"

/* 芦荟：肉质莲座，夏抽高葶 */
void qlcg_plant_cao_luhui(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_ROSETTE, .cold = 4, .heat = 1,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.60,
        .leaves = 32, .leaf_len = 1.00, .leaf_width = 0.130,
        .arch = 1.00, .plump = 1.15, .leaf_form = HERB_LEAF_STRAP,
        .bloom_form = HERB_HEAD_RACEME, .fruit_form = HERB_HEAD_NONE,
        .bloom_size = 0.10, .fruit_size = 0.03,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.height_scale = 0.89;
        profile.spread_scale = 0.79;
        profile.leaves = 28;
        profile.leaf_width = 0.156;
        profile.arch = 1.25;
        break;
    case HERB_SHAPE_B:
        profile.height_scale = 1.02;
        profile.spread_scale = 1.32;
        profile.leaves = 37;
        profile.leaf_width = 0.112;
        profile.arch = 0.75;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
