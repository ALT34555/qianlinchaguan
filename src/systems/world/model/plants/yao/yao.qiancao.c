#include "yao_model.h"

#include <math.h>

void qlcg_plant_yao_qiancao(LpBuilder *b, const PdDef *def, int season, int form) {
    YaoPlant p = qlcg_pa_yao_plant(def, season, form, PD_SEASON_AUTUMN);
    int stems = p.simple || p.seedling || p.dry ? 2 : 4;
    int whorls = p.simple || p.seedling ? 2 : 4;
    for (int i = 0; i < stems; i++) {
        double a = p.phase + i * GOLDEN;
        double x = cos(a), z = sin(a);
        double base[3] = {x * p.spread * .08, 0, z * p.spread * .08};
        double last[3] = {base[0], base[1], base[2]};
        double h = p.height * (.78 + .075 * i);
        double r = p.spread * (.63 + .13 * (i % 3));
        for (int n = 1; n <= whorls; n++) {
            double t = (double)n / whorls;
            double reach = r * t * t;
            double at[3] = {base[0] + x * reach, h * t * (1.19 - .28 * t), base[2] + z * reach};
            qlcg_pa_stem_between(b, last, at, p.height * (.019 - .008 * t), .76, p.stem);
            for (int j = 0; j < 4 && !p.dry; j++) {
                double dir = a + n * .67 + j * PI / 2;
                double len = p.spread * (.51 - .18 * t) * p.leaf_scale;
                double leaf_at[3] = {at[0] + cos(dir) * len * .21, at[1] + len * .11,
                                     at[2] + sin(dir) * len * .21};
                qlcg_pa_yao_twig(b, at, leaf_at, len * .016, p.stem);
                qlcg_pa_folded_petal(b, leaf_at, dir, len, len * .42, .23 + .16 * (j % 2) - .13 * t,
                                  j % 2 ? p.leaf : p.shade, LP_MAT_SOLID);
            }
            for (int axis = 0; axis < 3; axis++) last[axis] = at[axis];
        }
        if (!p.bloom && !p.fruit) continue;
        LpRgb color = qlcg_pa_tone(def, p.bloom ? "bloom" : "fruit", 1);
        int tips = p.simple ? 2 : 5;
        for (int j = 0; j < tips; j++) {
            double dir = a + j * GOLDEN;
            double reach = p.spread * (.09 + .06 * (j % 3));
            double at[3] = {last[0] + cos(dir) * reach, last[1] + h * (.08 + .025 * (j % 3)),
                            last[2] + sin(dir) * reach};
            qlcg_pa_yao_twig(b, last, at, h * .005, p.stem);
            if (p.bloom)
                qlcg_pa_yao_flower(b, at, dir, 5, reach * .60, reach * .26, .31, color);
            else
                qlcg_pa_yao_berry(b, at, p.spread * .048, .88, color);
        }
    }
}
