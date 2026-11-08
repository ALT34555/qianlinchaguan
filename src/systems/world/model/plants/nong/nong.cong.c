#include "nong_model.h"

void qlcg_plant_nong_cong(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 7, 2},
        {1.16, .82, 6, 2},
        {.86, 1.22, 9, 3},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_ALLIUM, .produce = CROP_ALLIUM_HEAD, .leaf_form = CROP_LEAF_TUBE,
        .cold = 1, .heat = 2, .bloom_season = PD_SEASON_COUNT, .winter_leaf = .49,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .014, .leaf_len = .45, .leaf_w = .045,
        .bloom_size = .084, .fruit_size = .06,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
