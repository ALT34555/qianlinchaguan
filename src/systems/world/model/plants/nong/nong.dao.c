#include "nong_model.h"

void qlcg_plant_nong_dao(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems;
    } shapes[] = {
        {1, 1, 5},
        {1.13, .81, 4},
        {.91, 1.23, 5},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_GRAIN, .produce = CROP_RICE, .leaf_form = CROP_LEAF_OVATE,
        .cold = 2, .heat = 1, .bloom_season = PD_SEASON_SUMMER, .winter_leaf = .04,
        .stems = shapes[variant].stems,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .008, .leaf_len = .40, .leaf_w = .044, .fruit_size = .245,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
