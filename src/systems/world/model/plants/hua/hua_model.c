#include "hua_model.h"
#include "../../plant_forms.h"

#include <math.h>

typedef struct {
    const HuaProfile *p;
    LpRgb leaf, shade, stem, flower;
    LpRng rng;
    double height, spread, bloom_size, seed_scale, foliage;
    int stems, simple, bloom, seed_count, seed_visible, winter;
} HuaPlant;

static void stem_rot(LpBuilder *b, const double start[3], const double end[3],
                     double radius, double taper, int sides, double rot, LpRgb color) {
    double dir[3] = {end[0] - start[0], end[1] - start[1], end[2] - start[2]};
    double len = js_hypot3(dir[0], dir[1], dir[2]);
    if (len <= 0) return;
    for (int i = 0; i < 3; i++) dir[i] /= len;
    int n = sides < 3 ? 3 : sides;
    double up[3] = {0.0, 1.0, 0.0};
    if (fabs(dir[1]) > 0.99) { up[0] = 1.0; up[1] = 0.0; }
    double ux = up[1] * dir[2] - up[2] * dir[1];
    double uy = up[2] * dir[0] - up[0] * dir[2];
    double uz = up[0] * dir[1] - up[1] * dir[0];
    double ul = js_hypot3(ux, uy, uz);
    if (ul <= 1e-12) ul = 1.0;
    ux /= ul; uy /= ul; uz /= ul;
    double vx = dir[1] * uz - dir[2] * uy;
    double vy = dir[2] * ux - dir[0] * uz;
    double vz = dir[0] * uy - dir[1] * ux;
    double r0[8][3], r1[8][3];
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) {
        double a = rot + ((double)i / (double)n) * TAU;
        double ca = cos(a), sa = sin(a);
        double dx = ux * ca + vx * sa, dy = uy * ca + vy * sa, dz = uz * ca + vz * sa;
        r0[i][0] = start[0] + dx * radius;
        r0[i][1] = start[1] + dy * radius;
        r0[i][2] = start[2] + dz * radius;
        r1[i][0] = end[0] + dx * (radius * taper);
        r1[i][1] = end[1] + dy * (radius * taper);
        r1[i][2] = end[2] + dz * (radius * taper);
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        qlcg_lp_quad(b, r0[i], r0[j], r1[j], r1[i], color, LP_MAT_SOLID);
    }
    for (int i = 1; i < n - 1; i++)
        qlcg_lp_triangle(b, r1[0], r1[i], r1[i + 1], color, LP_MAT_SOLID);
}

static void stem_to(LpBuilder *b, const double start[3], const double end[3],
                    double radius, double taper, LpRgb color) {
    stem_rot(b, start, end, radius, taper, 5, 0.0, color);
}

int qlcg_pa_hua_seed_count(const PdDef *def, int season) {
    if (def->state != PD_STATE_FRUITING &&
        (def->state != PD_STATE_NORMAL || season != PD_SEASON_AUTUMN)) return 0;
    return qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "seedCount", 0), 0, 8));
}

static void seed_body(LpBuilder *b, const double at[3], double x, double y, double z, LpRgb color) {
    double size[3] = {x, y, z};
    LpOpts opts = qlcg_lp_opts();
    opts.mat = LP_MAT_FRUIT;
    opts.rings = 0;
    qlcg_lp_ico(b, at, size, 5, color, &opts);
}

static void lotus_pod(LpBuilder *b, const double at[3], double r, double h, LpRgb color) {
    LpOpts opts = qlcg_lp_opts();
    opts.mat = LP_MAT_FRUIT;
    opts.radius_ratio = 1.82;
    qlcg_lp_cone(b, at, r / 1.82, r / 1.82, h, 12, color, &opts);
    double bot[3] = {at[0], at[1] - r * .16, at[2]};
    for (int k = 0; k < 12; k++) {
        double a0 = TAU * k / 12, a1 = TAU * (k + 1) / 12;
        double p0[3] = {at[0] + cos(a0) * r / 1.82, at[1], at[2] + sin(a0) * r / 1.82};
        double p1[3] = {at[0] + cos(a1) * r / 1.82, at[1], at[2] + sin(a1) * r / 1.82};
        qlcg_lp_triangle(b, p0, p1, bot, color, LP_MAT_FRUIT);
    }
    LpRgb well = qlcg_pa_deepen(color, .67);
    for (int i = 0; i < 7; i++) {
        double a = TAU * i / 6;
        double reach = i == 0 ? 0 : r * .61;
        double x = at[0] + cos(a) * reach, z = at[2] + sin(a) * reach;
        double floor[3] = {x, at[1] + h + r * .020, z};
        for (int k = 0; k < 6; k++) {
            double l = TAU * k / 6, m = TAU * (k + 1) / 6;
            double p[3] = {x + cos(l) * r * .14, floor[1], z + sin(l) * r * .14};
            double q[3] = {x + cos(m) * r * .14, floor[1], z + sin(m) * r * .14};
            double rim[3] = {x + cos(l) * r * .205, floor[1] + r * .025, z + sin(l) * r * .205};
            double next[3] = {x + cos(m) * r * .205, rim[1], z + sin(m) * r * .205};
            double ob[3] = {x + cos(l) * r * .215, at[1] + h - r * .005, z + sin(l) * r * .215};
            double on[3] = {x + cos(m) * r * .215, ob[1], z + sin(m) * r * .215};
            qlcg_lp_triangle(b, floor, q, p, well, LP_MAT_FRUIT);
            qlcg_lp_quad(b, p, q, next, rim, qlcg_lp_lighten(color, .14), LP_MAT_FRUIT);
            qlcg_lp_quad(b, rim, next, on, ob, color, LP_MAT_FRUIT);
        }
    }
}

void qlcg_pa_hua_seed(LpBuilder *b, const double at[3], const HuaSeedProfile *p,
                      double scale, int visible) {
    qlcg_lp_socket(b, at, LP_SOCKET_SEED);
    if (!visible) return;
    double r = p->radius * scale, h = r * p->aspect;
    LpRgb color = qlcg_lp_hex(p->color, 132, 100, 50);
    if (p->form == HUA_RECEPTACLE) {
        lotus_pod(b, at, r, h, color);
        return;
    }
    if (p->form == HUA_DRUPE || p->form == HUA_HIP) {
        double ctr[3] = {at[0], at[1] - h * .74, at[2]};
        seed_body(b, ctr, r, h, r * .91, color);
        if (p->form == HUA_DRUPE && p->divisions == 2) {
            double cheek[3] = {ctr[0] + r * .33, ctr[1], ctr[2]};
            seed_body(b, cheek, r * .72, h * .94, r * .84, qlcg_lp_lighten(color, .21));
        } else if (p->form == HUA_HIP) {
            LpOpts calyx = qlcg_lp_opts();
            calyx.mat = LP_MAT_FRUIT;
            calyx.pitch = -.38;
            double tip[3] = {ctr[0], ctr[1] - h * .84, ctr[2]};
            for (int i = 0; i < 5; i++)
                qlcg_lp_petal(b, tip, TAU * i / 5, r * .55, r * .19, qlcg_lp_rgb(99, 91, 44), &calyx);
        }
        return;
    }
    double axis = p->form == HUA_CONE ? -h : h;
    double ctr[3] = {at[0], at[1] + axis * .43, at[2]};
    if (p->form == HUA_CAPSULE || p->form == HUA_CONE)
        seed_body(b, ctr, r * .73, h * .52, r * .73, qlcg_pa_deepen(color, .18));
    if (p->form == HUA_ACHENES || p->form == HUA_GRAINS)
        seed_body(b, ctr, r * .46, h * .50, r * .46, qlcg_pa_deepen(color, .24));
    if (p->form == HUA_FOLLICLES)
        seed_body(b, ctr, r * .49, h * .55, r * .49, qlcg_pa_deepen(color, .37));
    for (int i = 0; i < p->divisions; i++) {
        double a = i * GOLDEN;
        double reach = r * .50;
        double y = at[1] + axis * .50;
        double width = r * .49, length = h * .47;
        if (p->form == HUA_GRAINS || p->form == HUA_CONE) {
            double t = (i + .5) / p->divisions;
            reach *= 1.15 - .72 * t;
            y = at[1] + axis * (.22 + .69 * t);
            width *= .71;
            length *= .30;
        } else if (p->form == HUA_ACHENES) {
            reach *= 1.12;
            width *= .48;
            length *= .70;
        }
        double part[3] = {at[0] + cos(a) * reach, y, at[2] + sin(a) * reach};
        seed_body(b, part, width, length, width * .87, qlcg_pa_deepen(color, .13 * (i % 3)));
        if (p->form == HUA_FOLLICLES) {
            part[1] += length * .81;
            seed_body(b, part, width * .25, length * .19, width * .25, qlcg_lp_rgb(53, 40, 33));
        }
    }
}

static void seed_at(LpBuilder *b, const HuaPlant *c, const double at[3]) {
    qlcg_pa_hua_seed(b, at, &c->p->seed, c->seed_scale, c->seed_visible);
}

static void bloom_petal(LpBuilder *b, const double at[3], double a, double len,
                        double width, double pitch, LpRgb color) {
    double dx = cos(a), dz = sin(a), lift = sin(pitch) * len;
    double mid[3] = {at[0] + dx * len * .53, at[1] + lift * .53, at[2] + dz * len * .53};
    double tip[3] = {at[0] + dx * len, at[1] + lift, at[2] + dz * len};
    double left[3] = {mid[0] - dz * width * .50, mid[1] + width * .16, mid[2] + dx * width * .50};
    double right[3] = {mid[0] + dz * width * .50, mid[1] + width * .16, mid[2] - dx * width * .50};
    qlcg_lp_triangle(b, at, left, tip, color, LP_MAT_BLOOM);
    qlcg_lp_triangle(b, tip, right, at, color, LP_MAT_BLOOM);
}

static void flower(LpBuilder *b, const HuaPlant *c, const double at[3], double angle) {
    const HuaProfile *p = c->p;
    double r = c->bloom_size;
    LpOpts opts = qlcg_lp_opts();
    opts.mat = LP_MAT_BLOOM;
    int petals = c->simple ? 5 : p->petals;
    int rings = c->simple ? 1 : p->rings;
    if (p->flower == HUA_LIP) {
        for (int i = 0; i < 5; i++) {
            double a = angle + TAU * i / 5;
            double dp = .035 * sin(a * 2.7 + (double)i * 1.7);
            double pitch = (i == 0 ? .70 : .32) + dp;
            double width = i % 2 ? .48 : .28;
            bloom_petal(b, at, a, r, r * width, pitch, c->flower);
        }
        bloom_petal(b, at, angle + PI, r * .72, r * .48, -.32 + .03 * sin(angle), qlcg_lp_deepen(c->flower, .34));
    } else {
        for (int ring = 0; ring < rings; ring++) {
            double t = (double)ring / rings;
            double base[3] = {at[0], at[1] + r * (.045 + .17 * t), at[2]};
            double length = r * (1 - .49 * t);
            double width = length * (p->flower == HUA_RAY ? .24 : p->flower == HUA_DOUBLE ? .68 : .82);
            double pitch = (p->flower == HUA_DOUBLE ? .32 : p->flower == HUA_OPEN ? .44 : .15) + t * .74;
            LpRgb color = qlcg_lp_deepen(c->flower, t * .24);
            for (int j = 0; j < petals; j++) {
                double a = angle + TAU * (j + .45 * ring) / petals
                           + .035 * (double)(((j * 3 + ring) % 5) - 2);
                double ph = a * 2.7 + (double)(ring * 5 + j) * 1.7 + at[0] * 13.0 + at[2] * 17.0;
                double dp = .05 * sin(ph);
                bloom_petal(b, base, a, length, width, pitch + dp, color);
            }
        }
    }
    opts.rings = 0;
    opts.rot_y = atan2(at[2], at[0]) + at[0] * 7.3 + at[1] * 11.7 + at[2] * 5.1 + 0.37;
    LpRgb gold = qlcg_lp_rgb(241, 181, 56);
    double core[3] = {at[0], at[1], at[2]};
    qlcg_lp_dome(b, core, r * .21, r * .20, r * .21, 5, gold, &opts);
    if (p->flower == HUA_COLUMN) {
        double tip[3] = {at[0], at[1] + r * .68, at[2]};
        stem_to(b, core, tip, r * .046, .62, qlcg_lp_deepen(c->flower, .45));
        qlcg_lp_dome(b, tip, r * .13, r * .075, r * .13, 5, gold, &opts);
    }
}

static void leaves_on(LpBuilder *b, const HuaPlant *c, const double start[3], const double end[3],
                      double bearing, int sites) {
    int count = qlcg_pa_js_round(sites * c->foliage);
    for (int i = 0; i < count; i++) {
        double t = .26 + .65 * (i + 1) / count;
        double at[3] = {start[0] + (end[0] - start[0]) * t,
                        start[1] + (end[1] - start[1]) * t,
                        start[2] + (end[2] - start[2]) * t};
        double len = c->spread * c->p->leaf_size * (1 - .34 * t) * (.65 + .35 * c->foliage);
        double side = i % 2 ? 1.0 : -1.0;
        double seed = bearing * 3.7 + (double)i * 1.9 + at[0] * 11.3 + at[1] * 17.7 + at[2] * 13.1;
        LpOpts opts = qlcg_lp_opts();
        opts.pitch = .16 + .08 * (double)((i * 2) % 4) + .06 * sin(seed) + (side > 0 ? .06 : -.05);
        opts.cup = .15 + .08 * (double)((i * 3 + (side > 0 ? 1 : 2)) % 4) + .05 * cos(seed * 1.3);
        double yaw = (side > 0 ? 1.21 : -1.04) + (double)(((i * 3) % 5) - 2) * .065;
        double ang_j = .09 * cos(seed * 0.9);
        qlcg_lp_petal(b, at, bearing + yaw + ang_j, len, len * c->p->leaf_width,
                     i % 2 ? c->leaf : c->shade, &opts);
    }
}

static void woody(LpBuilder *b, const PdDef *def, HuaPlant *c) {
    const HuaProfile *p = c->p;
    double h = c->height, r = c->spread;
    double origin[3] = {0, 0, 0};
    double fork[3] = {r * p->crown_base * .23, h * p->crown_base, 0};
    double root_radius = fmin(h, r) * (p->habit == HUA_TREE ? .14 : .064);
    stem_rot(b, origin, fork, root_radius, .63, 5, 0.0, c->stem);
    double phase = qlcg_lp_rng_next(&c->rng) * TAU;
    for (int i = 0; i < c->stems; i++) {
        double a = phase + GOLDEN * i;
        double dx = cos(a), dz = sin(a);
        double scale = .78 + .22 * qlcg_lp_rng_next(&c->rng);
        double brot = (double)i * .47 + .19;
        double mid[3] = {fork[0] + dx * r * .45, fork[1] + h * p->rise * .62 * scale, dz * r * .45};
        double tip[3] = {fork[0] + dx * r * scale, fork[1] + h * p->rise * scale, dz * r * scale};
        stem_rot(b, fork, mid, root_radius * .57, .64, 5, brot + .31, c->stem);
        stem_rot(b, mid, tip, root_radius * .39, .43, 5, brot + .73, c->stem);
        leaves_on(b, c, fork, mid, a, c->simple ? 2 : 5);
        leaves_on(b, c, mid, tip, a, c->simple ? 2 : 7);
        if (!c->simple) {
            double smid[3] = {mid[0] - (mid[0] - fork[0]) * .08,
                              mid[1] - (mid[1] - fork[1]) * .08,
                              mid[2] - (mid[2] - fork[2]) * .08};
            double side[3] = {mid[0] + cos(a + .85) * r * .45, mid[1] + h * p->rise * .23,
                              mid[2] + sin(a + .85) * r * .45};
            stem_rot(b, smid, side, root_radius * .26, .37, 5, brot + 1.19, c->stem);
            leaves_on(b, c, smid, side, a + .85, 4);
            if (c->bloom) flower(b, c, side, a + .53);
        }
        if (c->bloom) {
            flower(b, c, tip, a);
            if (p->habit == HUA_TREE && !c->simple) {
                for (int j = 0; j < 3; j++) {
                    double t = .18 + .27 * j;
                    double node[3] = {mid[0] + (tip[0] - mid[0]) * t,
                                      mid[1] + (tip[1] - mid[1]) * t,
                                      mid[2] + (tip[2] - mid[2]) * t};
                    double bearing = a + (j % 2 ? -.91 : .91);
                    double at[3] = {node[0] + cos(bearing) * c->bloom_size * .48,
                                    node[1] + c->bloom_size * .22,
                                    node[2] + sin(bearing) * c->bloom_size * .48};
                    stem_rot(b, node, at, c->bloom_size * .055, .48, 5,
                             brot + (double)j * .41 + .23, c->stem);
                    flower(b, c, at, bearing + (double)j * .17);
                }
            }
        }
        if (i < c->seed_count) seed_at(b, c, tip);
        if (c->winter && qlcg_pa_has_snow_target(def, "canopy"))
            qlcg_lp_socket(b, tip, LP_SOCKET_SNOW_CANOPY);
    }
}

static void herb(LpBuilder *b, const HuaPlant *c) {
    double h = c->height, r = c->spread;
    double origin[3] = {0, 0, 0};
    qlcg_lp_prism(b, origin, r * .19, r * .19, 0, h * .09, 5, c->stem, NULL);
    for (int i = 0; i < c->stems; i++) {
        double a = GOLDEN * i, dx = cos(a), dz = sin(a);
        double variation = .73 + .27 * (double)(i % 4) / 3;
        double base[3] = {dx * r * .10, h * .055, dz * r * .10};
        double tip[3] = {dx * r * .46 * variation, h * variation, dz * r * .46 * variation};
        stem_rot(b, base, tip, fmin(r, h) * .045, .55, 5, (double)i * .43 + .19, c->stem);
        leaves_on(b, c, base, tip, a, c->simple ? 3 : 7);
        if (c->bloom) flower(b, c, tip, a);
        if (i < c->seed_count) seed_at(b, c, tip);
    }
}

static void orchid(LpBuilder *b, const HuaPlant *c) {
    double h = c->height, r = c->spread;
    double origin[3] = {0, 0, 0};
    int leaves = qlcg_pa_js_round(c->foliage * (c->simple ? 5 : 12));
    qlcg_lp_prism(b, origin, r * .14, r * .14, 0, h * .07, 5, c->stem, NULL);
    for (int i = 0; i < leaves; i++) {
        LpOpts opts = qlcg_lp_opts();
        opts.pitch = .44 + .18 * (i % 4);
        opts.cup = .24;
        double len = r * (1.58 - .14 * (i % 3)) * (.71 + .29 * c->foliage);
        qlcg_lp_petal(b, origin, i * GOLDEN, len, r * .16,
                     i % 2 ? c->leaf : c->shade, &opts);
    }
    if (!c->bloom && !c->seed_count) return;
    double tip[3] = {r * .45, h, 0};
    stem_to(b, origin, tip, r * .034, .56, c->stem);
    for (int i = 0; i < c->stems; i++) {
        double t = .48 + .51 * (i + 1) / c->stems;
        double angle = i * GOLDEN;
        double node[3] = {tip[0] * t, tip[1] * t, 0};
        double at[3] = {node[0] + cos(angle) * r * .22, node[1] + h * .036, sin(angle) * r * .22};
        stem_to(b, node, at, r * .021, .52, c->stem);
        if (c->bloom) flower(b, c, at, angle);
        if (i < c->seed_count) seed_at(b, c, at);
    }
}

static void lotus(LpBuilder *b, const HuaPlant *c) {
    double h = c->height, r = c->spread;
    double origin[3] = {0, 0, 0};
    qlcg_lp_prism(b, origin, r * .15, r * .15, 0, h * .055, 5, c->stem, NULL);
    for (int i = 0; i < c->stems; i++) {
        double a = i * GOLDEN, dx = cos(a), dz = sin(a);
        double base[3] = {dx * r * .07, 0, dz * r * .07};
        double tip[3] = {dx * r * .50, h * (.46 + .09 * (i % 4)), dz * r * .50};
        stem_to(b, base, tip, r * c->p->leaf_stem_width, .62, c->stem);
        if (c->foliage > .12) {
            double width = r * (.38 + .07 * (i % 3)) * sqrt(c->foliage);
            int sides = c->simple ? 6 : 10;
            for (int j = 0; j < sides; j++) {
                double x = a + TAU * j / sides, z = a + TAU * (j + 1) / sides;
                double l[3] = {tip[0] + cos(x) * width, tip[1] + width * .17, tip[2] + sin(x) * width};
                double m[3] = {tip[0] + cos(z) * width, tip[1] + width * .17, tip[2] + sin(z) * width};
                qlcg_lp_triangle(b, tip, m, l, j % 2 ? c->leaf : c->shade, LP_MAT_SOLID);
            }
        }
    }
    if (!c->bloom && !c->seed_count) return;
    int stems = c->simple ? 1 : 2;
    for (int i = 0; i < stems; i++) {
        double x = r * (i ? -.33 : .22);
        double base[3] = {x, 0, 0}, tip[3] = {x, h * (i ? .84 : 1.04), 0};
        stem_to(b, base, tip, r * c->p->flower_stem_width, .68, c->stem);
        if (c->bloom) flower(b, c, tip, i * GOLDEN);
        if (i < c->seed_count) seed_at(b, c, tip);
    }
}

static void pine(LpBuilder *b, const PdDef *def, const HuaPlant *c) {
    double h = c->height, r = c->spread;
    double origin[3] = {0, 0, 0};
    double bend[3] = {r * .19, h * .40, 0}, leader[3] = {r * .34, h * .91, 0};
    double joint[3] = {bend[0] * .94, bend[1] * .94, 0};
    stem_to(b, origin, bend, r * .14, .66, c->stem);
    stem_rot(b, joint, leader, r * .086, .42, 5, 0.37, c->stem);
    double denom = c->stems > 1 ? (double)(c->stems - 1) : 1.0;
    for (int i = 0; i < c->stems; i++) {
        double t = 0.15 + 0.75 * (double)i / denom;
        double a = i * GOLDEN + .6;
        double base[3] = {bend[0] + (leader[0] - bend[0]) * t, bend[1] + (leader[1] - bend[1]) * t, 0};
        double reach = r * (1.15 - .81 * t);
        double tip[3] = {base[0] + cos(a) * reach, base[1] + h * .055, sin(a) * reach};
        stem_rot(b, base, tip, r * .047 * (1 - .57 * t), .32, 5, 0.31 * i + 0.17, c->stem);
        if (i < c->seed_count) seed_at(b, c, tip);
        if (c->foliage < .12) continue;
        double width = r * (.38 - .14 * t) * c->foliage;
        LpOpts opts = qlcg_lp_opts();
        opts.rings = 1;
        opts.rot_y = a;
        qlcg_lp_dome(b, tip, width, width * .54, width * .72, 6, c->leaf, &opts);
        if (!c->simple) {
            double spray[3] = {tip[0] - cos(a) * width * .36, tip[1], tip[2] - sin(a) * width * .36};
            qlcg_lp_dome(b, spray, width * .65, width * .31, width * .54, 5, c->shade, &opts);
        }
        if (c->winter && qlcg_pa_has_snow_target(def, "canopy")) {
            double snow[3] = {tip[0], tip[1] + width * .49, tip[2]};
            qlcg_lp_socket(b, snow, LP_SOCKET_SNOW_CANOPY);
        }
    }
}

void qlcg_pa_build_ornamental(LpBuilder *b, const PdDef *def, int season, int form,
                               const HuaProfile *p, const HuaShape *shape) {
    static const double density[] = {.70, 1.0, .76, 0};
    int dead = def->state == PD_STATE_WITHERED;
    int juvenile = def->state == PD_STATE_SEEDLING;
    int simple = form == VEG_FORM_SIMPLE;
    double h = fmax(.14, qlcg_pa_pnum(def, "height", 1.0));
    double r = fmax(.09, qlcg_pa_pnum(def, "spread", h * .46));
    HuaPlant c = {0};
    c.p = p;
    c.simple = simple;
    c.height = h * shape->height;
    c.spread = r * shape->spread;
    c.stems = p->stems + shape->stems;
    if (juvenile || dead || simple) c.stems = (c.stems + 1) / 2;
    c.bloom_size = p->bloom_size * shape->bloom_size;
    c.seed_scale = shape->bloom_size;
    c.winter = season == PD_SEASON_WINTER;
    c.bloom = !juvenile && !dead && def->bloom &&
        (def->state == PD_STATE_FLOWERING || (def->state == PD_STATE_NORMAL && season == p->bloom_season));
    c.seed_count = qlcg_pa_hua_seed_count(def, season);
    c.seed_visible = def->state == PD_STATE_FRUITING;
    c.foliage = dead ? .06 : c.winter ? p->winter_leaf : density[season];
    if (c.bloom && p->habit == HUA_TREE) c.foliage *= .24;
    if ((p->habit == HUA_HERB || p->habit == HUA_LOTUS) && c.foliage < .12) {
        c.height *= .39;
        c.stems = (c.stems + 1) / 2;
    }
    c.flower = qlcg_lp_hex(shape->color, 217, 101, 116);
    c.leaf = qlcg_pa_tone(def, dead ? "withered" : "foliage", 1);
    if (p->habit == HUA_PINE && !dead)
        c.leaf = qlcg_lp_wind_color(qlcg_lp_mix(c.leaf, c.flower, .62), LP_WIND_LEAF);
    c.shade = qlcg_pa_deepen(c.leaf, .25);
    c.stem = qlcg_pa_tone(def, dead ? "witheredStem" :
                          p->habit == HUA_TREE || p->habit == HUA_SHRUB || p->habit == HUA_PINE
                              ? "barkLight" : "foliageDark", 1);
    qlcg_lp_rng_seed(&c.rng, def->build_seed);

    switch (p->habit) {
    case HUA_TREE:
    case HUA_SHRUB: woody(b, def, &c); break;
    case HUA_ORCHID: orchid(b, &c); break;
    case HUA_LOTUS: lotus(b, &c); break;
    case HUA_PINE: pine(b, def, &c); break;
    default: herb(b, &c); break;
    }
}
