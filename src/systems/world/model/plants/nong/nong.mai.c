#include "nong_model.h"

void qlcg_plant_nong_mai(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 6, 0},
        {1.16, .82, 5, 0},
        {.86, 1.22, 8, 0},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_GRAIN, .produce = CROP_WHEAT, .leaf_form = CROP_LEAF_OVATE,
        .cold = 1, .heat = 2, .bloom_season = PD_SEASON_SPRING, .winter_leaf = .16,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .011, .leaf_len = .28, .leaf_w = .033,
        .bloom_size = .035, .fruit_size = .2,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
