#include "guan_model.h"

void qlcg_plant_guan_guanggun(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_PENCIL, .stems = 6, .forks = 4,
        .reach = .54, .bend = .34, .leaf = .00, .width = .00,
        .winter_leaf = 0, .cold = 4, .heat = 0,
        .bloom_season = PD_SEASON_SUMMER, .bloom_size = .045,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.24;
        profile.spread_scale = 0.7;
        profile.stems = 5;
        profile.forks = 3;
        profile.reach = 0.45;
        profile.bend = 0.47;
        break;
    case 2:
        profile.height_scale = 0.84;
        profile.spread_scale = 1.28;
        profile.stems = 7;
        profile.forks = 5;
        profile.reach = 0.64;
        profile.bend = 0.24;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
