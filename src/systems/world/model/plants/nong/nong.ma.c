#include "nong_model.h"

void qlcg_plant_nong_ma(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 4, 0},
        {1.16, .82, 3, 0},
        {.86, 1.22, 6, 0},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_FIBER_HERB, .produce = CROP_BAST, .leaf_form = CROP_LEAF_PALMATE,
        .cold = 3, .heat = 2, .bloom_season = PD_SEASON_SUMMER, .winter_leaf = .04,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .03, .leaf_len = .32, .leaf_w = .21,
        .bloom_size = .039, .fruit_size = .05, .fiber = 1,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
