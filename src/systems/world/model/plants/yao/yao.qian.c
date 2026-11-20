#include "yao_model.h"

#include <math.h>

static void pad_point(double out[3], const double at[3], double a, double radius, double lift) {
    out[0] = at[0] + cos(a) * radius;
    out[1] = at[1] + lift;
    out[2] = at[2] + sin(a) * radius;
}

static void draw_pad(LpBuilder *b, const double at[3], double a, double r, const YaoPlant *p) {
    int sides = p->simple ? 8 : 16;
    LpRgb underside = p->dry ? p->shade : qlcg_lp_wind_color(qlcg_lp_rgb(107, 66, 92), LP_WIND_LEAF);
    double center[3] = {at[0], at[1] + r * .095, at[2]};
    for (int i = 0; i < sides; i++) {
        int j = (i + 1) % sides;
        double t0 = a + i * TAU / sides, t1 = a + j * TAU / sides;
        double r0 = r * (i == 0 ? .81 : i % 2 ? 1.0 : .94);
        double r1 = r * (j == 0 ? .81 : j % 2 ? 1.0 : .94);
        double v0 = r * (.10 + .063 * sin(3 * t0));
        double v1 = r * (.10 + .063 * sin(3 * t1));
        double e0[3], e1[3], u0[3], u1[3];
        pad_point(e0, at, t0, r0, v0);
        pad_point(e1, at, t1, r1, v1);
        pad_point(u0, at, t0, r0, v0 - r * .035);
        pad_point(u1, at, t1, r1, v1 - r * .035);
        if (p->simple) {
            qlcg_lp_triangle(b, center, e1, e0, i % 2 ? p->leaf : p->shade, LP_MAT_SOLID);
        } else {
            double vein[3];
            pad_point(vein, at, t0, r0 * .62, v0 + r * .037);
            qlcg_lp_triangle(b, center, e1, vein, p->leaf, LP_MAT_SOLID);
            qlcg_lp_triangle(b, vein, e1, e0, qlcg_lp_deepen(p->leaf, .18), LP_MAT_SOLID);
        }
        qlcg_lp_quad(b, e0, e1, u1, u0, underside, LP_MAT_SOLID);
        qlcg_lp_triangle(b, at, u0, u1, underside, LP_MAT_SOLID);
    }
}

static void draw_fruit(LpBuilder *b, const double at[3], double r, LpRgb color, int simple) {
    qlcg_pa_yao_berry(b, at, r, .91, color);
    LpOpts point = qlcg_lp_opts();
    point.mat = LP_MAT_FRUIT;
    point.cap = 1;
    int count = simple ? 4 : 10;
    for (int i = 0; i < count; i++) {
        double a = i * GOLDEN;
        double y = r * (.15 + .19 * (i % 3));
        double pos[3];
        pad_point(pos, at, a, r * (.66 - .08 * (i % 3)), y);
        qlcg_lp_cone(b, pos, r * .15, r * .15, r * .67, 3, qlcg_lp_lighten(color, .17), &point);
    }
}

void qlcg_plant_yao_qian(LpBuilder *b, const PdDef *def, int season, int form) {
    YaoPlant p = qlcg_pa_yao_plant(def, season, form, PD_SEASON_SUMMER);
    const double root[3] = {0, 0, 0};
    int pads = p.dry || p.seedling ? 2 : p.simple ? 3 : 5;
    for (int i = 0; i < pads; i++) {
        double a = p.phase + TAU * i / pads;
        double r = p.spread * (.42 + .045 * (i % 3)) * p.leaf_scale;
        double at[3];
        pad_point(at, root, a, p.spread * (.77 + .085 * (i % 3)),
                  p.height * (.18 + .115 * (i % 3)));
        qlcg_pa_stem_between(b, root, at, p.height * .027, .52, p.stem);
        draw_pad(b, at, a, r, &p);
    }
    if (!p.bloom && !p.fruit) return;
    int count = p.simple ? 1 : 2;
    for (int i = 0; i < count; i++) {
        double a = p.phase + i * PI;
        double size = p.spread * (i ? .21 : .28);
        double at[3];
        pad_point(at, root, a, p.spread * (i ? .37 : .19), p.height * (i ? .65 : .85));
        qlcg_pa_stem_between(b, root, at, p.height * .026, .61, p.stem);
        if (p.bloom) {
            LpRgb violet = qlcg_pa_tone(def, "bloom", 1);
            qlcg_pa_yao_flower(b, at, a, p.simple ? 6 : 9, size, size * .39, .43, violet);
            qlcg_pa_yao_flower(b, at, a + .31, 6, size * .72, size * .35, .82,
                               qlcg_lp_mix(violet, qlcg_lp_rgb(255, 243, 242), .74));
            LpOpts core = qlcg_lp_opts();
            core.mat = LP_MAT_BLOOM;
            qlcg_lp_dome(b, at, size * .20, size * .22, size * .20, 5,
                         qlcg_lp_rgb(228, 183, 86), &core);
        } else {
            draw_fruit(b, at, size * .57, qlcg_pa_tone(def, "fruit", 1), p.simple);
        }
    }
}
