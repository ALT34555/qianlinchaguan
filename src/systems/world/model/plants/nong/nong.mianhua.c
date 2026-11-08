#include "nong_model.h"

/* 棉花：南方分枝经济作物 */
void qlcg_plant_nong_mianhua(LpBuilder *b, const PdDef *def, int season, int form) {
    CropProfile profile = {
        .habit = CROP_BOLL_SHRUB, .fiber = 1, .cold = 3, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.05,
        .stems = 1, .branches = 6, .stem_r = 0.035,
        .leaf_len = 0.22, .leaf_w = 0.20,
        .bloom_size = 0.09, .fruit_size = 0.10,
    };
    switch (qlcg_pa_crop_shape(def)) {
    case 1:
        profile.height_scale = 1.15;
        profile.spread_scale = 0.82;
        profile.branches = 5;
        break;
    case 2:
        profile.height_scale = 0.88;
        profile.spread_scale = 1.25;
        profile.branches = 8;
        break;
    }
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
