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

void qlcg_plant_guan_huanglu(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_PANICLE, .stems = 5, .forks = 4,
        .reach = .79, .bend = .17, .leaf = .24, .width = .89,
        .winter_leaf = 0, .cold = 2, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .bloom_size = .055,
        .crown_height = 0.46, .leaf_sites = 9,
        .build_fruit = build_fruit,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.13;
        profile.spread_scale = 0.82;
        profile.stems = 4;
        profile.reach = 0.7;
        profile.bend = 0.25;
        profile.leaf_sites = 8;
        break;
    case 2:
        profile.height_scale = 0.91;
        profile.spread_scale = 1.29;
        profile.stems = 6;
        profile.reach = 0.84;
        profile.bend = 0.05;
        profile.leaf_sites = 10;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
