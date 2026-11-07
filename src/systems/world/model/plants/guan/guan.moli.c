#include "guan_model.h"

static void build_fruit(LpBuilder *b, const PdDef *def, const double point[3],
                        double spread, LpRng *rng) {
    double radius = qlcg_pa_pnum(def, "fruitRadius", 0);
    radius = radius > 0 ? qlcg_pa_clampd(radius, .02, .5)
                        : qlcg_pa_clampd(spread * .075, .035, .24);
    double size[3] = {radius, radius, radius};
    LpOpts opts = qlcg_lp_opts();
    opts.mat = LP_MAT_FRUIT;
    opts.rings = 1;
    opts.jitter = .15;
    opts.rand = rng;
    qlcg_lp_ico(b, point, size, 5, qlcg_pa_tone(def, "fruit", 1), &opts);
}

void qlcg_plant_guan_moli(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_WOODY, .stems = 7, .forks = 3,
        .reach = .83, .bend = -.31, .leaf = .20, .width = .52,
        .winter_leaf = .35, .cold = 4, .heat = 3,
        .bloom_season = PD_SEASON_SUMMER, .bloom_size = .07,
        .crown_height = 0.45, .leaf_sites = 11,
        .build_fruit = build_fruit,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.1;
        profile.spread_scale = 0.81;
        profile.stems = 6;
        profile.reach = 0.73;
        profile.bend = -0.2;
        profile.leaf_sites = 10;
        break;
    case 2:
        profile.height_scale = 0.85;
        profile.spread_scale = 1.28;
        profile.stems = 8;
        profile.reach = 0.92;
        profile.bend = -0.42;
        profile.leaf_sites = 12;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
