#include "yao_model.h"

#include <math.h>

void qlcg_plant_yao_gouqi(LpBuilder *b, const PdDef *def, int season, int form) {
    YaoPlant p = qlcg_pa_yao_plant(def, season, form, PD_SEASON_SUMMER);
    int stems = p.seedling ? 2 : p.simple ? 3 : 6;
    for (int i = 0; i < stems; i++) {
        double a = p.phase + i * GOLDEN;
        double x = cos(a), z = sin(a);
        double h = p.height * (.69 + .10 * (i % 4));
        double r = p.spread * (.85 + .12 * (i % 3));
        double base[3] = {x * r * .07, 0, z * r * .07};
        double shoulder[3] = {x * r * .33, h * .55, z * r * .33};
        qlcg_pa_yao_stalk(b, base, shoulder, p.height * .014, .72, p.stem);
        double prev[3] = {shoulder[0], shoulder[1], shoulder[2]};
        int nodes = p.simple || p.seedling ? 3 : 7;
        for (int j = 1; j <= nodes; j++) {
            double t = (double)j / nodes;
            double at[3] = {x * r * (.33 + .77 * t), h * (.55 + 1.27 * t - 1.06 * t * t),
                            z * r * (.33 + .77 * t)};
            qlcg_pa_yao_stalk(b, prev, at, p.height * (.011 - .007 * t), .81, p.stem);
            double dir = a + (j % 2 ? -1.11 : 1.11);
            double len = p.spread * (.37 - .12 * t) * p.leaf_scale;
            if (!p.dry) {
                qlcg_pa_folded_petal(b, at, dir, len, len * .40, .30, p.leaf, LP_MAT_SOLID);
                qlcg_pa_folded_petal(b, at, dir - 1.16, len * .81, len * .32, .47, p.shade, LP_MAT_SOLID);
            }
            if (j % 3 == 1 && !p.seedling) {
                double thorn[3] = {at[0] + cos(dir) * len * .4, at[1] + len * .29,
                                   at[2] + sin(dir) * len * .4};
                qlcg_pa_yao_twig(b, at, thorn, len * .04, p.stem);
            }
            if ((p.bloom || p.fruit) && j % 2 == 1) {
                double end[3] = {at[0] + cos(dir) * len * .33, at[1] - len * .28,
                                 at[2] + sin(dir) * len * .33};
                qlcg_pa_yao_twig(b, at, end, p.height * .004, p.stem);
                if (p.bloom)
                    qlcg_pa_yao_flower(b, end, dir, 5, len * .41, len * .23, .38,
                                       qlcg_pa_tone(def, "bloom", 1));
                else
                    qlcg_pa_yao_berry(b, end, p.spread * .047, 1.83, qlcg_pa_tone(def, "fruit", 1));
            }
            for (int axis = 0; axis < 3; axis++) prev[axis] = at[axis];
        }
    }
}
