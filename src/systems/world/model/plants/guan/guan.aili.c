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

void qlcg_plant_guan_aili(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_WOODY, .stems = 7, .forks = 4,
        .reach = 1.00, .bend = -0.18, .leaf = .12, .width = .80,
        .winter_leaf = 0, .cold = 0, .heat = 4,
        .bloom_season = -1, .bloom_size = .04,
        .crown_height = 0.38, .leaf_sites = 8,
        .build_fruit = build_fruit,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.17;
        profile.spread_scale = 0.79;
        profile.stems = 6;
        profile.reach = 0.92;
        profile.bend = -0.08;
        profile.leaf_sites = 7;
        break;
    case 2:
        profile.height_scale = 0.87;
        profile.spread_scale = 1.24;
        profile.stems = 8;
        profile.reach = 1.08;
        profile.bend = -0.3;
        profile.leaf_sites = 9;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
