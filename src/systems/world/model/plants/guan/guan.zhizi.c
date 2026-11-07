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

void qlcg_plant_guan_zhizi(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_WOODY, .stems = 5, .forks = 4,
        .reach = .62, .bend = .20, .leaf = .30, .width = .56,
        .winter_leaf = .65, .cold = 3, .heat = 3,
        .bloom_season = PD_SEASON_SUMMER, .bloom_size = .13,
        .crown_height = 0.49, .leaf_sites = 12,
        .build_fruit = build_fruit,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.12;
        profile.spread_scale = 0.81;
        profile.stems = 4;
        profile.reach = 0.56;
        profile.bend = 0.31;
        profile.leaf_sites = 11;
        break;
    case 2:
        profile.height_scale = 0.88;
        profile.spread_scale = 1.27;
        profile.stems = 6;
        profile.reach = 0.7;
        profile.bend = 0.1;
        profile.leaf_sites = 13;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
