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

void qlcg_plant_guan_yueju(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_WOODY, .stems = 9, .forks = 2,
        .reach = .71, .bend = .18, .leaf = .16, .width = .63,
        .winter_leaf = .79, .cold = 1, .heat = 4,
        .bloom_season = PD_SEASON_SPRING, .bloom_size = .045,
        .crown_height = 0.34, .leaf_sites = 12,
        .build_fruit = build_fruit,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.14;
        profile.spread_scale = 0.83;
        profile.stems = 7;
        profile.reach = 0.63;
        profile.bend = 0.3;
        profile.leaf_sites = 11;
        break;
    case 2:
        profile.height_scale = 0.87;
        profile.spread_scale = 1.24;
        profile.stems = 10;
        profile.reach = 0.79;
        profile.bend = 0.08;
        profile.leaf_sites = 13;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
