#include "nong_model.h"

void qlcg_plant_nong_sang(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 1, 7},
        {1.16, .82, 1, 6},
        {.86, 1.22, 1, 9},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_WOODY, .produce = CROP_MULBERRY, .leaf_form = CROP_LEAF_LOBED,
        .cold = 2, .heat = 2, .bloom_season = PD_SEASON_SPRING, .winter_leaf = .03,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .24, .leaf_len = .74, .leaf_w = .51,
        .bloom_size = .07, .fruit_size = .12,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
