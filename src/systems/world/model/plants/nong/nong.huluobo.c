#include "nong_model.h"

void qlcg_plant_nong_huluobo(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 9, 0},
        {1.16, .82, 8, 0},
        {.86, 1.22, 11, 0},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_ROSETTE, .produce = CROP_CARROT, .leaf_form = CROP_LEAF_FEATHER,
        .cold = 1, .heat = 3, .bloom_season = PD_SEASON_COUNT, .winter_leaf = .38,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .011, .leaf_len = .33, .leaf_w = .108,
        .bloom_size = .069, .fruit_size = .063,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
