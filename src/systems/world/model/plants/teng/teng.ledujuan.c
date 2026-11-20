#include "teng_model.h"
#include <math.h>

/* 带刺拱枝、三枚苞片与乳白小花 */
static void cluster(LpBuilder *b, const TengShoot *p, const double br[3], double a, double len) {
    LpRgb cream = qlcg_lp_hex("#fff3cd", 255, 243, 205);
    for (int i = 0; i < 3; i++) {
        double dir = a + i*TAU/3, scale = len*(.95+.09*(i%2));
        qlcg_pa_folded_petal(b, br, dir, scale, scale*.93, .39+.16*i,
                           qlcg_lp_lighten(p->bloom, .11*i), LP_MAT_BLOOM);
        double x = cos(dir)*len, z = sin(dir)*len;
        double heel[3] = {br[0]+x*.08+z*.051, br[1]+len*.16, br[2]+z*.08-x*.051};
        double tip[3] = {br[0]+x*.26, br[1]+len*.245, br[2]+z*.26};
        double edge[3] = {br[0]+x*.08-z*.051, br[1]+len*.16, br[2]+z*.08+x*.051};
        qlcg_lp_triangle(b, heel, edge, tip, cream, LP_MAT_BLOOM);
    }
}

static void node(LpBuilder *b, const TengShoot *p, const double at[3], double a, int n) {
    double len = p->span * (.18 + .024*(n % 3)), c = cos(a), s = sin(a);
    if (n % 2) {
        double thorn[3] = {at[0]-c*len*.29, at[1]+len*.23, at[2]-s*len*.29};
        double width = len*.052;
        qlcg_lp_triangle(b, at, thorn, (double[3]){at[0]+s*width, at[1], at[2]-c*width},
                         p->stem, LP_MAT_SOLID);
        qlcg_lp_triangle(b, at, (double[3]){at[0], at[1]+width, at[2]}, thorn,
                         p->stem, LP_MAT_SOLID);
    }
    if (p->bare) return;
    int flower = p->flowering && n > 1;
    int fruit = p->fruiting && n % 2 == 0;
    double br[3] = {at[0]+c*len*.62, at[1]+len*.44, at[2]+s*len*.62};
    int fork = n % 2 == 0 || flower;
    if (fork) qlcg_pa_vine_ribbon(b, at, br, a, len*.055, p->stem);
    qlcg_pa_folded_petal(b, at, a-.85, len, len*.61, .38, p->shade, LP_MAT_SOLID);
    qlcg_pa_folded_petal(b, fork ? br : at, a+.72, len*.86, len*.55, .12, p->leaf, LP_MAT_SOLID);
    if (flower) {
        cluster(b, p, br, a, len);
        if (n == 4) {
            double dir = a-1.4;
            double side[3] = {br[0]+cos(dir)*len*.94, br[1]+len*.30, br[2]+sin(dir)*len*.94};
            qlcg_pa_vine_ribbon(b, br, side, dir, len*.035, p->stem);
            cluster(b, p, side, dir, len*.86);
        }
    }
    if (fruit) qlcg_pa_vine_fruit(b, br, (double[3]){len*.09,len*.17,len*.09}, p->fruit);
}

void qlcg_plant_teng_ledujuan(LpBuilder *b, const PdDef *def, int season, int form) {
    static const TengHabit habit = {
        5, 7, PD_SEASON_SUMMER, 0, .025, node,
        {{1, .66, .76, .89, GOLDEN, .46, 1, .09},
         {.85, .89, 1.05, 1.04, 1.38, .35, .74, .09},
         {1.24, .49, .48, .65, GOLDEN, .65, 1.21, -.05}}
    };
    (void)form;
    qlcg_pa_vine(b, def, season, &habit);
}
