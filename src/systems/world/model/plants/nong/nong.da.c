#include "nong_model.h"

void qlcg_plant_nong_da(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 3, 0},
        {1.16, .82, 2, 0},
        {.86, 1.22, 4, 0},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_STEM_HERB, .produce = CROP_POD, .leaf_form = CROP_LEAF_TRIFOLIATE,
        .cold = 3, .heat = 2, .bloom_season = PD_SEASON_SUMMER, .winter_leaf = .07,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .021, .leaf_len = .25, .leaf_w = .21,
        .bloom_size = .041, .fruit_size = .082,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
