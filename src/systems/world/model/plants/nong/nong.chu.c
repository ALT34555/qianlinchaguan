#include "nong_model.h"

void qlcg_plant_nong_chu(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 1, 6},
        {1.14, .78, 1, 5},
        {.88, 1.27, 1, 6},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_WOODY, .produce = CROP_BARK, .leaf_form = CROP_LEAF_LOBED,
        .cold = 2, .heat = 2, .bloom_season = PD_SEASON_SPRING, .winter_leaf = .04,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .17, .leaf_len = .63, .leaf_w = .46,
        .bloom_size = .08, .fruit_size = .12, .fiber = 1,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
