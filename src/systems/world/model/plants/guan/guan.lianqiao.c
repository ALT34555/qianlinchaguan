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

void qlcg_plant_guan_lianqiao(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_ARCHING, .stems = 6, .forks = 5,
        .reach = .93, .bend = -.32, .leaf = .18, .width = .36,
        .winter_leaf = 0, .cold = 1, .heat = 3,
        .bloom_season = PD_SEASON_SPRING, .bloom_size = .095,
        .crown_height = 0.78, .leaf_sites = 7,
        .build_fruit = build_fruit,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.16;
        profile.spread_scale = 0.83;
        profile.stems = 5;
        profile.reach = 0.8;
        profile.bend = -0.37;
        profile.leaf_sites = 7;
        break;
    case 2:
        profile.height_scale = 0.89;
        profile.spread_scale = 1.26;
        profile.stems = 7;
        profile.reach = 1;
        profile.bend = -0.56;
        profile.leaf_sites = 8;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
