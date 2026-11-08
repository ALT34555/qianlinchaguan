#include "nong_model.h"

void qlcg_plant_nong_huma(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 2, 0},
        {1.16, .82, 1, 0},
        {.86, 1.22, 3, 0},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_STEM_HERB, .produce = CROP_SESAME, .leaf_form = CROP_LEAF_OVATE,
        .cold = 3, .heat = 1, .bloom_season = PD_SEASON_SUMMER, .winter_leaf = .04,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .024, .leaf_len = .25, .leaf_w = .14,
        .bloom_size = .065, .fruit_size = .042,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
