#include "nong_model.h"

/* 甘蔗：南方高秆糖料作物 */
void qlcg_plant_nong_ganzhe(LpBuilder *b, const PdDef *def, int season, int form) {
    CropProfile profile = {
        .habit = CROP_CANE, .cold = 3, .heat = 1,
        .bloom_season = PD_SEASON_AUTUMN, .winter_leaf = 0.15,
        .stems = 4, .stem_r = 0.045, .leaf_len = 0.95, .leaf_w = 0.05,
        .bloom_size = 0.35, .fruit_size = 0.15,
    };
    switch (qlcg_pa_crop_shape(def)) {
    case 1:
        profile.height_scale = 1.18;
        profile.spread_scale = 0.78;
        profile.stems = 3;
        break;
    case 2:
        profile.height_scale = 0.88;
        profile.spread_scale = 1.25;
        profile.stems = 6;
        break;
    }
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
