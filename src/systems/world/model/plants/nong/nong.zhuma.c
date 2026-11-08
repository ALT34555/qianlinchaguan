#include "nong_model.h"

/* 苎麻：南方传统纤维作物 */
void qlcg_plant_nong_zhuma(LpBuilder *b, const PdDef *def, int season, int form) {
    CropProfile profile = {
        .habit = CROP_FIBER_HERB, .produce = CROP_BAST, .fiber = 1, .cold = 3, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.18,
        .stems = 5, .stem_r = 0.03, .leaf_len = 0.28, .leaf_w = 0.18,
        .bloom_size = 0.08, .fruit_size = 0.06,
    };
    switch (qlcg_pa_crop_shape(def)) {
    case 1:
        profile.height_scale = 1.16;
        profile.spread_scale = 0.80;
        profile.stems = 4;
        break;
    case 2:
        profile.height_scale = 0.86;
        profile.spread_scale = 1.26;
        profile.stems = 7;
        break;
    }
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
