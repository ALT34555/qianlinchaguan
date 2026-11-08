#include "nong_model.h"

void qlcg_plant_nong_jiu(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 10, 2},
        {1.16, .82, 8, 2},
        {.86, 1.22, 13, 3},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_ALLIUM, .produce = CROP_ALLIUM_HEAD, .leaf_form = CROP_LEAF_FLAT,
        .cold = 1, .heat = 2, .bloom_season = PD_SEASON_COUNT, .winter_leaf = .3,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .008, .leaf_len = .31, .leaf_w = .033,
        .bloom_size = .073, .fruit_size = .045,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
