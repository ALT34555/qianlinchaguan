#include "yao_model.h"

#include <math.h>

void qlcg_plant_yao_huaishanyao(LpBuilder *b, const PdDef *def, int season, int form) {
    YaoPlant p = qlcg_pa_yao_plant(def, season, form, PD_SEASON_SUMMER);
    int vines = p.simple || p.dry || p.seedling ? 1 : 3;
    int nodes = p.seedling ? 3 : p.simple ? 5 : 9;
    for (int i = 0; i < vines; i++) {
        double h = p.height * (1 - i * .13);
        double last[3] = {0, 0, 0};
        for (int j = 1; j <= nodes; j++) {
            double t = (double)j / nodes;
            double dir = p.phase + i * TAU / vines + t * 1.8 * TAU;
            double r = p.spread * (.13 + .48 * t);
            double at[3] = {cos(dir) * r, h * t, sin(dir) * r};
            qlcg_pa_yao_stalk(b, last, at, p.height * (.013 - .007 * t), .67, p.stem);
            if (!p.dry) {
                int leaves = j > nodes / 3 && !p.seedling ? 2 : 1;
                for (int k = 0; k < leaves; k++) {
                    double a = dir + k * PI;
                    double len = p.spread * (.87 - .27 * t) * p.leaf_scale;
                    double leaf_at[3] = {at[0] + cos(a) * len * .37, at[1] - len * .03,
                                         at[2] + sin(a) * len * .37};
                    qlcg_pa_yao_twig(b, at, leaf_at, len * .026, p.stem);
                    qlcg_pa_heart_leaf(b, leaf_at, a + .21, len, len * .68, len * (.39 - .18 * k),
                                      k ? p.shade : p.leaf, NULL);
                }
            }
            if (j % 2 == 0) {
                if (p.fruit) {
                    double bulb[3] = {at[0] + cos(dir) * r * .19, at[1] - p.spread * .055,
                                      at[2] + sin(dir) * r * .19};
                    qlcg_pa_yao_twig(b, at, bulb, p.spread * .009, p.stem);
                    qlcg_pa_yao_berry(b, bulb, p.spread * .065, 1.17, qlcg_pa_tone(def, "fruit", 1));
                }
                if (p.bloom) {
                    double size = p.spread * .055;
                    double tip[3] = {at[0] + cos(dir) * size * 3, at[1] - size * 4,
                                     at[2] + sin(dir) * size * 3};
                    LpRgb bloom = qlcg_pa_tone(def, "bloom", 1);
                    qlcg_pa_yao_twig(b, at, tip, size * .13, p.stem);
                    for (int k = 1; k <= 2; k++) {
                        double f = k * .5;
                        double floret[3] = {at[0] + (tip[0] - at[0]) * f, at[1] + (tip[1] - at[1]) * f,
                                            at[2] + (tip[2] - at[2]) * f};
                        qlcg_pa_yao_flower(b, floret, dir + k, 3, size, size * .55, .24, bloom);
                    }
                }
            }
            for (int axis = 0; axis < 3; axis++) last[axis] = at[axis];
        }
    }
}
