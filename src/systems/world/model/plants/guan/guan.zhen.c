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

void qlcg_plant_guan_zhen(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_CATKIN, .stems = 6, .forks = 3,
        .reach = .63, .bend = .37, .leaf = .29, .width = .76,
        .winter_leaf = 0, .cold = 1, .heat = 3,
        .bloom_season = PD_SEASON_SPRING, .bloom_size = .04,
        .crown_height = 0.73, .leaf_sites = 7,
        .build_fruit = build_fruit,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.17;
        profile.spread_scale = 0.78;
        profile.stems = 5;
        profile.reach = 0.55;
        profile.bend = 0.46;
        profile.leaf_sites = 6;
        break;
    case 2:
        profile.height_scale = 0.91;
        profile.spread_scale = 1.28;
        profile.stems = 7;
        profile.reach = 0.72;
        profile.bend = 0.25;
        profile.leaf_sites = 8;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
