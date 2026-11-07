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

void qlcg_plant_guan_dujuan(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_WOODY, .stems = 5, .forks = 3,
        .reach = .76, .bend = .11, .leaf = .24, .width = .42,
        .winter_leaf = .52, .cold = 2, .heat = 3,
        .bloom_season = PD_SEASON_SPRING, .bloom_size = .12,
        .crown_height = 0.47, .leaf_sites = 10,
        .build_fruit = build_fruit,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.14;
        profile.spread_scale = 0.82;
        profile.stems = 4;
        profile.reach = 0.7;
        profile.bend = 0.22;
        profile.leaf_sites = 9;
        break;
    case 2:
        profile.height_scale = 0.87;
        profile.spread_scale = 1.25;
        profile.stems = 6;
        profile.reach = 0.84;
        profile.bend = 0.02;
        profile.leaf_sites = 11;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
