#include "teng_model.h"

#include <math.h>
#include <string.h>

void qlcg_pa_vine_twig(LpBuilder *b, const double from[3], const double to[3],
                       double radius, double taper, LpRgb color) {
    double direction[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    double length = js_hypot3(direction[0], direction[1], direction[2]);
    if (length < 1e-6) return;
    for (int axis = 0; axis < 3; axis++) direction[axis] /= length;
    LpOpts opts = qlcg_lp_opts();
    opts.sides = 3;
    opts.cap = 0;
    opts.tip_ratio = taper;
    qlcg_lp_limb(b, from, direction, length, radius, color, &opts);
}

void qlcg_pa_vine_ribbon(LpBuilder *b, const double from[3], const double to[3],
                         double bearing, double radius, LpRgb color) {
    double x = sin(bearing)*radius, z = cos(bearing)*radius;
    double left[3] = {from[0]+x, from[1], from[2]-z};
    double right[3] = {from[0]-x, from[1], from[2]+z};
    double upper[3] = {from[0], from[1]+radius, from[2]};
    double lower[3] = {from[0], from[1]-radius, from[2]};
    qlcg_lp_triangle(b, left, to, right, color, LP_MAT_SOLID);
    qlcg_lp_triangle(b, upper, lower, to, color, LP_MAT_SOLID);
}

void qlcg_pa_vine_fruit(LpBuilder *b, const double at[3], const double radii[3], LpRgb color) {
    LpOpts o = qlcg_lp_opts();
    o.mat = LP_MAT_FRUIT;
    qlcg_lp_ico(b, at, radii, 5, color, &o);
    qlcg_lp_socket(b, at, LP_SOCKET_FRUIT);
}

void qlcg_pa_vine(LpBuilder *b, const PdDef *def, int season, const TengHabit *habit) {
    unsigned variant = qlcg_pa_shape_variant(def);
    const TengForm *form = &habit->forms[variant];
    int seedling = def->state == PD_STATE_SEEDLING;
    TengShoot shoot = {0};
    shoot.hanging = def->state == PD_STATE_HANGING;
    int mature = shoot.hanging || def->state == PD_STATE_NORMAL;
    shoot.bare = def->state == PD_STATE_WITHERED;
    shoot.flowering = def->bloom && (def->state == PD_STATE_FLOWERING ||
                      (mature && season == habit->bloom_season));
    shoot.fruiting = def->fruit && (def->state == PD_STATE_FRUITING ||
                     (mature && season == PD_SEASON_AUTUMN && !shoot.flowering));
    shoot.span = qlcg_pa_pnum(def, "spread", 1);
    shoot.stem = qlcg_pa_tone(def, shoot.bare ? "witheredStem" : "barkLight", 1);
    shoot.leaf = qlcg_pa_tone(def, "foliage", 1);
    shoot.shade = qlcg_pa_tone(def, "foliageDark", 1);
    if (shoot.flowering) shoot.bloom = qlcg_pa_tone(def, "bloom", 1);
    if (shoot.fruiting) shoot.fruit = qlcg_pa_tone(def, "fruit", 1);
    double h = qlcg_pa_pnum(def, "height", .5);
    int canes = seedling ? 1 : habit->canes;
    int nodes = seedling ? 3 : habit->nodes;
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    double phase = qlcg_lp_rng_next(&rng) * TAU + variant * .82;
    for (int i = 0; i < canes; i++) {
        double a = phase + form->bearing_step * i, vigor = 1 - .14 * (i % 3);
        double radius = shoot.span * habit->thickness * vigor;
        double previous[3] = {0}, previous_twist[3] = {0};
        for (int n = 1; n <= nodes; n++) {
            double t = (double)n / nodes;
            double angle = a + form->bend * sin(t * 5.4 + i) + .37 * t;
            double reach = shoot.span * vigor * t * form->reach;
            double y = h * vigor * (form->arch * sin(PI * t) + form->tip_height * t);
            double outward = 0;
            if (shoot.hanging) {
                reach *= form->hanging_spread * (.49 + .18 * sin(PI * t));
                outward = shoot.span * .36 * sin(PI * t * .65);
                y = shoot.span * vigor * (form->hanging_lift * sin(PI * t) - form->drop * t * t);
            }
            double at[3] = {outward + cos(angle) * reach, y, sin(angle) * reach};
            qlcg_pa_vine_twig(b, previous, at, radius * (1 - .76 * (n-1) / nodes),
                                 (1 - .76 * t) / (1 - .76 * (n-1) / nodes), shoot.stem);
            if (i < habit->twined_canes && !seedling) {
                double turn = 1.9 * n + a, offset = radius * (1.1 - .75*t);
                double twist[3] = {at[0]+cos(turn)*offset, at[1]+sin(turn)*offset, at[2]-sin(turn)*offset};
                qlcg_pa_vine_twig(b, previous_twist, twist, offset*.52, .82,
                                     qlcg_lp_lighten(shoot.stem, .22));
                memcpy(previous_twist, twist, sizeof(twist));
            }
            double bearing = angle + (n % 2 ? .93 : -.93);
            habit->draw_node(b, &shoot, at, bearing, n);
            memcpy(previous, at, sizeof(previous));
        }
    }
}
