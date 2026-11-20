#include "teng_model.h"
#include <math.h>

/* 浅喉、五条折线与外翻花冠 */
static void trumpet(LpBuilder *b, const TengShoot *p, const double at[3], double a, double size) {
    double c = cos(a), s = sin(a);
    double throat[5][3], rim[10][3];
    LpRgb cream = qlcg_lp_hex("#fff0d6", 255, 240, 214);
    LpRgb pale = qlcg_lp_mix(p->bloom, cream, .43);
    for (int i = 0; i < 10; i++) {
        double phi = TAU*i/10, lateral = cos(phi), vertical = sin(phi);
        double radius = size*(i % 2 ? .91 : 1.05);
        double forward = size*(i % 2 ? .91 : 1.0);
        if (i % 2 == 0) {
            double *inner = throat[i/2];
            inner[0] = at[0] + size*(.29*c - .29*s*lateral - .22*c*vertical);
            inner[1] = at[1] + size*(.28 + .23*vertical);
            inner[2] = at[2] + size*(.29*s + .29*c*lateral - .22*s*vertical);
        }
        rim[i][0] = at[0] + forward*c + radius*(-s*lateral - .70*c*vertical);
        rim[i][1] = at[1] + forward*.97 + radius*.71*vertical;
        rim[i][2] = at[2] + forward*s + radius*(c*lateral - .70*s*vertical);
    }
    for (int i = 0; i < 5; i++) {
        int j = (i+1)%5;
        const double *crest = rim[2*i], *valley = rim[2*i+1];
        qlcg_lp_triangle(b, at, throat[i], throat[j], cream, LP_MAT_BLOOM);
        qlcg_lp_triangle(b, throat[i], crest, valley, p->bloom, LP_MAT_BLOOM);
        qlcg_lp_triangle(b, throat[j], valley, rim[2*j], p->bloom, LP_MAT_BLOOM);
        qlcg_lp_triangle(b, throat[i], valley, throat[j], pale, LP_MAT_BLOOM);
    }
}

static void heart_leaf(LpBuilder *b, const TengShoot *p, const double at[3], double a,
                        double length, double rise) {
    static const double edge[][3] = {
        {0, 0, 0}, {-.12, .33, .07}, {.23, .49, .28}, {1, 0, 1},
        {.23, -.49, .28}, {-.12, -.33, .07}
    };
    double x = cos(a)*length, z = sin(a)*length;
    double center[3] = {at[0]+x*.31, at[1]+rise*.47+length*.13, at[2]+z*.31};
    double outline[6][3];
    for (int i = 0; i < 6; i++) {
        outline[i][0] = at[0] + edge[i][0]*x - edge[i][1]*z;
        outline[i][1] = at[1] + edge[i][2]*rise;
        outline[i][2] = at[2] + edge[i][0]*z + edge[i][1]*x;
    }
    for (int i = 0; i < 6; i++)
        qlcg_lp_triangle(b, center, outline[i], outline[(i+1)%6],
                         i < 3 ? p->leaf : qlcg_lp_mix(p->leaf, p->shade, .45), LP_MAT_SOLID);
}

static void node(LpBuilder *b, const TengShoot *p, const double at[3], double a, int n) {
    if (p->bare) return;
    double len = p->span * (.17 + .026*(n % 3));
    heart_leaf(b, p, at, a, len*1.14, len*(.23+.17*(n%3)));
    int flower = p->flowering && n % 2 == 0;
    int fruit = p->fruiting && n % 2 == 0;
    if (!flower && !fruit) return;
    double dir = a + 1.88, r = len*.58;
    double bud[3] = {at[0] + cos(dir)*r*.80, at[1] + r*.86, at[2] + sin(dir)*r*.80};
    if (flower) {
        qlcg_pa_vine_ribbon(b, at, bud, dir, r*.073, p->stem);
        trumpet(b, p, bud, dir, r);
    }
    if (fruit) {
        qlcg_pa_vine_twig(b, at, bud, r*.073, .75, p->stem);
        qlcg_pa_vine_fruit(b, bud, (double[3]){r*.32,r*.40,r*.32}, p->fruit);
    }
}

void qlcg_plant_teng_qianniuhua(LpBuilder *b, const PdDef *def, int season, int form) {
    static const TengHabit habit = {
        4, 9, PD_SEASON_SUMMER, 0, .009, node,
        {{1, .56, .28, 1.12, GOLDEN, .46, 1, .09},
         {1.22, .35, .18, .82, GOLDEN, .28, 1.28, -.07},
         {.83, 1.04, .48, 1.32, .58, .78, .72, .09}}
    };
    (void)form;
    qlcg_pa_vine(b, def, season, &habit);
}
