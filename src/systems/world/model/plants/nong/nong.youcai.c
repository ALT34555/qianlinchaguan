#include "nong_model.h"

/* 油菜：南方越冬油料作物 */
void qlcg_plant_nong_youcai(LpBuilder *b, const PdDef *def, int season, int form) {
    CropProfile profile = {
        .habit = CROP_BRASSICA, .cold = 2, .heat = 3,
        .bloom_season = PD_SEASON_SPRING, .winter_leaf = 0.65,
        .stems = 1, .branches = 5, .stem_r = 0.025,
        .leaf_len = 0.35, .leaf_w = 0.12,
        .bloom_size = 0.045, .fruit_size = 0.08,
    };
    switch (qlcg_pa_crop_shape(def)) {
    case 1:
        profile.height_scale = 1.15;
        profile.spread_scale = 0.82;
        profile.branches = 4;
        break;
    case 2:
        profile.height_scale = 0.85;
        profile.spread_scale = 1.28;
        profile.branches = 7;
        break;
    }
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
