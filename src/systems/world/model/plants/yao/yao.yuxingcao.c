#include "yao_model.h"

#include <math.h>

void qlcg_plant_yao_yuxingcao(LpBuilder *b, const PdDef *def, int season, int form) {
    YaoPlant p = qlcg_pa_yao_plant(def, season, form, PD_SEASON_SUMMER);
    const double root[3] = {0, 0, 0};
    int stems = p.seedling || p.simple ? 3 : p.dry ? 4 : 7;
    LpRgb underside = p.dry ? p.shade : qlcg_lp_wind_color(qlcg_lp_rgb(102, 64, 81), LP_WIND_LEAF);
    for (int i = 0; i < stems; i++) {
        double a = p.phase + i * GOLDEN;
        double x = cos(a), z = sin(a);
        double r = p.spread * (.34 + .17 * (i % 4));
        double h = p.height * (.56 + .135 * (i % 4));
        double base[3] = {x * r * .65, h * .04, z * r * .65};
        double top[3] = {x * r, h, z * r};
        qlcg_pa_stem_between(b, root, base, p.height * .028, .77, p.stem);
        qlcg_pa_stem_between(b, base, top, p.height * .021, .44, p.stem);
        int leaves = p.dry ? 1 : p.seedling ? 2 : 3;
        for (int j = 0; j < leaves; j++) {
            double t = .40 + j * .23;
            double dir = a + (j - 1.0) * 1.58;
            double len = p.spread * (.69 - .10 * j) * p.leaf_scale;
            if (p.dry) len *= .53;
            double node[3] = {x * r * (.65 + .35 * t), h * (.04 + .96 * t), z * r * (.65 + .35 * t)};
            double at[3] = {node[0] + cos(dir) * len * .56, node[1] + len * .25,
                            node[2] + sin(dir) * len * .56};
            qlcg_pa_stem_between(b, node, at, len * .025, .54, p.stem);
            qlcg_pa_heart_leaf(b, at, dir, len, len * 1.08, len * (p.dry ? -.24 : .14 + .14 * j),
                              j % 2 ? p.shade : p.leaf, &underside);
        }
        if (i % 2 != 0 || (!p.bloom && !p.fruit)) continue;
        double size = p.height * .21;
        if (p.bloom) {
            qlcg_pa_yao_flower(b, top, a, 4, size, size * .78, .31, qlcg_pa_tone(def, "bloom", 1));
            LpOpts o = qlcg_lp_opts();
            o.mat = LP_MAT_BLOOM;
            o.radius_ratio = .65;
            qlcg_lp_cone(b, top, size * .18, size * .18, size * .63, 5,
                          qlcg_lp_rgb(212, 184, 91), &o);
        } else {
            qlcg_pa_yao_berry(b, top, size * .20, 2.25, qlcg_pa_tone(def, "fruit", 1));
        }
    }
}
