#include "nong_model.h"

void qlcg_plant_nong_song(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems, branches;
    } shapes[] = {
        {1, 1, 13, 0},
        {1.16, .82, 12, 0},
        {.86, 1.22, 16, 0},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_ROSETTE, .produce = CROP_SILIQUE, .leaf_form = CROP_LEAF_CUP,
        .cold = 1, .heat = 3, .bloom_season = PD_SEASON_COUNT, .winter_leaf = .57,
        .stems = shapes[variant].stems, .branches = shapes[variant].branches,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .017, .leaf_len = .3, .leaf_w = .245,
        .bloom_size = .052, .fruit_size = .051,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
