#include "guan_model.h"
#include <math.h>

static void build_fruit(LpBuilder *b, const PdDef *def, const double point[3],
                        double spread, LpRng *rng) {
    double radius = qlcg_pa_pnum(def, "fruitRadius", 0);
    radius = radius > 0 ? qlcg_pa_clampd(radius, .02, .5)
                        : qlcg_pa_clampd(spread * .075, .035, .24);
    double pip[3] = {radius * .49, radius * .52, radius * .49};
    LpRgb color = qlcg_pa_tone(def, "fruit", 1);
    LpOpts opts = qlcg_lp_opts();
    opts.mat = LP_MAT_FRUIT;
    opts.rings = 0;
    double phase = qlcg_lp_rng_next(rng) * TAU;
    for (int i = 0; i < 6; i++) {
        double angle = phase + i * GOLDEN;
        double reach = radius * (i < 4 ? .49 : .25);
        double center[3] = {point[0] + cos(angle) * reach, point[1] + radius * (i < 4 ? .12 : -.45),
                            point[2] + sin(angle) * reach};
        qlcg_lp_ico(b, center, pip, 5, qlcg_pa_deepen(color, i * .035), &opts);
    }
}

void qlcg_plant_guan_xuangouzi(LpBuilder *b, const PdDef *def, int season, int form) {
    ShrubProfile profile = {
        .habit = SHRUB_BRAMBLE, .stems = 7, .forks = 4,
        .reach = 1.06, .bend = -.42, .leaf = .30, .width = .62,
        .winter_leaf = 0, .cold = 1, .heat = 3,
        .bloom_season = PD_SEASON_SUMMER, .bloom_size = .057,
        .crown_height = 0.33, .leaf_sites = 5,
        .build_fruit = build_fruit,
    };
    switch (qlcg_pa_shape_variant(def)) {
    case 1:
        profile.height_scale = 1.12;
        profile.spread_scale = 0.8;
        profile.stems = 6;
        profile.reach = 0.84;
        profile.bend = -0.31;
        profile.leaf_sites = 4;
        break;
    case 2:
        profile.height_scale = 0.84;
        profile.spread_scale = 1.26;
        profile.stems = 8;
        profile.reach = 1.08;
        profile.bend = -0.53;
        profile.leaf_sites = 6;
        break;
    }
    qlcg_pa_build_shrub(b, def, season, form, &profile);
}
