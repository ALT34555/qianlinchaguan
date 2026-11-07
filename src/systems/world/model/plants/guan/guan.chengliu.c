#include "guan_model.h"

void qlcg_plant_guan_chengliu(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_FEATHER, .stems = 5, .forks = 5,
        .reach = .72, .bend = -.38, .leaf = .22, .width = .09,
        .winter_leaf = 0, .cold = 2, .heat = 1,
        .bloom_season = PD_SEASON_SUMMER, .bloom_size = .035,
        .crown_height = 0.69, .leaf_sites = 13,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.15;
        profile.spread_scale = 0.79;
        profile.stems = 4;
        profile.reach = 0.65;
        profile.bend = -0.23;
        profile.leaf_sites = 11;
        break;
    case 2:
        profile.height_scale = 0.87;
        profile.spread_scale = 1.23;
        profile.stems = 6;
        profile.reach = 0.81;
        profile.bend = -0.51;
        profile.leaf_sites = 14;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
