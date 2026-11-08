#include "nong_model.h"

void qlcg_plant_nong_cha(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 3, 9},
        {1.16, .82, 3, 8},
        {.86, 1.22, 4, 11},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_WOODY, .produce = CROP_CAPSULE, .leaf_form = CROP_LEAF_OVATE,
        .cold = 2, .heat = 2, .bloom_season = PD_SEASON_COUNT, .winter_leaf = .76,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .072, .leaf_len = .28, .leaf_w = .16,
        .bloom_size = .079, .fruit_size = .052,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
