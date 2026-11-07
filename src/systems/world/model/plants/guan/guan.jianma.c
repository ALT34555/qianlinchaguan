#include "guan_model.h"

void qlcg_plant_guan_jianma(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_ROSETTE, .stems = 1, .forks = 1,
        .reach = .87, .bend = .00, .leaf = .00, .width = .00,
        .winter_leaf = .72, .cold = 4, .heat = 1,
        .bloom_season = -1, .bloom_size = .045,
        .rosette_leaves = 24,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.28;
        profile.spread_scale = 0.77;
        profile.rosette_leaves = 20;
        break;
    case 2:
        profile.height_scale = 0.82;
        profile.spread_scale = 1.28;
        profile.rosette_leaves = 27;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
