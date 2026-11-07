#include "shui_model.h"
#include "../../plant_forms.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* 水生配色 */
typedef struct {
    LpRgb leaf, dark, stem, dry, bloom, fruit, rib, vein;
} ShuiTone;

/* 单株构建上下文 */
typedef struct {
    const PdDef *def;
    const ShuiProfile *p;
    const ShuiSeason *s;
    const ShuiTone *t;
    LpRng *rng;
    double h, sp;
    int season;
} ShuiCtx;

/* 无中脉标记 */
static const LpRgb NO_RIB = { -1.0, 0.0, 0.0, 0 };

/* 读取抗性等级 */
static int tolerance(const PdDef *def, const char *prefix, int fallback) {
    char tag[16];
    for (int i = 0; i < 4; i++) {
        snprintf(tag, sizeof(tag), "%s_%d", prefix, i);
        if (qlcg_pa_tag_has(def, tag)) return i;
    }
    return fallback;
}

/* 寒温带冬季结冰 */
static int frozen_climate(const PdDef *def) {
    const char *c = def->climate ? def->climate : "temperate";
    return !strcmp(c, "cold") || !strcmp(c, "temperate");
}

/* 枯色：向草黄过渡 */
static LpRgb dry_of(LpRgb c, double amount) {
    LpRgb straw = qlcg_lp_rgb(163.0, 133.0, 72.0);
    double t = qlcg_pa_clampd(amount, 0.0, 1.0) * 0.80;
    return qlcg_lp_deepen(qlcg_lp_mix(c, straw, t), 0.18 * amount);
}

/* 由耐寒耐热推四季结构 */
ShuiSeason qlcg_pa_shui_season(const PdDef *def, int season, const ShuiProfile *p) {
    int cold_level = tolerance(def, "naihan", p->cold);
    int heat_level = tolerance(def, "naire", p->heat);
    double chill = (4.0 - qlcg_pa_clampd((double)cold_level, 0.0, 4.0)) / 4.0;
    double warmth = (4.0 - qlcg_pa_clampd((double)heat_level, 0.0, 4.0)) / 4.0;
    double win = 1.0 - chill;
    double sum = 1.0 - warmth;
    double keep = qlcg_pa_clampd(p->winter_leaf, 0.02, 0.95);
    ShuiSeason s;
    memset(&s, 0, sizeof(s));
    s.height = 1.0;
    s.blade = 1.0;
    s.density = 1.0;
    if (season == PD_SEASON_SPRING) {
        s.height = 0.74 + 0.16 * chill + 0.05 * keep;
        s.blade = 0.62 + 0.20 * chill;
        s.density = 0.55 + 0.20 * chill + 0.10 * keep;
        s.droop = 0.08;
        s.dry = 0.03;
        s.sprout = 2 + qlcg_pa_js_round(3.0 * chill);
    } else if (season == PD_SEASON_SUMMER) {
        s.height = 1.00 - 0.10 * sum;
        s.blade = 1.00 - 0.12 * sum;
        s.density = 1.00 - 0.30 * sum;
        s.droop = 0.18 + 0.35 * sum;
        s.dry = 0.05 + 0.50 * sum;
        s.stress = sum;
    } else if (season == PD_SEASON_AUTUMN) {
        s.height = 0.92 - 0.06 * win;
        s.blade = 0.86 - 0.08 * win;
        s.density = 0.84 - 0.18 * win;
        s.droop = 0.22 + 0.16 * win;
        s.dry = 0.35 + 0.35 * win;
        s.stress = 0.35 + 0.25 * win;
    } else {
        double keep_w = keep * (1.0 - 0.35 * win);
        s.height = 0.80 - 0.38 * win;
        s.blade = 0.76 - 0.40 * win;
        s.density = keep_w * 0.80;
        s.droop = 0.35 + 0.30 * win;
        s.dry = 0.18 + 0.74 * win;
        s.stress = win;
        s.standing = keep_w >= 0.10;
        s.dormant = keep_w < 0.10;
        s.frozen = frozen_climate(def);
    }
    s.bloom = p->bloom_form != SHUI_HEAD_NONE && season == p->bloom_season;
    s.seed_head = p->seed_form != SHUI_HEAD_NONE && season == PD_SEASON_AUTUMN;
    if (def->state == PD_STATE_FLOWERING) s.bloom = p->bloom_form != SHUI_HEAD_NONE;
    if (def->state == PD_STATE_FRUITING) {
        s.bloom = 0;
        s.seed_head = 0;
        s.fruit = 1;
    }
    if (def->state == PD_STATE_SUBADULT) {
        s.bloom = 0;
        s.seed_head = 0;
    }
    return s;
}

/* 色板键是否存在 */
static int has_tone(const PdDef *def, const char *key) {
    return key && pd_str(&def->palette, key, NULL) != NULL;
}

/* 统一配色 */
static void tones_of(const PdDef *def, const ShuiSeason *s, ShuiTone *t) {
    t->leaf = qlcg_pa_tone(def, "foliage", 1.0);
    t->dark = has_tone(def, "foliageDark") ? qlcg_pa_tone(def, "foliageDark", 1.0)
                                           : qlcg_pa_tone(def, "foliageDeep", 1.0);
    if (has_tone(def, "barkLight")) {
        t->stem = qlcg_pa_tone(def, "barkLight", 1.0);
    } else if (has_tone(def, "reedCulm")) {
        t->stem = qlcg_pa_tone(def, "reedCulm", 1.0);
    } else if (has_tone(def, "waterweed")) {
        t->stem = qlcg_lp_mix(t->leaf, qlcg_pa_tone(def, "waterweed", 1.0), 0.5);
        t->stem.wind = LP_WIND_BARK;
    } else {
        t->stem = qlcg_lp_mix(t->leaf, dry_of(t->leaf, 0.40), 0.45);
        t->stem.wind = LP_WIND_BARK;
    }
    t->dry = dry_of(t->leaf, 0.35 + 0.55 * s->dry);
    t->bloom = qlcg_pa_tone(def, "bloom", 1.0);
    t->fruit = qlcg_pa_tone(def, "fruit", 1.0);
    t->rib = qlcg_lp_lighten(t->leaf, 0.14);
    t->vein = qlcg_lp_lighten(t->leaf, 0.24);
}

/* 两点间茎段 */
static void segment(LpBuilder *b, const double a[3], const double z[3],
                    double radius, double taper, int sides, LpRgb col, int mat) {
    double d[3] = { z[0] - a[0], z[1] - a[1], z[2] - a[2] };
    double len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len <= 1e-5) return;
    d[0] /= len; d[1] /= len; d[2] /= len;
    LpOpts o = qlcg_lp_opts();
    o.sides = sides;
    o.tip_ratio = taper;
    o.mat = mat;
    qlcg_lp_limb(b, a, d, len, radius, col, &o);
}

/* 弯曲叶片，可带中脉 */
static void blade(LpBuilder *b, const double base[3], double bearing, double len, double wid,
                  double rise, double drop, LpRgb col, LpRgb rib, int segs, int mat) {
    if (segs < 1) segs = 1;
    double dx = cos(bearing), dz = sin(bearing);
    double px = -dz, pz = dx;
    double prev_c[3] = { base[0], base[1], base[2] };
    double prev_l[3] = { base[0] - px * wid * 0.5, base[1], base[2] - pz * wid * 0.5 };
    double prev_r[3] = { base[0] + px * wid * 0.5, base[1], base[2] + pz * wid * 0.5 };
    double prev_w = wid;
    for (int i = 1; i <= segs; i++) {
        double t = (double)i / (double)segs;
        double y = base[1] + rise * t * (2.0 - t) - drop * t * t;
        double cx = base[0] + dx * len * t;
        double cz = base[2] + dz * len * t;
        double w = wid * (1.0 - 0.60 * t);
        if (w < wid * 0.05) w = wid * 0.05;
        double cl[3] = { cx - px * w * 0.5, y, cz - pz * w * 0.5 };
        double cr[3] = { cx + px * w * 0.5, y, cz + pz * w * 0.5 };
        qlcg_lp_quad(b, prev_l, prev_r, cr, cl, col, mat);
        if (rib.r >= 0.0) {
            double lift = 0.012 * len;
            double ml0[3] = { prev_c[0] - px * prev_w * 0.07, prev_c[1] + lift, prev_c[2] - pz * prev_w * 0.07 };
            double mr0[3] = { prev_c[0] + px * prev_w * 0.07, prev_c[1] + lift, prev_c[2] + pz * prev_w * 0.07 };
            double ml1[3] = { cx - px * w * 0.07, y + lift, cz - pz * w * 0.07 };
            double mr1[3] = { cx + px * w * 0.07, y + lift, cz + pz * w * 0.07 };
            qlcg_lp_quad(b, ml0, mr0, mr1, ml1, rib, mat);
        }
        memcpy(prev_l, cl, sizeof(cl));
        memcpy(prev_r, cr, sizeof(cr));
        prev_c[0] = cx; prev_c[1] = y; prev_c[2] = cz;
        prev_w = w;
    }
}

/* 干叶散列挑选 */
static int pick_dry(int index, int stride, double amount) {
    double u = fmod((double)index * 0.6180339887 + (double)stride * 0.19 + 0.27, 1.0);
    return u < amount;
}

/* 叶色：干叶与常叶 */
static LpRgb leaf_col(const ShuiCtx *c, int index, int stride) {
    if (pick_dry(index, stride, c->s->dry)) return c->t->dry;
    return (index % 2) ? c->t->leaf : c->t->dark;
}

/* 籽粒 */
static void grain(LpBuilder *b, const double at[3], double size, LpRgb col, int mat) {
    double r[3] = { size, size * 0.75, size };
    LpOpts o = qlcg_lp_opts();
    o.sides = 4;
    o.rings = 1;
    o.mat = mat;
    qlcg_lp_ico(b, at, r, 4, col, &o);
}

/* 小穗 */
static void spikelet(LpBuilder *b, const double at[3], double bearing, double len,
                     LpRgb col, int mat, LpRng *rng) {
    for (int i = 0; i < 2; i++) {
        LpOpts o = qlcg_lp_opts();
        o.pitch = 0.50 + 0.22 * (double)i;
        o.cup = 0.12;
        o.rand = rng;
        o.mat = mat;
        qlcg_lp_petal(b, at, bearing, len * (1.0 - 0.20 * (double)i), len * 0.18, col, &o);
    }
}

/* 花序主轴 */
static void rachis(LpBuilder *b, const double base[3], double height, double lean,
                   double radius, LpRgb col, int mat, double tip[3]) {
    double dir[3] = { lean, 1.0, lean * 0.5 };
    double len = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    dir[0] /= len; dir[1] /= len; dir[2] /= len;
    LpOpts o = qlcg_lp_opts();
    o.sides = 3;
    o.tip_ratio = 0.45;
    o.mat = mat;
    o.cap = 0.0;
    qlcg_lp_limb(b, base, dir, height, radius, col, &o);
    tip[0] = base[0] + dir[0] * height;
    tip[1] = base[1] + dir[1] * height;
    tip[2] = base[2] + dir[2] * height;
}

/* 花穗/果穗 */
static void build_head(LpBuilder *b, const ShuiCtx *c, int form, const double origin[3],
                       double spread, double size, int bloom) {
    if (form == SHUI_HEAD_NONE) return;
    if (!bloom && !c->s->seed_head) return;
    LpRgb col = bloom ? c->t->bloom : c->t->dry;
    int mat = bloom ? LP_MAT_BLOOM : LP_MAT_SOLID;
    double tip[3];
    double height = size * 2.6;
    rachis(b, origin, height, 0.05, size * 0.16, c->t->stem, mat, tip);
    if (form == SHUI_HEAD_PANICLE) {
        for (int i = 0; i < 5; i++) {
            double t = (double)(i + 1) / 6.0;
            double angle = (double)i * GOLDEN + 0.4;
            double blen = spread * (0.85 - 0.35 * t);
            double node[3] = { origin[0] + (tip[0] - origin[0]) * t,
                               origin[1] + (tip[1] - origin[1]) * t,
                               origin[2] + (tip[2] - origin[2]) * t };
            double end[3] = { node[0] + cos(angle) * blen,
                              node[1] + blen * 0.50 + size * 0.4,
                              node[2] + sin(angle) * blen };
            segment(b, node, end, size * 0.08, 0.45, 3, col, mat);
            for (int j = 0; j < 2; j++) {
                double off = (double)(j - 1) * size * 0.18;
                double at[3] = { end[0] + cos(angle + 1.4) * off,
                                 end[1] - size * 0.20 - (double)j * size * 0.10,
                                 end[2] + sin(angle + 1.4) * off };
                spikelet(b, at, angle, size * 0.9, col, mat, c->rng);
            }
        }
        grain(b, tip, size * 0.22, col, mat);
    } else if (form == SHUI_HEAD_SPIKE) {
        for (int i = 0; i < 8; i++) {
            double t = (double)(i + 1) / 9.0;
            double at[3] = { origin[0] + (tip[0] - origin[0]) * t,
                             origin[1] + (tip[1] - origin[1]) * t,
                             origin[2] + (tip[2] - origin[2]) * t };
            spikelet(b, at, ((i % 2) ? 1.0 : -1.0) * 1.15, size, col, mat, c->rng);
            if (i % 3 == 0) grain(b, at, size * 0.22, col, mat);
        }
    } else if (form == SHUI_HEAD_SPADIX) {
        for (int i = 0; i < 10; i++) {
            double t = 0.20 + 0.78 * (double)i / 9.0;
            double at[3] = { origin[0] + (tip[0] - origin[0]) * t,
                             origin[1] + (tip[1] - origin[1]) * t,
                             origin[2] + (tip[2] - origin[2]) * t };
            double angle = (double)i * GOLDEN;
            grain(b, at, size * 0.30, col, mat);
            spikelet(b, at, angle, size * 0.55, col, mat, c->rng);
        }
    } else if (form == SHUI_HEAD_UMBEL) {
        for (int i = 0; i < 5; i++) {
            double angle = (double)i * (TAU / 5.0) + 0.4;
            double at[3] = { tip[0] + cos(angle) * spread * 0.40,
                             tip[1] - spread * 0.15,
                             tip[2] + sin(angle) * spread * 0.40 };
            segment(b, tip, at, size * 0.07, 0.5, 3, c->t->stem, mat);
            spikelet(b, at, angle, size * 0.9, col, mat, c->rng);
            grain(b, at, size * 0.20, col, mat);
        }
    }
}

/* 浮叶 */
static void build_pad(LpBuilder *b, const double at[3], double r, double angle, LpRgb col, LpRgb vein,
                      int rim, int veins, LpRng *rng) {
    double gap = 0.62;
    int n = 8;
    LpRgb edge = qlcg_lp_deepen(col, 0.12);
    for (int i = 0; i < n; i++) {
        double a0 = angle + gap * 0.5 + (TAU - gap) * (double)i / (double)n;
        double a1 = angle + gap * 0.5 + (TAU - gap) * (double)(i + 1) / (double)n;
        double p0[3] = { at[0] + cos(a0) * r, at[1], at[2] + sin(a0) * r };
        double p1[3] = { at[0] + cos(a1) * r, at[1], at[2] + sin(a1) * r };
        qlcg_lp_triangle(b, at, p0, p1, col, LP_MAT_SOLID);
        if (rim) {
            double h0[3] = { at[0] + cos(a0) * r * 1.02, at[1] + r * 0.16,
                             at[2] + sin(a0) * r * 1.02 };
            double h1[3] = { at[0] + cos(a1) * r * 1.02, at[1] + r * 0.16,
                             at[2] + sin(a1) * r * 1.02 };
            qlcg_lp_quad(b, p0, p1, h1, h0, edge, LP_MAT_SOLID);
        }
        if (veins && (i % 2 == 0)) {
            double am = (a0 + a1) * 0.5;
            double q0[3] = { at[0] + cos(am) * r * 0.20, at[1] + r * 0.01,
                             at[2] + sin(am) * r * 0.20 };
            double q1[3] = { at[0] + cos(am) * r * 0.92, at[1] + r * 0.01,
                             at[2] + sin(am) * r * 0.92 };
            double w0[3] = { q0[0] - sin(am) * r * 0.02, q0[1], q0[2] + cos(am) * r * 0.02 };
            double w1[3] = { q1[0] - sin(am) * r * 0.02, q1[1], q1[2] + cos(am) * r * 0.02 };
            double w2[3] = { q1[0] + sin(am) * r * 0.02, q1[1], q1[2] - cos(am) * r * 0.02 };
            double w3[3] = { q0[0] + sin(am) * r * 0.02, q0[1], q0[2] - cos(am) * r * 0.02 };
            qlcg_lp_quad(b, w0, w1, w2, w3, vein, LP_MAT_SOLID);
        }
    }
    double ctr[3] = { at[0], at[1] + r * 0.02, at[2] };
    LpOpts o = qlcg_lp_opts();
    o.sides = 5; o.rings = 0; o.squash = 0.4; o.rand = rng;
    qlcg_lp_dome(b, ctr, r * 0.10, r * 0.05, r * 0.10, 5, vein, &o);
}

/* 叶柄 */
static void petiole(LpBuilder *b, const double top[3], double bend, LpRgb col, LpRng *rng) {
    double mid[3] = { top[0] * 0.42 + bend * 0.5, top[1] * 0.55, top[2] * 0.42 };
    double base[3] = { 0.0, 0.0, 0.0 };
    segment(b, base, mid, 0.010, 0.85, 3, col, LP_MAT_SOLID);
    segment(b, mid, top, 0.006, 0.70, 3, col, LP_MAT_SOLID);
    (void)rng;
}

/* 沉水带状叶 */
static void build_ribbon(LpBuilder *b, const ShuiCtx *c) {
    double h = c->h, sp = c->sp;
    int basal = c->p->form == 0;
    double root_y = basal ? h * 0.035 + 0.004 : 0.02;
    if (basal) {
        double origin[3] = { 0.0, 0.0, 0.0 };
        qlcg_lp_prism(b, origin, sp * 0.21, sp * 0.21, 0.0, root_y * 1.6, 6, c->t->stem, NULL);
    }
    for (int i = 0; i < 3; i++) {
        double angle = (double)i * (TAU / 3.0) + 0.4;
        double base[3] = { 0.0, root_y, 0.0 };
        double end[3] = { cos(angle) * sp * 0.55, root_y + (basal ? 0.0 : 0.01), sin(angle) * sp * 0.55 };
        segment(b, base, end, h * 0.012, 0.4, 3, c->t->stem, LP_MAT_SOLID);
    }
    int n = qlcg_pa_js_round((double)c->p->blades * c->s->density);
    if (n < 3) n = 3;
    if (c->p->form == 1) {
        for (int i = 0; i < n / 2; i++) {
            double angle = (double)i * GOLDEN + 0.3;
            double base[3] = { cos(angle) * sp * 0.16, 0.02, sin(angle) * sp * 0.16 };
            double stem_len = h * c->p->blade_len * (0.8 + 0.3 * qlcg_lp_rng_next(c->rng));
            double tip[3] = { base[0] + cos(angle) * sp * 0.18, base[1] + stem_len,
                              base[2] + sin(angle) * sp * 0.18 };
            segment(b, base, tip, h * 0.006, 0.35, 3, c->t->stem, LP_MAT_SOLID);
            for (int j = 0; j < 8; j++) {
                double t = 0.15 + 0.80 * (double)j / 7.0;
                double node[3] = { base[0] + (tip[0] - base[0]) * t,
                                   base[1] + (tip[1] - base[1]) * t,
                                   base[2] + (tip[2] - base[2]) * t };
                for (int side = -1; side <= 1; side += 2) {
                    double dir = angle + side * 1.45;
                    double plen = h * 0.10 * c->p->blade_len * (1.0 - 0.5 * t);
                    blade(b, node, dir, plen, c->p->blade_width * 3.0,
                          plen * 0.10, plen * 0.10, leaf_col(c, i * 8 + j, 2), NO_RIB, 2, LP_MAT_SOLID);
                }
            }
        }
    } else {
        for (int i = 0; i < n; i++) {
            double angle = (double)i * GOLDEN + (qlcg_lp_rng_next(c->rng) - 0.5) * 0.4;
            double ring = 0.20 + 0.80 * (double)(i % 3) / 2.0;
            double base[3] = { cos(angle) * sp * 0.14 * ring, root_y,
                               sin(angle) * sp * 0.14 * ring };
            double len = h * c->p->blade_len * (0.80 + 0.30 * qlcg_lp_rng_next(c->rng));
            double wid = c->p->blade_width * c->s->blade;
            double rise = len * (0.86 - 0.40 * c->s->droop) * c->p->arch;
            double drop = fmin(len * (0.30 + 0.80 * c->s->droop), rise + root_y * 0.80);
            blade(b, base, angle, len, wid, rise, drop, leaf_col(c, i, 1), c->t->rib, 4, LP_MAT_SOLID);
        }
    }
    for (int i = 0; i < c->s->sprout; i++) {
        double angle = (double)i * (TAU / (double)c->s->sprout) + 0.5;
        double pos[3] = { cos(angle) * sp * 0.10, root_y + (basal ? 0.0 : 0.01), sin(angle) * sp * 0.10 };
        LpOpts o = qlcg_lp_opts();
        o.sides = 4; o.mat = LP_MAT_SOLID; o.cap = 0.0; o.rand = c->rng;
        qlcg_lp_cone(b, pos, h * 0.020, h * 0.020, h * 0.16, 4, c->t->rib, &o);
    }
    if (c->season == PD_SEASON_AUTUMN) {
        for (int i = 0; i < 2; i++) {
            double angle = (double)i * 2.7 + 0.9;
            double reach = sp * (basal ? 0.14 : 0.30);
            double base[3] = { cos(angle) * reach, root_y, sin(angle) * reach };
            double len = h * 0.55;
            double drop = basal ? fmin(len * 0.55, len * 0.35 + root_y * 0.80) : len * 0.55;
            blade(b, base, angle, len, c->p->blade_width * 1.2,
                  len * 0.35, drop, c->t->dry, NO_RIB, 2, LP_MAT_SOLID);
        }
    }
}

/* 挺水剑叶 */
static void build_sword(LpBuilder *b, const ShuiCtx *c) {
    double h = c->h, sp = c->sp;
    double origin[3] = { 0.0, 0.0, 0.0 };
    qlcg_lp_prism(b, origin, sp * 0.24, sp * 0.24, 0.0, h * 0.05, 5, c->t->stem, NULL);
    int n = qlcg_pa_js_round((double)c->p->blades * c->s->density);
    if (n < 3) n = 3;
    for (int rank = 0; rank < 2; rank++) {
        int cnt = n - rank * (n / 3);
        for (int i = 0; i < cnt; i++) {
            double angle = (double)i * GOLDEN + (double)rank * 0.55;
            double layer = (double)(i % 3) / 2.0 + 0.2 * (double)rank;
            double base[3] = { cos(angle) * sp * 0.10, h * 0.02, sin(angle) * sp * 0.10 };
            double len = h * c->p->blade_len * (1.00 - 0.20 * layer) * (0.90 + 0.15 * qlcg_lp_rng_next(c->rng));
            double wid = c->p->blade_width * c->s->blade * (1.10 - 0.25 * layer);
            double lift = (1.10 - 0.40 * layer) * (1.0 - 0.45 * c->s->droop) * c->p->arch;
            blade(b, base, angle, len, wid, len * lift, len * (0.10 + 0.50 * c->s->droop),
                  leaf_col(c, rank * 9 + i, 3), c->t->rib, 4, LP_MAT_SOLID);
        }
    }
    int form = c->s->bloom ? c->p->bloom_form : c->p->seed_form;
    if (form != SHUI_HEAD_NONE) {
        double top[3] = { 0.0, h * 0.74, 0.0 };
        build_head(b, c, form, top, sp * 0.45, c->p->bloom_size, c->s->bloom);
    }
    for (int i = 0; i < c->s->sprout; i++) {
        double angle = (double)i * (TAU / (double)c->s->sprout) + 0.2;
        double pos[3] = { cos(angle) * sp * 0.14, h * 0.02, sin(angle) * sp * 0.14 };
        LpOpts o = qlcg_lp_opts();
        o.sides = 4; o.mat = LP_MAT_SOLID; o.cap = 0.0; o.rand = c->rng;
        qlcg_lp_cone(b, pos, h * 0.022, h * 0.022, h * 0.20, 4, c->t->rib, &o);
    }
}

/* 芦苇 */
static void build_reed(LpBuilder *b, const ShuiCtx *c) {
    double h = c->h, sp = c->sp;
    int culms = (int)ceil((double)c->p->culms * (0.35 + 0.65 * c->s->density));
    if (culms < 2) culms = 2;
    int segs = 3;
    double head_bases[3][3];
    for (int i = 0; i < culms; i++) {
        double angle = (double)i * GOLDEN + (qlcg_lp_rng_next(c->rng) - 0.5) * 0.5;
        double rad = sp * (0.18 + 0.72 * qlcg_lp_rng_next(c->rng));
        double base[3] = { cos(angle) * rad, 0.0, sin(angle) * rad };
        double lean = (qlcg_lp_rng_next(c->rng) - 0.5) * 0.16;
        double cur[3] = { base[0], base[1], base[2] };
        double step = h / (double)segs;
        LpRgb culm_col = pick_dry(i, 4, c->s->dry) ? c->t->dry : c->t->stem;
        for (int sg = 0; sg < segs; sg++) {
            double t = (double)(sg + 1) / (double)segs;
            double next[3] = { base[0] + lean * h * t + cos(angle) * sp * 0.05 * t,
                               step * (double)(sg + 1),
                               base[2] + sin(angle) * sp * 0.05 * t };
            segment(b, cur, next, h * 0.012 * (1.0 - 0.4 * t), 0.85, 3, culm_col, LP_MAT_SOLID);
            if (sg < segs - 1) {
                LpOpts no = qlcg_lp_opts();
                no.sides = 4; no.mat = LP_MAT_SOLID; no.cap = 0.0;
                qlcg_lp_cone(b, next, h * 0.020, h * 0.020, h * 0.020, 4, c->t->stem, &no);
                int leaves = 2;
                for (int l = 0; l < leaves; l++) {
                    double la = angle + ((double)l ? 1.0 : -1.0) * (1.30 + 0.3 * qlcg_lp_rng_next(c->rng));
                    double len = h * 0.24 * c->s->blade * c->p->blade_len;
                    blade(b, next, la, len, c->p->blade_width * c->s->blade,
                          len * (0.55 - 0.30 * c->s->droop), len * (0.30 + 0.80 * c->s->droop),
                          leaf_col(c, i * 5 + sg * 2 + l, 6), c->t->rib, 3, LP_MAT_SOLID);
                }
            }
            memcpy(cur, next, sizeof(next));
        }
        if (i < 3) memcpy(head_bases[i], cur, sizeof(cur));
    }
    int form = c->s->bloom ? c->p->bloom_form : c->p->seed_form;
    int heads = 1 + qlcg_pa_js_round(2.0 * c->s->density);
    if (form == SHUI_HEAD_NONE) heads = 0;
    for (int i = 0; i < heads && i < 3 && i < culms; i++) {
        build_head(b, c, form, head_bases[i], sp * 0.50, c->p->bloom_size, c->s->bloom);
    }
    if (c->season == PD_SEASON_SPRING) {
        for (int i = 0; i < 2; i++) {
            double angle = (double)i * 2.8 + 0.5;
            double pos[3] = { cos(angle) * sp * 0.24, 0.02, sin(angle) * sp * 0.24 };
            LpOpts o = qlcg_lp_opts();
            o.sides = 4; o.mat = LP_MAT_SOLID; o.cap = 0.0; o.rand = c->rng;
            qlcg_lp_cone(b, pos, h * 0.024, h * 0.024, h * 0.22, 4, c->t->rib, &o);
        }
    }
    if (c->season == PD_SEASON_AUTUMN) {
        for (int i = 0; i < 3; i++) {
            double angle = (double)i * 2.2 + 0.7;
            double base[3] = { cos(angle) * sp * 0.28, 0.02, sin(angle) * sp * 0.28 };
            double len = h * 0.30;
            blade(b, base, angle, len, c->p->blade_width * 1.1,
                  len * 0.45, len * 0.70, c->t->dry, NO_RIB, 2, LP_MAT_SOLID);
        }
    }
}

/* 蒲草 */
static void build_sedge(LpBuilder *b, const ShuiCtx *c) {
    double h = c->h, sp = c->sp;
    int culms = (int)ceil((double)c->p->culms * (0.35 + 0.65 * c->s->density));
    if (culms < 2) culms = 2;
    double head_bases[3][3];
    for (int i = 0; i < culms; i++) {
        double angle = (double)i * GOLDEN + 0.2;
        double rad = sp * (0.15 + 0.55 * qlcg_lp_rng_next(c->rng));
        double base[3] = { cos(angle) * rad, 0.0, sin(angle) * rad };
        double top[3] = { base[0] + cos(angle) * sp * 0.12, h * (0.80 + 0.18 * qlcg_lp_rng_next(c->rng)),
                          base[2] + sin(angle) * sp * 0.12 };
        if (i < 3) memcpy(head_bases[i], top, sizeof(top));
        segment(b, base, top, h * 0.010, 0.55, 3, c->t->stem, LP_MAT_SOLID);
        for (int n = 0; n < 2; n++) {
            double t = 0.35 + 0.45 * (double)n;
            double node[3] = { base[0] + (top[0] - base[0]) * t,
                               base[1] + (top[1] - base[1]) * t,
                               base[2] + (top[2] - base[2]) * t };
            LpOpts no = qlcg_lp_opts();
            no.sides = 4; no.mat = LP_MAT_SOLID; no.cap = 0.0;
            qlcg_lp_cone(b, node, h * 0.018, h * 0.018, h * 0.018, 4, c->t->stem, &no);
            for (int l = 0; l < 2; l++) {
                double la = angle + ((double)l ? 1.0 : -1.0) * 1.35;
                double len = h * 0.32 * c->s->blade * c->p->blade_len;
                blade(b, node, la, len, c->p->blade_width * c->s->blade,
                      len * 0.55, len * (0.35 + 0.7 * c->s->droop),
                      leaf_col(c, i * 4 + n * 2 + l, 5), c->t->rib, 2, LP_MAT_SOLID);
            }
        }
    }
    int form = c->s->bloom ? c->p->bloom_form : c->p->seed_form;
    int heads = 1 + qlcg_pa_js_round(1.5 * c->s->density);
    if (form == SHUI_HEAD_NONE) heads = 0;
    for (int i = 0; i < heads && i < 3 && i < culms; i++) {
        build_head(b, c, form, head_bases[i], sp * 0.30, c->p->bloom_size, c->s->bloom);
    }
    if (c->season == PD_SEASON_AUTUMN) {
        for (int i = 0; i < 2; i++) {
            double angle = (double)i * 2.5 + 0.8;
            double base[3] = { cos(angle) * sp * 0.24, 0.02, sin(angle) * sp * 0.24 };
            double len = h * 0.45;
            blade(b, base, angle, len, c->p->blade_width * 1.2,
                  len * 0.40, len * 0.60, c->t->dry, NO_RIB, 2, LP_MAT_SOLID);
        }
    }
}

/* 浮叶类 */
static void build_floating(LpBuilder *b, const ShuiCtx *c) {
    double sp = c->sp;
    int pads = qlcg_pa_js_round((double)c->p->pads * c->s->density);
    int raised = c->p->habit == SHUI_LOTUS;
    int rim = c->p->form == 2;
    int winter = c->season == PD_SEASON_WINTER;
    if (raised && winter) {
        for (int i = 0; i < 8; i++) {
            double angle = (double)i * GOLDEN + 0.3;
            double base[3] = { cos(angle) * sp * 0.12, 0.0, sin(angle) * sp * 0.12 };
            double top[3] = { cos(angle) * sp * (0.30 + 0.18 * (double)(i % 3)),
                              c->h * (0.36 + 0.16 * (double)(i % 4)),
                              sin(angle) * sp * (0.30 + 0.18 * (double)(i % 3)) };
            segment(b, base, top, c->h * 0.008, 0.55, 3, c->t->dry, LP_MAT_SOLID);
            if (i < 4) {
                double len = c->h * 0.16;
                blade(b, top, angle, len, c->p->blade_width * 0.9,
                      len * 0.30, len * 0.80, c->t->dry, NO_RIB, 2, LP_MAT_SOLID);
            }
        }
        double at[3] = { 0.0, c->h * 0.52, 0.0 };
        LpOpts po = qlcg_lp_opts();
        po.sides = 6; po.mat = LP_MAT_SOLID; po.cap = 0.0;
        qlcg_lp_cone(b, at, c->p->seed_size, c->p->seed_size, c->p->seed_size * 1.5, 6,
                     c->t->dry, &po);
        double top2[3] = { at[0], at[1] + c->p->seed_size * 1.5, at[2] };
        for (int i = 0; i < 3; i++) {
            double angle = (double)i * (TAU / 3.0) + 0.4;
            double sp2[3] = { top2[0] + cos(angle) * c->p->seed_size * 0.55, top2[1] + 0.01,
                              top2[2] + sin(angle) * c->p->seed_size * 0.55 };
            qlcg_lp_socket(b, sp2, LP_SOCKET_FRUIT);
        }
        return;
    }
    if (winter && c->s->density < 0.12) pads = 0;
    if (pads < 1) {
        double spread = fmin(sp * 0.20, c->h * 0.30);
        double mound[3] = { 0.0, 0.0, 0.0 };
        LpRgb mud = qlcg_lp_mix(c->t->dry, c->t->stem, 0.35);
        LpOpts om = qlcg_lp_opts();
        om.floor_on = 0.0; om.jitter = 0.16; om.rand = c->rng;
        qlcg_lp_dome(b, mound, spread, c->h * 0.46, spread * 0.9, 6, mud, &om);
        for (int i = 0; i < 6; i++) {
            double angle = (double)i * (TAU / 6.0) + 0.4;
            double base[3] = { cos(angle) * spread * 0.45, 0.0, sin(angle) * spread * 0.45 };
            double top[3] = { base[0] + cos(angle) * spread * 0.40, c->h * (0.48 + 0.20 * (double)(i % 3)),
                              base[2] + sin(angle) * spread * 0.40 };
            segment(b, base, top, c->h * 0.008, 0.3, 3, c->t->dry, LP_MAT_SOLID);
        }
        for (int i = 0; i < 3; i++) {
            double angle = (double)i * 2.3 + 0.6;
            double base[3] = { cos(angle) * spread * 0.6, 0.0, sin(angle) * spread * 0.6 };
            double len = c->h * 0.30;
            blade(b, base, angle, len, c->p->blade_width * 1.4,
                  len * 0.20, len * 0.85, c->t->dry, NO_RIB, 2, LP_MAT_SOLID);
        }
        return;
    }
    if (winter && pads > 3) pads = 3;
    double pad_scale = 0.55 + 0.45 * c->s->density;
    double nominal = c->p->pad_radius * c->s->blade * pad_scale;
    int ring_count = pads - 1;
    double ring_radius = fmax(sp * 0.82, nominal * 2.0);
    double spacing = ring_count > 1
        ? fmin(ring_radius, 2.0 * ring_radius * sin(PI / ring_count)) : ring_radius;
    for (int i = 0; i < pads; i++) {
        double angle = i == 0 ? 0.0 : TAU * (i - 1) / ring_count + 0.4;
        double rad = i == 0 ? 0.0 : ring_radius;
        double level = c->p->float_y;
        if (raised && i % 3 != 0) level += c->h * (0.42 + 0.17 * (i % 3));
        double at[3] = { cos(angle) * rad, level, sin(angle) * rad };
        petiole(b, at, rad * 0.25, c->t->stem, c->rng);
        double r = nominal * (0.82 + 0.25 * qlcg_lp_rng_next(c->rng));
        if (pads > 1) r = fmin(r, (i == 0 ? ring_radius : spacing) * 0.47);
        LpRgb col = pick_dry(i, 3, c->s->dry) ? c->t->dry : ((i % 2) ? c->t->leaf : c->t->dark);
        build_pad(b, at, r, angle, col, c->t->vein, rim, 1, c->rng);
    }
    if (raised) {
        int flowers = c->s->bloom ? 1 + qlcg_pa_js_round((double)c->p->pads * 0.18) : 0;
        for (int i = 0; i < flowers; i++) {
            double angle = (double)i * 2.4 + 0.8;
            double at[3] = { cos(angle) * sp * 0.35, c->h * 0.62, sin(angle) * sp * 0.35 };
            petiole(b, at, 0.0, c->t->stem, c->rng);
            qlcg_pa_flower_at(b, c->def, at, c->p->bloom_size, c->rng);
        }
        if (c->s->seed_head || c->s->fruit) {
            double at[3] = { 0.0, c->h * 0.55, 0.0 };
            LpOpts po = qlcg_lp_opts();
            po.sides = 6; po.mat = LP_MAT_SOLID; po.cap = 0.0;
            qlcg_lp_cone(b, at, c->p->seed_size, c->p->seed_size, c->p->seed_size * 1.4, 6,
                         c->t->dry, &po);
            double top[3] = { at[0], at[1] + c->p->seed_size * 1.4, at[2] };
            for (int i = 0; i < 3; i++) {
                double angle = (double)i * (TAU / 3.0) + 0.4;
                double sp2[3] = { top[0] + cos(angle) * c->p->seed_size * 0.55, top[1] + 0.01,
                                  top[2] + sin(angle) * c->p->seed_size * 0.55 };
                qlcg_lp_socket(b, sp2, LP_SOCKET_FRUIT);
            }
        }
    } else {
        int flowers = c->s->bloom ? 1 + (int)ceil((double)c->p->pads * 0.20) : 0;
        if (flowers < 1 && c->s->bloom) flowers = 1;
        for (int i = 0; i < flowers; i++) {
            double angle = (double)i * GOLDEN + 0.6;
            double at[3] = { cos(angle) * sp * 0.45, c->p->float_y + c->h * 0.10,
                             sin(angle) * sp * 0.45 };
            qlcg_pa_flower_at(b, c->def, at, c->p->bloom_size, c->rng);
        }
        if (c->s->bloom) {
            for (int i = 0; i < 2; i++) {
                double angle = (double)i * 2.2 + 0.3;
                double at[3] = { cos(angle) * sp * 0.30, c->p->float_y + c->h * 0.03,
                                 sin(angle) * sp * 0.30 };
                LpOpts o = qlcg_lp_opts();
                o.sides = 4; o.mat = LP_MAT_SOLID; o.cap = 0.0;
                qlcg_lp_cone(b, at, sp * 0.05, sp * 0.05, c->h * 0.18, 4, c->t->rib, &o);
            }
        }
    }
    for (int i = 0; i < c->s->sprout; i++) {
        double angle = (double)i * (TAU / (double)c->s->sprout) + 0.5;
        double at[3] = { cos(angle) * sp * 0.20, c->p->float_y, sin(angle) * sp * 0.20 };
        LpOpts o = qlcg_lp_opts();
        o.sides = 4; o.rings = 0; o.squash = 0.5;
        qlcg_lp_dome(b, at, sp * 0.10, sp * 0.05, sp * 0.10, 4, c->t->leaf, &o);
    }
}

/* 凤眼蓝 */
static void build_hyacinth(LpBuilder *b, const ShuiCtx *c) {
    double sp = c->sp;
    int leaves = qlcg_pa_js_round((double)c->p->blades * c->s->density);
    if (leaves < 3) leaves = 3;
    for (int i = 0; i < leaves; i++) {
        double angle = (double)i * GOLDEN;
        double base[3] = { cos(angle) * sp * 0.08, 0.02, sin(angle) * sp * 0.08 };
        double len = c->h * 0.30 * (0.85 + 0.25 * qlcg_lp_rng_next(c->rng));
        double lift = (0.95 - 0.35 * c->s->droop) * c->p->arch;
        blade(b, base, angle, len, c->p->blade_width * c->s->blade,
              len * lift, len * (0.15 + 0.45 * c->s->droop),
              leaf_col(c, i, 2), c->t->rib, 2, LP_MAT_SOLID);
    }
    for (int i = 0; i < 6; i++) {
        double angle = (double)i * (TAU / 6.0) + 0.3;
        double at[3] = { cos(angle) * sp * 0.22, 0.05, sin(angle) * sp * 0.22 };
        double r[3] = { sp * 0.09, sp * 0.07, sp * 0.09 };
        LpOpts o = qlcg_lp_opts();
        o.sides = 4; o.rings = 1; o.jitter = 0.15; o.rand = c->rng;
        qlcg_lp_ico(b, at, r, 4, c->t->stem, &o);
    }
    if (c->s->bloom) {
        double base[3] = { 0.0, c->h * 0.18, 0.0 };
        double crown[3] = { 0.0, 0.02, 0.0 };
        double stem_radius = c->h * 0.012;
        LpOpts crown_opts = qlcg_lp_opts();
        qlcg_lp_dome(b, crown, sp * 0.12, c->h * 0.08, sp * 0.12, 5, c->t->stem, &crown_opts);
        segment(b, crown, base, stem_radius, 1.0, 3, c->t->stem, LP_MAT_SOLID);
        double tip[3];
        rachis(b, base, c->h * 0.62, 0.04, stem_radius, c->t->stem, LP_MAT_BLOOM, tip);
        for (int i = 0; i < 9; i++) {
            double t = (double)(i + 1) / 10.0;
            double at[3] = { base[0] + (tip[0] - base[0]) * t, base[1] + (tip[1] - base[1]) * t,
                             base[2] + (tip[2] - base[2]) * t };
            for (int j = 0; j < 3; j++) {
                LpOpts o = qlcg_lp_opts();
                o.pitch = 0.60 + 0.15 * (double)j;
                o.cup = 0.24;
                o.mat = LP_MAT_BLOOM;
                o.rand = c->rng;
                qlcg_lp_petal(b, at, (double)j * (TAU / 3.0) + (double)i * 0.9,
                              c->p->bloom_size * (0.55 + 0.20 * (double)(i % 2)),
                              c->p->bloom_size * 0.38, c->t->bloom, &o);
            }
        }
    }
}

/* 幼苗 */
static void build_seedling(LpBuilder *b, const ShuiCtx *c) {
    double h = c->h * 0.26;
    double sp = c->sp * 0.45;
    double root[3] = { 0.0, 0.0, 0.0 };
    double crown[3] = { 0.0, h * 0.10, 0.0 };
    segment(b, root, crown, h * 0.03, 0.7, 4, c->t->stem, LP_MAT_SOLID);
    for (int i = 0; i < 4; i++) {
        double angle = (double)i * GOLDEN;
        double base[3] = { 0.0, h * 0.10, 0.0 };
        double len = h * (0.55 + 0.15 * (double)(i % 2));
        blade(b, base, angle, len, c->p->blade_width * 0.8,
              len * 0.70, len * 0.24, i % 2 ? c->t->leaf : c->t->dark, c->t->rib, 2, LP_MAT_SOLID);
    }
    if (qlcg_pa_has_snow_target(c->def, "canopy")) {
        double at[3] = { 0.0, h * 0.50, 0.0 };
        qlcg_lp_socket(b, at, LP_SOCKET_SNOW_CANOPY);
    }
    (void)sp;
}

/* 干枯株 */
static void build_withered(LpBuilder *b, const ShuiCtx *c) {
    double h = c->h, sp = c->sp;
    LpRgb dry = qlcg_pa_tone(c->def, "withered", 1.0);
    LpRgb dry_dark = qlcg_pa_tone(c->def, "witheredDark", 1.0);
    LpRgb stem = qlcg_pa_tone(c->def, "witheredStem", 1.0);
    double origin[3] = { 0.0, 0.0, 0.0 };
    LpOpts om = qlcg_lp_opts();
    om.floor_on = 0.0; om.jitter = 0.2; om.rand = c->rng;
    qlcg_lp_dome(b, origin, sp * 0.32, h * 0.10, sp * 0.30, 5, stem, &om);
    int stems = 4 + qlcg_pa_js_round(c->p->culms * 0.4);
    if (stems > 8) stems = 8;
    for (int i = 0; i < stems; i++) {
        double angle = (double)i * GOLDEN + 0.2;
        double base[3] = { cos(angle) * sp * 0.20, h * 0.04, sin(angle) * sp * 0.20 };
        double len = h * (0.25 + 0.30 * qlcg_lp_rng_next(c->rng));
        double tip[3] = { base[0] + cos(angle) * len * 0.20, base[1] + len,
                          base[2] + sin(angle) * len * 0.20 };
        segment(b, base, tip, h * 0.010, 0.25, 3, (i % 2) ? stem : dry_dark, LP_MAT_SOLID);
        double dl = len * 0.55;
        blade(b, tip, angle, dl, sp * 0.03, dl * 0.15, dl * 0.85, dry, NO_RIB, 2, LP_MAT_SOLID);
    }
}

/* 挂点：雪与果 */
static void add_sockets(LpBuilder *b, const ShuiCtx *c) {
    const PdDef *def = c->def;
    if (c->s->frozen) {
        double at[3] = { 0.0, 0.006, 0.0 };
        qlcg_lp_socket(b, at, LP_SOCKET_SNOW_GROUND);
    }
    if (c->s->frozen && qlcg_pa_wants_snow_on(def, "canopy")) {
        double at[3] = { 0.0, c->h * 0.80, 0.0 };
        qlcg_lp_socket(b, at, LP_SOCKET_SNOW_CANOPY);
    }
    if (def->fruit) {
        int count = qlcg_pa_fruit_count_for(def, qlcg_pa_js_round(qlcg_pa_pnum(def, "fruitCount", 0)), 2);
        double ctr[3] = { 0.0, c->h * 0.55, 0.0 };
        double size = c->p->seed_size > 0 ? c->p->seed_size : 0.06;
        qlcg_pa_fruit_layout(b, def, ctr, c->sp * 0.45, count, c->rng, size, 1, NULL);
    }
}

/* 水生模型统一入口 */
void qlcg_pa_build_shui(LpBuilder *b, const PdDef *def, int season, int form,
                        const ShuiProfile *p) {
    ShuiSeason s = qlcg_pa_shui_season(def, season, p);
    ShuiTone t;
    tones_of(def, &s, &t);
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    double species_h = qlcg_pa_pnum(def, "height", 0.8);
    double species_sp = p->spread > 0 ? p->spread : qlcg_pa_pnum(def, "spread", species_h * 0.4);
    ShuiCtx c;
    c.def = def;
    c.p = p;
    c.s = &s;
    c.t = &t;
    c.rng = &rng;
    c.season = season;
    c.h = fmax(0.08, species_h) * s.height;
    c.sp = fmax(0.05, species_sp) * s.blade;
    c.h *= p->height_scale > 0 ? p->height_scale : 1.0;
    c.sp *= p->spread_scale > 0 ? p->spread_scale : 1.0;
    if (def->state == PD_STATE_SUBADULT) {
        c.h *= 0.72;
        c.sp *= 0.76;
    }
    if (form == VEG_FORM_SIMPLE) {
        double o[3] = { 0.0, 0.0, 0.0 };
        qlcg_lp_cross_quad(b, o, c.sp * 2.0, c.h, t.leaf, NULL);
        return;
    }
    if (def->state == PD_STATE_WITHERED) {
        build_withered(b, &c);
        return;
    }
    if (def->state == PD_STATE_SEEDLING) {
        build_seedling(b, &c);
        return;
    }
    if (s.dormant && p->habit != SHUI_LOTUS) {
        double spread = fmin(c.sp * 0.18, c.h * 0.30);
        double mound[3] = { 0.0, 0.0, 0.0 };
        LpRgb mud = qlcg_lp_mix(t.dry, t.stem, 0.35);
        LpOpts om = qlcg_lp_opts();
        om.floor_on = 0.0; om.jitter = 0.16; om.rand = &rng;
        qlcg_lp_dome(b, mound, spread, c.h * 0.46, spread * 0.9, 6, mud, &om);
        for (int i = 0; i < 5; i++) {
            double angle = (double)i * (TAU / 5.0) + 0.3;
            double len = c.h * (0.45 + 0.26 * qlcg_lp_rng_next(&rng));
            double base[3] = { cos(angle) * spread * 0.45, c.h * 0.02, sin(angle) * spread * 0.45 };
            double tip[3] = { base[0] + cos(angle) * len * 0.35, base[1] + len,
                              base[2] + sin(angle) * len * 0.35 };
            segment(b, base, tip, c.h * 0.008, 0.25, 3, t.dry, LP_MAT_SOLID);
        }
        for (int i = 0; i < 3; i++) {
            double angle = (double)i * 2.4 + 0.7;
            double base[3] = { cos(angle) * spread * 0.6, c.h * 0.02, sin(angle) * spread * 0.6 };
            double len = c.h * 0.32;
            blade(b, base, angle, len, c.p->blade_width * 1.4,
                  len * 0.20, len * 0.85, t.dry, NO_RIB, 2, LP_MAT_SOLID);
        }
        add_sockets(b, &c);
        return;
    }
    switch (p->habit) {
    case SHUI_SWORD: build_sword(b, &c); break;
    case SHUI_REED: build_reed(b, &c); break;
    case SHUI_SEDGE: build_sedge(b, &c); break;
    case SHUI_FLOATING:
    case SHUI_LILY:
    case SHUI_LOTUS: build_floating(b, &c); break;
    case SHUI_HYACINTH: build_hyacinth(b, &c); break;
    case SHUI_RIBBON:
    default: build_ribbon(b, &c); break;
    }
    add_sockets(b, &c);
}
