#include "nong_model.h"

void qlcg_plant_nong_su(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 7, 0},
        {1.16, .82, 6, 0},
        {.86, 1.22, 9, 0},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_GRAIN, .produce = CROP_MILLET, .leaf_form = CROP_LEAF_OVATE,
        .cold = 2, .heat = 1, .bloom_season = PD_SEASON_SUMMER, .winter_leaf = .03,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .014, .leaf_len = .31, .leaf_w = .048,
        .bloom_size = .045, .fruit_size = .23,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
