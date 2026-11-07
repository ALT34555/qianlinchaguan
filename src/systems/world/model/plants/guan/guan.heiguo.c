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

void qlcg_plant_guan_heiguo(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_THORNY, .stems = 6, .forks = 4,
        .reach = .65, .bend = .08, .leaf = .115, .width = .30,
        .winter_leaf = 0, .cold = 1, .heat = 3,
        .bloom_season = PD_SEASON_SUMMER, .bloom_size = .047,
        .build_fruit = build_fruit,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.11;
        profile.spread_scale = 0.79;
        profile.stems = 5;
        profile.reach = 0.58;
        profile.bend = 0.17;
        profile.leaf_sites = 6;
        break;
    case 2:
        profile.height_scale = 0.85;
        profile.spread_scale = 1.27;
        profile.stems = 7;
        profile.reach = 0.75;
        profile.bend = -0.04;
        profile.leaf_sites = 8;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
