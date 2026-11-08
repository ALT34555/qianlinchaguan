#include "nong_model.h"

void qlcg_plant_nong_bailuobo(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 8, 0},
        {1.16, .82, 7, 0},
        {.86, 1.22, 10, 0},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_ROSETTE, .produce = CROP_RADISH, .leaf_form = CROP_LEAF_LOBED,
        .cold = 1, .heat = 3, .bloom_season = PD_SEASON_COUNT, .winter_leaf = .37,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .018, .leaf_len = .34, .leaf_w = .192,
        .bloom_size = .055, .fruit_size = .106,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
