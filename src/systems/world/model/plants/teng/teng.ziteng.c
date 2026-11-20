#include "teng_model.h"
#include <math.h>

/* 羽状复叶与渐细的下垂花序 */
static void leaflet(LpBuilder *b, const double at[3], double a, double len, LpRgb color) {
    double x = cos(a)*len, z = sin(a)*len;
    double left[3] = {at[0]+.19*z, at[1], at[2]-.19*x};
    double right[3] = {at[0]-.19*z, at[1], at[2]+.19*x};
    double tip[3] = {at[0]+x, at[1]+len*.105, at[2]+z};
    qlcg_lp_triangle(b, left, right, tip, color, LP_MAT_SOLID);
}

static void compound_leaf(LpBuilder *b, const TengShoot *p, const double at[3], double a, double len) {
    double c = cos(a), s = sin(a);
    double tip[3] = {at[0]+c*len, at[1]+len*.17, at[2]+s*len};
    qlcg_pa_vine_ribbon(b, at, tip, a, len*.024, p->stem);
    const int pairs = 2;
    for (int i = 0; i < pairs; i++) {
        double t = (.65 * i / pairs) + .17;
        double leaf[3] = {at[0]+c*len*t, at[1]+len*.17*t, at[2]+s*len*t};
        for (int side = -1; side <= 1; side += 2)
            leaflet(b, leaf, a+side*.98, len*(.72-.20*t), side<0 ? p->leaf : p->shade);
    }
    leaflet(b, tip, a, len*.43, p->leaf);
}

static void node(LpBuilder *b, const TengShoot *p, const double at[3], double a, int n) {
    if (p->bare) return;
    double len = p->span * (.21 + .028*(n % 3));
    compound_leaf(b, p, at, a, len);
    int flower = p->flowering && n % 2 == 0;
    int fruit = p->fruiting && n % 2 == 0;
    if (!flower && !fruit) return;
    double c = cos(a), s = sin(a);
    double drop = p->span * (.27 + .031*(n % 3));
    if (!p->hanging) drop = fmin(drop, at[1]*.87);
    double end[3] = {at[0]+c*len*.18, at[1]-drop, at[2]+s*len*.18};
    if (fruit) {
        qlcg_pa_vine_twig(b, at, end, len*.030, .39, p->stem);
        double rad = len*.13;
        double pod[3] = {end[0], end[1]+drop*.30, end[2]};
        qlcg_pa_vine_fruit(b, pod, (double[3]){rad, drop*.36, rad*.60}, p->fruit);
        return;
    }
    qlcg_pa_vine_ribbon(b, at, end, a, len*.026, p->stem);
    const int flowers = 4;
    for (int i = 0; i < flowers; i++) {
        double t = (i+.5)/flowers, dir = a + i*GOLDEN;
        double floret[3] = {at[0]+c*len*.18*t, at[1]-drop*t, at[2]+s*len*.18*t};
        double size = len * (.77-.50*t);
        LpRgb tone = qlcg_lp_lighten(p->bloom, .32*(1-t));
        qlcg_pa_folded_petal(b, floret, dir, size, size*1.10, .15, tone, LP_MAT_BLOOM);
        qlcg_pa_folded_petal(b, floret, dir+2.36, size*.92, size*.86, -.59,
                           p->bloom, LP_MAT_BLOOM);
    }
}

void qlcg_plant_teng_ziteng(LpBuilder *b, const PdDef *def, int season, int form) {
    static const TengHabit habit = {
        4, 8, PD_SEASON_SPRING, 2, .032, node,
        {{1, .77, .28, 1.06, GOLDEN, .46, 1, .09},
         {.9, 1.20, .38, 1.24, 1.02, .54, .80, .09},
         {1.28, .52, .23, .85, GOLDEN, .63, 1.28, .09}}
    };
    (void)form;
    qlcg_pa_vine(b, def, season, &habit);
}
