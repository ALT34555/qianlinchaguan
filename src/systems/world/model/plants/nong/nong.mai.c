#include "nong_model.h"

void qlcg_plant_nong_mai(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems;
    } shapes[] = {
        {1, 1, 5},
        {1.16, .82, 4},
        {.86, 1.22, 5},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_GRAIN, .produce = CROP_WHEAT, .leaf_form = CROP_LEAF_OVATE,
        .cold = 1, .heat = 2, .bloom_season = PD_SEASON_SPRING, .winter_leaf = .16,
        .stems = shapes[variant].stems,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .009, .leaf_len = .24, .leaf_w = .034, .fruit_size = .22,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
