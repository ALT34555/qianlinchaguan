#include "../nong/nong_model.h"

void qlcg_plant_yao_yimi(LpBuilder *b, const PdDef *def, int season, int form) {
    static const struct {
        double height, spread;
        int stems;
    } shapes[] = {
        {1, 1, 4},
        {1.15, .86, 4},
        {.88, 1.18, 5},
    };
    unsigned variant = qlcg_pa_shape_variant(def);
    CropProfile profile = {
        .habit = CROP_GRAIN, .produce = CROP_COIX, .leaf_form = CROP_LEAF_OVATE,
        .cold = 2, .heat = 2, .bloom_season = PD_SEASON_SUMMER, .winter_leaf = .04,
        .stems = shapes[variant].stems,
        .height_scale = shapes[variant].height, .spread_scale = shapes[variant].spread,
        .stem_r = .012, .leaf_len = .43, .leaf_w = .064,
        .bloom_size = .052, .fruit_size = .23,
    };
    qlcg_pa_build_crop(b, def, season, form, &profile);
}
