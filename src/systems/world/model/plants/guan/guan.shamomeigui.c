#include "guan_model.h"

void qlcg_plant_guan_shamomeigui(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_CAUDEX, .stems = 5, .forks = 2,
        .reach = .57, .bend = .23, .leaf = .25, .width = .35,
        .winter_leaf = .06, .cold = 4, .heat = 1,
        .bloom_season = PD_SEASON_SUMMER, .bloom_size = .15,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.2;
        profile.spread_scale = 0.78;
        profile.stems = 4;
        profile.reach = 0.52;
        profile.bend = 0.34;
        profile.leaf_sites = 6;
        break;
    case 2:
        profile.height_scale = 0.84;
        profile.spread_scale = 1.26;
        profile.stems = 6;
        profile.reach = 0.64;
        profile.bend = 0.13;
        profile.leaf_sites = 8;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
