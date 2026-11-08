#include "nong_model.h"

void qlcg_plant_nong_xie(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 13, 2},
        {1.16, .82, 11, 2},
        {.86, 1.22, 16, 3},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_ALLIUM, .produce = CROP_ALLIUM_HEAD, .leaf_form = CROP_LEAF_KEEL,
        .cold = 2, .heat = 2, .bloom_season = PD_SEASON_COUNT, .winter_leaf = .33,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .009, .leaf_len = .35, .leaf_w = .026,
        .bloom_size = .062, .fruit_size = .042,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
