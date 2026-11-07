#include "cao_model.h"
#include "../../plant_forms.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* 草本季节结构量 */
typedef struct {
    double height, spread, leaf_len, leaf_width, density, droop, plump, dry;
    int sprout, seed_head, bloom, fruit, dormant, standing, evergreen, frozen;
} HerbSeason;

/* 草本配色 */
typedef struct {
    LpRgb leaf, dark, stem, soil, dry, bloom, fruit, rib;
} HerbTone;

typedef struct {
    double points[64][3];
    int count;
} HerbFruitSites;

/* 单株构建上下文 */
typedef struct {
    const PdDef *def;
    const HerbProfile *p;
    const HerbSeason *s;
    const HerbTone *t;
    LpRng *rng;
    HerbFruitSites *fruit_sites;
    double h, sp;
    int season;
} HerbCtx;

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
static HerbSeason season_of(const PdDef *def, int season, const HerbProfile *p) {
    int cold_level = tolerance(def, "naihan", p->cold);
    int heat_level = tolerance(def, "naire", p->heat);
    double chill = (4.0 - qlcg_pa_clampd((double)cold_level, 0.0, 4.0)) / 4.0;
    double warmth = (4.0 - qlcg_pa_clampd((double)heat_level, 0.0, 4.0)) / 4.0;
    double win = 1.0 - chill;
    double sum = 1.0 - warmth;
    double keep = qlcg_pa_clampd(p->winter_leaf, 0.02, 0.95);
    HerbSeason s;
    memset(&s, 0, sizeof(s));
    s.height = 1.0;
    s.spread = 1.0;
    s.leaf_len = 1.0;
    s.leaf_width = 1.0;
    s.density = 1.0;
    s.plump = 1.0;
    if (season == PD_SEASON_SPRING) {
        s.height = 0.70 + 0.24 * chill + 0.06 * keep;
        s.spread = 0.86 + 0.12 * chill;
        s.leaf_len = 0.72 + 0.22 * chill;
        s.leaf_width = 0.86 + 0.10 * chill;
        s.density = 0.55 + 0.30 * chill + 0.10 * keep;
        s.droop = 0.10;
        s.plump = 0.86 + 0.12 * chill;
        s.dry = 0.03;
        s.sprout = 2 + qlcg_pa_js_round(3.0 * chill);
    } else if (season == PD_SEASON_SUMMER) {
        s.height = 1.03 - 0.12 * sum;
        s.leaf_len = 1.0 - 0.10 * sum;
        s.leaf_width = 1.0 - 0.20 * sum;
        s.density = 1.0 - 0.30 * sum;
        s.droop = 0.10 + 0.55 * sum;
        s.plump = 1.10 - 0.35 * sum;
        s.dry = 0.02 + 0.30 * sum;
    } else if (season == PD_SEASON_AUTUMN) {
        s.height = 0.95 - 0.05 * win;
        s.spread = 0.97 + 0.08 * win;
        s.leaf_len = 0.88 - 0.10 * win;
        s.leaf_width = 0.92 - 0.10 * win;
        s.density = 0.78 - 0.26 * win;
        s.droop = 0.18 + 0.30 * win;
        s.plump = 0.92 - 0.15 * win;
        s.dry = 0.30 + 0.45 * win;
    } else {
        double keep_w = keep * (1.0 - 0.35 * win);
        s.evergreen = keep_w >= 0.55;
        s.height = 0.78 - 0.30 * win;
        s.spread = 0.72 - 0.18 * win;
        s.leaf_len = keep_w * (0.85 - 0.15 * win);
        s.leaf_width = keep_w * (0.80 - 0.10 * win);
        s.density = keep_w * (s.evergreen ? 0.70 : 0.80);
        s.droop = 0.35 + 0.35 * win;
        s.plump = 0.55 + 0.35 * keep_w;
        s.dry = 0.35 + 0.60 * win;
        s.standing = keep_w >= 0.10;
        s.dormant = keep_w < 0.10;
        s.frozen = frozen_climate(def);
    }
    s.bloom = p->bloom_form != HERB_HEAD_NONE && season == p->bloom_season;
    s.seed_head = p->fruit_form != HERB_HEAD_NONE && season == PD_SEASON_AUTUMN;
    if (def->state == PD_STATE_FLOWERING) s.bloom = p->bloom_form != HERB_HEAD_NONE;
    if (def->state == PD_STATE_FRUITING) {
        s.bloom = 0;
        s.seed_head = p->fruit_form != HERB_HEAD_NONE;
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
static void tones_of(const PdDef *def, const HerbSeason *s, HerbTone *t) {
    t->leaf = qlcg_pa_tone(def, "foliage", 1.0);
    t->dark = qlcg_pa_tone(def, "foliageDark", 1.0);
    if (has_tone(def, "barkLight")) {
        t->stem = qlcg_pa_tone(def, "barkLight", 1.0);
    } else if (has_tone(def, "grassDry")) {
        t->stem = qlcg_lp_mix(t->leaf, qlcg_pa_tone(def, "grassDry", 1.0), 0.45);
        t->stem.wind = LP_WIND_BARK;
    } else {
        t->stem = qlcg_lp_mix(t->leaf, dry_of(t->leaf, 0.45), 0.50);
        t->stem.wind = LP_WIND_BARK;
    }
    t->soil = has_tone(def, "soil") ? qlcg_pa_tone(def, "soil", 1.0)
                                    : qlcg_lp_deepen(t->stem, 0.55);
    t->dry = dry_of(t->leaf, 0.35 + 0.55 * s->dry);
    t->bloom = qlcg_pa_tone(def, "bloom", 1.0);
    t->fruit = qlcg_pa_tone(def, "fruit", 1.0);
    t->rib = qlcg_lp_lighten(t->leaf, 0.12);
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
static void blade_shape(LpBuilder *b, const double base[3], double bearing, double len, double wid,
                        double rise, double drop, LpRgb col, LpRgb rib, int segs, int mat,
                        const LpRgb *spine) {
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
        double w = wid * (1.0 - 0.62 * t);
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
    if (spine) {
        double tip[3] = { prev_c[0] + dx * len * 0.12, prev_c[1] + len * 0.035,
                          prev_c[2] + dz * len * 0.12 };
        qlcg_lp_triangle(b, prev_l, prev_r, tip, *spine, mat);
    }
}

static void blade(LpBuilder *b, const double base[3], double bearing, double len, double wid,
                  double rise, double drop, LpRgb col, LpRgb rib, int segs, int mat) {
    blade_shape(b, base, bearing, len, wid, rise, drop, col, rib, segs, mat, NULL);
}

static void fruit_site(const HerbCtx *c, const double at[3]) {
    HerbFruitSites *sites = c->fruit_sites;
    if (sites && sites->count < sizeof(sites->points) / sizeof(sites->points[0]))
        memcpy(sites->points[sites->count++], at, sizeof(sites->points[0]));
}

static void flower_on_stem(LpBuilder *b, const HerbCtx *c, const double at[3], double radius) {
    LpOpts o = qlcg_lp_opts();
    o.mat = LP_MAT_BLOOM;
    qlcg_lp_cone(b, at, radius * 0.23, radius * 0.23, radius * 0.40, 5, c->t->stem, &o);
    qlcg_pa_flower_at(b, c->def, at, radius, c->rng);
}

/* 干叶散列挑选 */
static int pick_dry(int index, int stride, double amount) {
    double u = fmod((double)index * 0.6180339887 + (double)stride * 0.17 + 0.31, 1.0);
    return u < amount;
}

/* 叶色：干叶与常叶 */
static LpRgb leaf_col(const HerbCtx *c, int index, int stride) {
    if (pick_dry(index, stride, c->s->dry)) return c->t->dry;
    return (index % 2) ? c->t->leaf : c->t->dark;
}

/* 小穗 */
static void spikelet(LpBuilder *b, const double at[3], double bearing, double len,
                     LpRgb col, int mat, LpRng *rng) {
    for (int i = 0; i < 2; i++) {
        LpOpts o = qlcg_lp_opts();
        o.pitch = 0.55 + 0.22 * (double)i;
        o.cup = 0.16;
        o.rand = rng;
        o.mat = mat;
        qlcg_lp_petal(b, at, bearing, len * (1.0 - 0.22 * (double)i), len * 0.20, col, &o);
    }
}

/* 籽粒 */
static void grain(LpBuilder *b, const double at[3], double size, LpRgb col, int mat) {
    double r[3] = { size, size * 0.72, size };
    LpOpts o = qlcg_lp_opts();
    o.sides = 4;
    o.rings = 1;
    o.mat = mat;
    qlcg_lp_ico(b, at, r, 4, col, &o);
}

/* 花序主轴 */
static void rachis(LpBuilder *b, const double base[3], double height, double lean,
                   double radius, LpRgb col, int mat, double tip[3]) {
    double dir[3] = { lean, 1.0, lean * 0.6 };
    double len = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    dir[0] /= len; dir[1] /= len; dir[2] /= len;
    LpOpts o = qlcg_lp_opts();
    o.sides = 3;
    o.tip_ratio = 0.5;
    o.mat = mat;
    o.cap = 0.0;
    qlcg_lp_limb(b, base, dir, height, radius, col, &o);
    tip[0] = base[0] + dir[0] * height;
    tip[1] = base[1] + dir[1] * height;
    tip[2] = base[2] + dir[2] * height;
}

/* 花穗/果穗 */
static void build_head(LpBuilder *b, const HerbCtx *c, int form, const double origin[3],
                       double spread, double size, int bloom) {
    if (form == HERB_HEAD_NONE) return;
    if (!bloom && !c->s->seed_head) return;
    LpRgb col = bloom ? c->t->bloom : c->t->dry;
    int mat = bloom ? LP_MAT_BLOOM : LP_MAT_SOLID;
    double tip[3];
    double height = size * 2.6;
    if (form == HERB_HEAD_RACEME) height = size * 6.0;
    if (form == HERB_HEAD_CYME) height = size * 4.2;
    rachis(b, origin, height, 0.06, size * 0.18, c->t->stem, mat, tip);
    if (form == HERB_HEAD_SPIKE) {
        int rows = 8;
        for (int i = 0; i < rows; i++) {
            double t = (double)(i + 1) / (double)(rows + 1);
            double at[3] = { origin[0] + (tip[0] - origin[0]) * t,
                             origin[1] + (tip[1] - origin[1]) * t,
                             origin[2] + (tip[2] - origin[2]) * t };
            double side = (i % 2) ? 1.0 : -1.0;
            spikelet(b, at, side * 1.1, size, col, mat, c->rng);
            if (i % 4 == 0) grain(b, at, size * 0.22, bloom ? c->t->bloom : c->t->dry, mat);
        }
    } else if (form == HERB_HEAD_PANICLE) {
        int branches = 5;
        for (int i = 0; i < branches; i++) {
            double t = (double)(i + 1) / (double)(branches + 1);
            double angle = (double)i * GOLDEN + 0.4;
            double blen = spread * (0.85 - 0.35 * t);
            double node[3] = { origin[0] + (tip[0] - origin[0]) * t,
                               origin[1] + (tip[1] - origin[1]) * t,
                               origin[2] + (tip[2] - origin[2]) * t };
            double end[3] = { node[0] + cos(angle) * blen,
                              node[1] + blen * 0.55 + size * 0.4,
                              node[2] + sin(angle) * blen };
            segment(b, node, end, size * 0.09, 0.45, 3, col, mat);
            fruit_site(c, end);
            for (int j = 0; j < 2; j++) {
                double u = 0.78 + 0.22 * j;
                double at[3] = { node[0] + (end[0] - node[0]) * u,
                                 node[1] + (end[1] - node[1]) * u,
                                 node[2] + (end[2] - node[2]) * u };
                spikelet(b, at, angle, size * 0.9, col, mat, c->rng);
            }
        }
        grain(b, tip, size * 0.24, bloom ? c->t->bloom : c->t->dry, mat);
    } else if (form == HERB_HEAD_RACEME) {
        int nodes = bloom ? 4 : 3;
        for (int i = 0; i < nodes; i++) {
            double t = (double)(i + 1) / (double)(nodes + 1);
            double at[3] = { origin[0] + (tip[0] - origin[0]) * t,
                             origin[1] + (tip[1] - origin[1]) * t,
                             origin[2] + (tip[2] - origin[2]) * t };
            double side = (i % 2) ? 1.0 : -1.0;
            double ped[3] = { at[0] + side * size * 0.30, at[1] + size * 0.16, at[2] };
            segment(b, at, ped, size * 0.06, 0.5, 3, c->t->stem, mat);
            if (bloom) {
                flower_on_stem(b, c, ped, size * 0.62);
            } else {
                spikelet(b, ped, side * 1.2, size * 0.8, col, mat, c->rng);
                grain(b, ped, size * 0.20, c->t->dry, mat);
            }
        }
    } else if (form == HERB_HEAD_CYME) {
        for (int i = 0; i < 3; i++) {
            double angle = (double)i * (TAU / 3.0) + 0.5;
            double at[3] = { tip[0] + cos(angle) * spread * 0.28,
                             tip[1] - spread * 0.16,
                             tip[2] + sin(angle) * spread * 0.28 };
            segment(b, tip, at, size * 0.06, 0.5, 3, c->t->stem, mat);
            if (bloom) {
                qlcg_pa_flower_at(b, c->def, at, size * 0.60, c->rng);
            } else {
                spikelet(b, at, angle, size * 0.7, col, mat, c->rng);
                grain(b, at, size * 0.20, c->t->dry, mat);
            }
        }
    } else if (form == HERB_HEAD_DIGITATE) {
        for (int i = 0; i < 4; i++) {
            double angle = 1.5707963 + ((double)i - 1.5) * 0.42;
            double dir[3] = { cos(angle) * 0.9, 0.55, sin(angle) * 0.9 };
            double dlen = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            dir[0] /= dlen; dir[1] /= dlen; dir[2] /= dlen;
            LpOpts o = qlcg_lp_opts();
            o.sides = 3; o.tip_ratio = 0.4; o.mat = mat; o.cap = 0.0;
            double blen = size * 3.4;
            qlcg_lp_limb(b, tip, dir, blen, size * 0.16, col, &o);
            for (int j = 1; j <= 4; j++) {
                double t = (double)j / 4.0;
                double at[3] = { tip[0] + dir[0] * blen * t,
                                 tip[1] + dir[1] * blen * t,
                                 tip[2] + dir[2] * blen * t };
                spikelet(b, at, angle + (j % 2 ? 0.9 : -0.9), size * 0.7, col, mat, c->rng);
                if (bloom) grain(b, at, size * 0.18, c->t->bloom, LP_MAT_BLOOM);
            }
        }

    }
}

/* 果实网格 */
static void draw_fruit(LpBuilder *b, const PdDef *def, const double at[3], double radius, LpRng *rng) {
    double r[3] = { radius, radius * 0.85, radius };
    LpOpts o = qlcg_lp_opts();
    o.sides = 5; o.rings = 1; o.mat = LP_MAT_FRUIT; o.jitter = 0.16; o.rand = rng;
    qlcg_lp_ico(b, at, r, 5, qlcg_pa_tone(def, "fruit", 1.0), &o);
}

/* 丛生禾草 */
static void build_tussock(LpBuilder *b, const HerbCtx *c) {
    double h = c->h, sp = c->sp;
    double origin[3] = { 0.0, 0.0, 0.0 };
    qlcg_lp_prism(b, origin, sp * 0.30, sp * 0.30, 0.0, h * 0.07, 6, c->t->soil, NULL);
    int blades = qlcg_pa_js_round((double)c->p->blades * c->s->density);
    if (blades < 3) blades = 3;
    int sheaths = blades / 3;
    for (int i = 0; i < blades; i++) {
        double angle = (double)i * GOLDEN + (qlcg_lp_rng_next(c->rng) - 0.5) * 0.5;
        double ring = 0.18 + 0.82 * (double)(i % 4) / 3.0;
        double len = h * c->p->leaf_len * (0.72 + 0.38 * ring) * (0.85 + 0.25 * qlcg_lp_rng_next(c->rng));
        double wid = c->p->leaf_width * c->s->plump * (0.85 + 0.3 * qlcg_lp_rng_next(c->rng));
        double base[3] = { cos(angle) * sp * 0.22 * ring, h * 0.05, sin(angle) * sp * 0.22 * ring };
        double rise = len * (0.92 - 0.38 * c->s->droop) * c->p->arch;
        double drop = len * (0.10 + 1.05 * c->s->droop);
        blade(b, base, angle, len, wid, rise, drop, leaf_col(c, i, 1), c->t->rib, 3, LP_MAT_SOLID);
        if (i < sheaths) {
            double so[3] = { base[0] * 0.6, 0.0, base[2] * 0.6 };
            LpOpts o = qlcg_lp_opts();
            o.cap = 0.0; o.jitter = 0.12; o.rand = c->rng;
            qlcg_lp_prism(b, so, sp * 0.11, sp * 0.11, 0.0, h * 0.15, 3, c->t->stem, &o);
        }
    }
    for (int i = 0; i < c->s->sprout; i++) {
        double angle = (double)i * (TAU / (double)c->s->sprout) + 0.4;
        double pos[3] = { cos(angle) * sp * 0.10, h * 0.03, sin(angle) * sp * 0.10 };
        LpOpts o = qlcg_lp_opts();
        o.sides = 4; o.mat = LP_MAT_SOLID; o.rand = c->rng;
        qlcg_lp_cone(b, pos, h * 0.035, h * 0.035, h * 0.20, 4, c->t->rib, &o);
    }
    int tillers = 3 + qlcg_pa_js_round(3.0 * c->s->density);
    int winter_tillers = c->season == PD_SEASON_WINTER && !(c->s->frozen && c->s->standing);
    for (int i = 0; i < tillers; i++) {
        double angle = (double)i * (TAU / (double)tillers) + 0.2;
        double pos[3] = { cos(angle) * sp * 0.20, 0.0, sin(angle) * sp * 0.20 };
        LpOpts o = qlcg_lp_opts();
        o.rings = 1; o.squash = 0.5; o.jitter = 0.15; o.rand = c->rng;
        qlcg_lp_dome(b, pos, sp * 0.16, h * 0.10, sp * 0.15, 5, c->t->stem, &o);
        if (winter_tillers) {
            double top[3] = { pos[0], h * 0.10, pos[2] };
            fruit_site(c, top);
        }
    }
    if (c->s->frozen && c->s->standing) {
        int culms = 4 + qlcg_pa_js_round(3.0 * c->s->density);
        if (c->fruit_sites) culms++;
        for (int i = 0; i < culms; i++) {
            double angle = (double)i * GOLDEN + 0.2;
            double base[3] = { cos(angle) * sp * 0.16, 0.0, sin(angle) * sp * 0.16 };
            double tip[3] = { base[0] + cos(angle) * sp * 0.12,
                              h * (0.55 + 0.10 * (double)(i % 3)),
                              base[2] + sin(angle) * sp * 0.12 };
            segment(b, base, tip, h * 0.006, 0.30, 3, c->t->dry, LP_MAT_SOLID);
            fruit_site(c, tip);
        }
    }
    int heads = 1;
    if (c->s->density > 0.70) heads = 2;
    if (c->p->blades >= 12) heads = 3;
    if (c->s->dormant) heads = 0;
    int form = c->s->bloom ? c->p->bloom_form : c->p->fruit_form;
    if (form == HERB_HEAD_NONE) form = c->p->bloom_form;
    int has_head = (c->s->bloom || c->s->seed_head) && form != HERB_HEAD_NONE;
    if (!has_head && c->fruit_sites && c->season != PD_SEASON_WINTER) heads = 6;
    else if (!has_head) heads = 0;
    for (int i = 0; i < heads; i++) {
        double angle = (double)i * (TAU / (double)heads) + 0.35;
        double base[3] = { cos(angle) * sp * 0.10, h * 0.035, sin(angle) * sp * 0.10 };
        double top[3] = { cos(angle) * sp * 0.16, h * (0.72 + 0.04 * (i % 3)),
                          sin(angle) * sp * 0.16 };
        segment(b, base, top, h * 0.008, 0.6, 3, c->t->stem, LP_MAT_SOLID);
        if (has_head) build_head(b, c, form, top, sp * 0.55, c->p->bloom_size, c->s->bloom);
        else fruit_site(c, top);
    }
}

/* 匍匐走茎 */
static void build_stolon(LpBuilder *b, const HerbCtx *c) {
    double h = c->h, sp = c->sp;
    int runners = c->p->stems > 0 ? c->p->stems : 5;
    runners = 2 + qlcg_pa_js_round((double)(runners - 2) * (0.40 + 0.60 * c->s->density));
    if (c->s->dormant) runners = 2;
    int leaves_per = 1 + qlcg_pa_js_round(1.5 * c->s->density);
    for (int r = 0; r < runners; r++) {
        double angle = (double)r * (TAU / (double)runners) + 0.25;
        int nodes = c->s->dormant ? 2 : 2 + qlcg_pa_js_round(2.0 * c->s->density);
        double prev[3] = { 0.0, 0.015, 0.0 };
        for (int n = 1; n <= nodes; n++) {
            double t = (double)n / (double)nodes;
            double rad = sp * 0.95 * t;
            double next[3] = { cos(angle) * rad, 0.02 + h * 0.05 * t * t, sin(angle) * rad };
            segment(b, prev, next, h * 0.016, 0.75, 3, c->t->stem, LP_MAT_SOLID);
            if (!c->s->dormant) {
                for (int l = 0; l < leaves_per; l++) {
                    double la = angle + ((double)l - 1.0) * 1.2 + TAU * 0.25;
                    double lb[3] = { next[0], next[1] + h * 0.02, next[2] };
                    double len = h * 0.34 * c->s->leaf_len * (0.8 + 0.3 * qlcg_lp_rng_next(c->rng));
                    blade(b, lb, la, len, c->p->leaf_width * c->s->plump,
                          len * 0.82 * c->p->arch, len * (0.2 + 0.7 * c->s->droop),
                          leaf_col(c, r * 4 + n, 3), c->t->rib, 2, LP_MAT_SOLID);
                }
            }
            memcpy(prev, next, sizeof(next));
        }
        if ((c->s->bloom || c->s->seed_head) && (r % 2 == 0)) {
            double head[3] = { prev[0], prev[1] + h * 0.12, prev[2] };
            segment(b, prev, head, h * 0.014, 0.8, 3, c->t->stem, LP_MAT_SOLID);
            rachis(b, head, h * 0.20, 0.02, h * 0.012, c->t->stem, LP_MAT_SOLID, head);
            for (int d = 0; d < 4; d++) {
                double da = 1.5707963 + ((double)d - 1.5) * 0.40;
                double dir[3] = { cos(da) * 0.85, 0.5, sin(da) * 0.85 };
                double dl = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                dir[0] /= dl; dir[1] /= dl; dir[2] /= dl;
                LpOpts o = qlcg_lp_opts();
                o.sides = 3; o.tip_ratio = 0.45; o.mat = LP_MAT_SOLID; o.cap = 0.0;
                double blen = h * 0.16;
                qlcg_lp_limb(b, head, dir, blen, h * 0.008, c->t->dry, &o);
                for (int j = 1; j <= 3; j++) {
                    double at[3] = { head[0] + dir[0] * blen * (double)j / 3.0,
                                     head[1] + dir[1] * blen * (double)j / 3.0,
                                     head[2] + dir[2] * blen * (double)j / 3.0 };
                    spikelet(b, at, da + (j % 2 ? 0.8 : -0.8), h * 0.06,
                             c->t->bloom, LP_MAT_BLOOM, c->rng);
                    if (j == 3) fruit_site(c, at);
                }
            }
        } else if (!c->s->bloom && !c->s->seed_head) fruit_site(c, prev);
        if (c->season == PD_SEASON_SPRING) {
            double pos[3] = { prev[0], prev[1] + h * 0.02, prev[2] };
            LpOpts o = qlcg_lp_opts();
            o.sides = 4; o.mat = LP_MAT_SOLID; o.cap = 0.0; o.rand = c->rng;
            qlcg_lp_cone(b, pos, h * 0.03, h * 0.03, h * 0.14, 4, c->t->rib, &o);
        }
    }
}

/* 蕨类 */
static void build_fern(LpBuilder *b, const HerbCtx *c) {
    double h = c->h, sp = c->sp;
    double origin[3] = { 0.0, 0.0, 0.0 };
    LpOpts ro = qlcg_lp_opts();
    ro.sides = 5; ro.cap = 0.0; ro.jitter = 0.2; ro.rand = c->rng;
    qlcg_lp_prism(b, origin, sp * 0.16, sp * 0.16, 0.0, h * 0.10, 5, c->t->stem, &ro);
    int fronds = c->p->leaves + qlcg_pa_js_round(4.0 * c->s->density);
    if (c->s->dormant) fronds = 2;
    for (int f = 0; f < fronds; f++) {
        double angle = (double)f * GOLDEN + 0.3;
        double start[3] = { cos(angle) * sp * 0.12, h * 0.06, sin(angle) * sp * 0.12 };
        double reach = sp * (0.72 + 0.25 * qlcg_lp_rng_next(c->rng));
        double rise = h * (0.86 - 0.25 * c->s->droop) * c->p->arch;
        double tip[3] = { start[0] + cos(angle) * reach, start[1] + rise, start[2] + sin(angle) * reach };
        segment(b, start, tip, h * 0.018, 0.35, 3, c->t->stem, LP_MAT_SOLID);
        int pinnae = c->s->dormant ? 2 : 3 + (int)ceil(5.0 * c->s->density);
        for (int i = 0; i < pinnae; i++) {
            double denom = pinnae > 1 ? (double)(pinnae - 1) : 1.0;
            double t = 0.22 + 0.66 * (double)i / denom;
            double node[3] = { start[0] + (tip[0] - start[0]) * t,
                               start[1] + (tip[1] - start[1]) * t,
                               start[2] + (tip[2] - start[2]) * t };
            double plen = reach * (0.34 - 0.10 * t) * (0.8 + 0.3 * qlcg_lp_rng_next(c->rng));
            for (int side = -1; side <= 1; side += 2) {
                double dir = angle + side * 1.25;
                blade(b, node, dir, plen, plen * 0.34, plen * 0.24, plen * 0.30,
                      leaf_col(c, f * 7 + i * 2 + side, 5), c->t->rib, 2, LP_MAT_SOLID);
            }
        }
    }
    for (int i = 0; i < c->s->sprout; i++) {
        double angle = (double)i * (TAU / (double)c->s->sprout) + 1.1;
        double pos[3] = { cos(angle) * sp * 0.12, h * 0.05, sin(angle) * sp * 0.12 };
        LpOpts o = qlcg_lp_opts();
        o.sides = 4; o.radius_ratio = 0.55; o.mat = LP_MAT_SOLID; o.rand = c->rng;
        qlcg_lp_cone(b, pos, h * 0.05, h * 0.05, h * 0.26, 4, c->t->rib, &o);
    }
}

/* 直立草本 */
static void build_forb(LpBuilder *b, const HerbCtx *c) {
    double h = c->h, sp = c->sp;
    int stems = c->p->stems > 0 ? c->p->stems : 3;
    for (int s = 0; s < stems; s++) {
        double angle = (double)s * GOLDEN + 0.2;
        double base[3] = { cos(angle) * sp * 0.10, 0.0, sin(angle) * sp * 0.10 };
        double top[3] = { base[0] + cos(angle) * sp * 0.22,
                          h * (0.78 + 0.12 * (double)(s % 2)),
                          base[2] + sin(angle) * sp * 0.22 };
        segment(b, base, top, h * 0.014, 0.55, 4, c->t->stem, LP_MAT_SOLID);
        int nodes = c->s->dormant ? 1 : 1 + qlcg_pa_js_round(3.5 * c->s->density);
        for (int n = 0; n < nodes; n++) {
            double denom = nodes > 1 ? (double)(nodes - 1) : 1.0;
            double t = 0.35 + 0.55 * (double)n / denom;
            double node[3] = { base[0] + (top[0] - base[0]) * t,
                               base[1] + (top[1] - base[1]) * t,
                               base[2] + (top[2] - base[2]) * t };
            double len = h * 0.26 * c->s->leaf_len * (0.85 + 0.3 * qlcg_lp_rng_next(c->rng));
            for (int side = -1; side <= 1; side += 2) {
                double dir = angle + side * 1.35;
                blade(b, node, dir, len, c->p->leaf_width * c->s->plump,
                      len * 0.40 * c->p->arch, len * (0.25 + 0.5 * c->s->droop),
                      leaf_col(c, s * 5 + n * 2 + side, 7), c->t->rib, 3, LP_MAT_SOLID);
            }
        }
        if (c->s->bloom) {
            flower_on_stem(b, c, top, c->p->bloom_size);
            fruit_site(c, top);
        } else if (c->s->seed_head) {
            for (int f = 0; f < 3; f++) {
                double fa = angle + (double)f * 1.3;
                double fp[3] = { top[0] + cos(fa) * sp * 0.10, top[1] - h * 0.04, top[2] + sin(fa) * sp * 0.10 };
                segment(b, top, fp, h * 0.008, 0.5, 3, c->t->stem, LP_MAT_SOLID);
                spikelet(b, fp, fa, h * 0.05, c->t->dry, LP_MAT_SOLID, c->rng);
                fruit_site(c, fp);
            }
        } else fruit_site(c, top);
    }
}

/* 多刺灌木状草本 */
static void build_bramble(LpBuilder *b, const HerbCtx *c) {
    double h = c->h, sp = c->sp;
    int canes = c->p->stems > 0 ? c->p->stems : 5;
    canes = 2 + qlcg_pa_js_round((double)(canes - 2) * (0.45 + 0.55 * c->s->density));
    int leaves_per = 1 + qlcg_pa_js_round(1.0 * c->s->density);
    for (int k = 0; k < canes; k++) {
        double angle = (double)k * GOLDEN + 0.15;
        double prev[3] = { 0.0, 0.0, 0.0 };
        int segs = c->s->dormant ? 2 : 3 + qlcg_pa_js_round(c->s->density);
        for (int s = 0; s < segs; s++) {
            double t = (double)(s + 1) / (double)segs;
            double dir = angle + ((s % 2) ? 1.0 : -1.0) * 1.02;
            double next[3] = { prev[0] + cos(dir) * sp * 0.34,
                               prev[1] + h * 0.30 * (1.0 - 0.12 * t),
                               prev[2] + sin(dir) * sp * 0.34 };
            segment(b, prev, next, h * 0.018 * (1.0 - 0.35 * t), 0.6, 4,
                    s == 0 ? c->t->stem : c->t->dry, LP_MAT_SOLID);
            if (!c->s->dormant) {
                for (int l = 0; l < leaves_per; l++) {
                    double la = dir + ((double)l ? 1.0 : -1.0) * 1.2;
                    double len = h * 0.16 * c->s->leaf_len;
                    blade(b, next, la, len, c->p->leaf_width * c->s->plump,
                          len * 0.3 * c->p->arch, len * (0.2 + 0.6 * c->s->droop),
                          leaf_col(c, k * 6 + s * 2 + l, 9), NO_RIB, 2, LP_MAT_SOLID);
                }
            }
            double side_dir = dir + 1.5707963;
            double thorn[3] = { next[0] + cos(side_dir) * h * 0.035,
                                next[1] + h * 0.02,
                                next[2] + sin(side_dir) * h * 0.035 };
            segment(b, next, thorn, h * 0.006, 0.05, 3, c->t->rib, LP_MAT_SOLID);
            memcpy(prev, next, sizeof(next));
        }
        if (c->s->bloom) {
            double at[3] = { prev[0], prev[1] + h * 0.02, prev[2] };
            qlcg_pa_flower_at(b, c->def, at, c->p->bloom_size, c->rng);
        }
    }
}

/* 剑叶莲座 */
static void build_strap(LpBuilder *b, const HerbCtx *c) {
    double h = c->h, sp = c->sp;
    double origin[3] = { 0.0, 0.0, 0.0 };
    qlcg_lp_prism(b, origin, sp * 0.20, sp * 0.20, 0.0, h * 0.06, 5, c->t->soil, NULL);
    int leaves = qlcg_pa_js_round((double)c->p->blades * 2.20 * c->s->density);
    if (leaves < 3) leaves = 3;
    for (int i = 0; i < leaves; i++) {
        double angle = (double)i * GOLDEN;
        double layer = (double)(i % 3) / 2.0;
        double len = h * c->p->leaf_len * (1.02 - 0.22 * layer) * (0.9 + 0.15 * qlcg_lp_rng_next(c->rng));
        double wid = c->p->leaf_width * c->s->plump * (1.10 - 0.25 * layer);
        double base[3] = { cos(angle) * sp * 0.10, h * 0.03, sin(angle) * sp * 0.10 };
        double lift = (1.30 - 0.55 * layer) * (1.0 - 0.45 * c->s->droop) * c->p->arch;
        blade(b, base, angle, len, wid, len * lift, len * (0.10 + 0.55 * c->s->droop),
              leaf_col(c, i, 2), c->t->rib, 3, LP_MAT_SOLID);
    }
    int form = c->s->bloom ? c->p->bloom_form : c->p->fruit_form;
    if ((c->s->bloom || c->s->seed_head) && form != HERB_HEAD_NONE) {
        double base[3] = { 0.0, h * 0.03, 0.0 };
        double top[3] = { 0.0, h * 0.55, 0.0 };
        segment(b, base, top, h * 0.013, 0.65, 4, c->t->stem, LP_MAT_SOLID);
        build_head(b, c, form, top, sp * 0.5, c->p->bloom_size, c->s->bloom);
    }
    if (c->season == PD_SEASON_SPRING) {
        for (int i = 0; i < 3; i++) {
            double angle = (double)i * (TAU / 3.0) + 0.2;
            double pos[3] = { cos(angle) * sp * 0.12, h * 0.06, sin(angle) * sp * 0.12 };
            LpOpts o = qlcg_lp_opts();
            o.sides = 4; o.mat = LP_MAT_SOLID; o.cap = 0.0; o.rand = c->rng;
            qlcg_lp_cone(b, pos, sp * 0.06, sp * 0.06, h * 0.22, 4, c->t->rib, &o);
        }
    } else if (c->season == PD_SEASON_AUTUMN) {
        for (int i = 0; i < 2; i++) {
            double angle = (double)i * 2.8 + 1.0;
            double base[3] = { cos(angle) * sp * 0.18, h * 0.05, sin(angle) * sp * 0.18 };
            double len = h * 0.70;
            blade(b, base, angle, len, c->p->leaf_width * 0.9,
                  len * 0.40, len * 0.60, c->t->dry, NO_RIB, 2, LP_MAT_SOLID);
        }
    }
}

/* 螺旋莲座 */
static void build_rosette(LpBuilder *b, const HerbCtx *c) {
    double h = c->h, sp = c->sp;
    double origin[3] = { 0.0, 0.0, 0.0 };
    qlcg_lp_prism(b, origin, sp * 0.26, sp * 0.26, 0.0, h * 0.10, 6, c->t->soil, NULL);
    int leaves = qlcg_pa_js_round((double)c->p->leaves * c->s->density);
    if (leaves < 5) leaves = 5;
    for (int i = 0; i < leaves; i++) {
        double angle = (double)i * GOLDEN;
        double layer = (double)(i % 4) / 3.0;
        double len = h * c->p->leaf_len * (1.05 - 0.30 * layer) * (0.88 + 0.20 * qlcg_lp_rng_next(c->rng));
        double wid = c->p->leaf_width * c->s->plump * (1.20 - 0.30 * layer);
        double base[3] = { cos(angle) * sp * 0.10, h * 0.05 + layer * h * 0.04, sin(angle) * sp * 0.10 };
        double lift = (1.05 - 0.55 * layer) * (1.0 - 0.50 * c->s->droop) * c->p->arch;
        blade_shape(b, base, angle, len, wid, len * lift, len * (0.12 + 0.60 * c->s->droop),
                    leaf_col(c, i, 4), c->t->rib, 3, LP_MAT_SOLID, &c->t->rib);
    }
    int form = c->s->bloom ? c->p->bloom_form : c->p->fruit_form;
    if ((c->s->bloom || c->s->seed_head) && form != HERB_HEAD_NONE) {
        double base[3] = { 0.0, h * 0.05, 0.0 };
        double top[3] = { 0.0, h * 0.75, 0.0 };
        segment(b, base, top, h * 0.017, 0.65, 4, c->t->stem, LP_MAT_SOLID);
        build_head(b, c, form, top, sp * 0.55, c->p->bloom_size, c->s->bloom);
    }
    if (c->season == PD_SEASON_SPRING) {
        for (int i = 0; i < 3; i++) {
            double angle = (double)i * (TAU / 3.0) + 0.4;
            double pos[3] = { cos(angle) * sp * 0.10, h * 0.10, sin(angle) * sp * 0.10 };
            LpOpts o = qlcg_lp_opts();
            o.sides = 4; o.mat = LP_MAT_SOLID; o.cap = 0.0; o.rand = c->rng;
            qlcg_lp_cone(b, pos, sp * 0.07, sp * 0.07, h * 0.20, 4, c->t->rib, &o);
        }
    } else if (c->season == PD_SEASON_AUTUMN) {
        for (int i = 0; i < 2; i++) {
            double angle = (double)i * 2.6 + 0.8;
            double base[3] = { cos(angle) * sp * 0.16, h * 0.06, sin(angle) * sp * 0.16 };
            double len = h * 0.55;
            blade(b, base, angle, len, c->p->leaf_width * 0.9,
                  len * 0.35, len * 0.55, c->t->dry, NO_RIB, 2, LP_MAT_SOLID);
        }
    }
}

/* 贴地叶状地衣 */
static void build_cushion(LpBuilder *b, const HerbCtx *c, int form) {
    enum { BANDS = 6, MAX_SECTORS = 40 };
    static const int SECTORS[] = {36, 40, 34, 28};
    int simple = form == VEG_FORM_SIMPLE;
    int young = c->def->state == PD_STATE_SEEDLING;
    int dead = c->def->state == PD_STATE_WITHERED;
    int sectors = simple ? 12 : young ? 24 : SECTORS[c->season];
    int bands = simple ? 2 : BANDS;
    double spread = c->sp * (young ? 0.42 : dead ? 0.68 : 1.0);
    double depth = spread * (dead ? 0.060 : 0.12) * c->p->plump;
    int lobes = c->p->cushion_lobes;
    double phase = qlcg_lp_rng_next(c->rng) * TAU;
    double center[3] = {0.0, depth * 0.40, 0.0};
    double surface[BANDS][MAX_SECTORS][3];
    LpRgb thallus = qlcg_lp_rgb(151, 159, 125);
    thallus = qlcg_lp_mix(thallus, c->t->leaf, 0.20);
    thallus = qlcg_lp_mix(thallus, qlcg_lp_rgb(126, 111, 86), dead ? 0.75 : c->s->dry * 0.36);
    LpRgb rim = qlcg_lp_mix(thallus, qlcg_lp_rgb(211, 213, 181), 0.44);
    LpRgb underside = qlcg_lp_deepen(thallus, 0.30);
    for (int i = 0; i < sectors; i++) {
        double a = TAU * i / sectors + phase;
        double lobe = 0.73 + 0.16 * cos(lobes * a + phase) + 0.08 * sin(3 * a - phase);
        double length = spread * lobe * (0.94 + 0.12 * qlcg_lp_rng_next(c->rng));
        for (int band = 0; band < bands; band++) {
            double t = (double)(band + 1) / bands;
            double r = length * t;
            double *v = surface[band][i];
            v[0] = cos(a) * r;
            v[2] = sin(a) * r * 0.82;
            v[1] = depth * (0.24 + 0.40 * sin(8 * t + 4 * a) * t * t
                           + 0.28 * (0.5 + 0.5 * cos(lobes * a)) + 0.27 * t * t);
        }
    }
    for (int i = 0; i < sectors; i++) {
        int j = (i + 1) % sectors;
        qlcg_lp_triangle(b, center, surface[0][j], surface[0][i], thallus, LP_MAT_SOLID);
        for (int band = 1; band < bands; band++) {
            LpRgb col = band == bands - 1 ? rim : qlcg_lp_deepen(thallus, 0.04 * ((i + band * 2) % 5));
            qlcg_lp_quad(b, surface[band - 1][i], surface[band - 1][j],
                         surface[band][j], surface[band][i], col, LP_MAT_SOLID);
        }
        const double *a = surface[bands - 1][i], *z = surface[bands - 1][j];
        double low_a[3] = {a[0], 0.0, a[2]}, low_z[3] = {z[0], 0.0, z[2]};
        qlcg_lp_quad(b, a, z, low_z, low_a, underside, LP_MAT_SOLID);
    }
    if (c->s->bloom && !simple && !young && !dead) {
        LpRgb disc = qlcg_lp_rgb(117, 71, 44);
        for (int cup = 0; cup < 5; cup++) {
            const double *base = surface[2 + cup % 2][(cup * 7 + 2) % sectors];
            double radius = spread * (0.036 + 0.008 * (cup % 3));
            double bowl[3] = {base[0], base[1] + depth * 0.17, base[2]};
            for (int i = 0; i < 6; i++) {
                double a = TAU * i / 6, z = TAU * (i + 1) / 6;
                double l[3] = {base[0] + cos(a) * radius, base[1] + depth * 0.31,
                               base[2] + sin(a) * radius};
                double r[3] = {base[0] + cos(z) * radius, l[1], base[2] + sin(z) * radius};
                qlcg_lp_triangle(b, base, l, r, rim, LP_MAT_SOLID);
                qlcg_lp_triangle(b, bowl, r, l, disc, LP_MAT_SOLID);
            }
        }
    }
    if (!simple && (c->s->frozen || young)) qlcg_lp_socket(b, center, LP_SOCKET_SNOW_CANOPY);
}

/* 肉质盐生草 */
static void build_suaeda(LpBuilder *b, const HerbCtx *c) {
    double h = c->h, sp = c->sp;
    int stems = c->p->stems > 0 ? c->p->stems : 4;
    for (int s = 0; s < stems; s++) {
        double angle = (double)s * GOLDEN + 0.2;
        double prev[3] = { cos(angle) * sp * 0.10, 0.0, sin(angle) * sp * 0.10 };
        int joints = c->s->dormant ? 2 : 2 + qlcg_pa_js_round(3.0 * c->s->density);
        for (int j = 0; j < joints; j++) {
            double t = (double)(j + 1) / (double)joints;
            double next[3] = { prev[0] + cos(angle) * sp * 0.10,
                               prev[1] + h * 0.86 / (double)joints,
                               prev[2] + sin(angle) * sp * 0.10 };
            double rad = h * 0.022 * c->s->plump * (1.0 - 0.10 * t);
            segment(b, prev, next, rad, 0.85, 5,
                    pick_dry(s * 5 + j, 6, c->s->dry) ? c->t->dry : c->t->leaf, LP_MAT_SOLID);
            if (!c->s->dormant) {
                for (int l = -1; l <= 1; l += 2) {
                    double la = angle + (double)l * 1.2;
                    double len = h * 0.10 * c->s->leaf_len;
                    blade(b, next, la, len, h * 0.012 * c->s->plump,
                          len * 0.30, len * 0.20, c->t->dark, c->t->rib, 1, LP_MAT_SOLID);
                }
            }
            if (j == joints - 1) {
                if (c->s->bloom || c->s->seed_head) {
                    for (int k = 0; k < 3; k++) {
                        double u = 1.0 - 0.30 * k;
                        double at[3] = { prev[0] + (next[0] - prev[0]) * u,
                                         prev[1] + (next[1] - prev[1]) * u,
                                         prev[2] + (next[2] - prev[2]) * u };
                        spikelet(b, at, angle + 1.57, h * 0.055,
                                 c->s->bloom ? c->t->bloom : c->t->dry,
                                 c->s->bloom ? LP_MAT_BLOOM : LP_MAT_SOLID, c->rng);
                        fruit_site(c, at);
                    }
                } else fruit_site(c, next);
            }
            memcpy(prev, next, sizeof(next));
        }
    }
}

/* 关节肉质茎 */
static void build_salicornia(LpBuilder *b, const HerbCtx *c) {
    double h = c->h, sp = c->sp;
    int stems = c->p->stems > 0 ? c->p->stems : 5;
    for (int s = 0; s < stems; s++) {
        double angle = (double)s * GOLDEN + 0.3;
        double base[3] = { cos(angle) * sp * 0.16, 0.0, sin(angle) * sp * 0.16 };
        int joints = c->s->dormant ? 2 : 2 + (int)ceil(3.0 * c->s->density);
        double y = 0.0;
        for (int j = 0; j < joints; j++) {
            double jh = h * 0.80 / (double)joints;
            double rad = h * 0.028 * c->s->plump * (1.0 - 0.08 * (double)j);
            double off[3] = { base[0] + cos(angle) * sp * 0.03 * (double)j, y,
                              base[2] + sin(angle) * sp * 0.03 * (double)j };
            LpOpts o = qlcg_lp_opts();
            o.jitter = 0.10; o.rand = c->rng;
            qlcg_lp_prism(b, off, rad, rad, 0.0, jh, 5,
                          pick_dry(s * 5 + j, 8, c->s->dry) ? c->t->dry : c->t->leaf, &o);
            if (s == 0 && j == joints - 1 && (c->s->bloom || c->s->seed_head)) {
                double top[3] = { off[0], off[1] + jh, off[2] };
                int form = c->s->bloom ? c->p->bloom_form : c->p->fruit_form;
                build_head(b, c, form, top, sp * 0.20, h * 0.06, c->s->bloom);
            }
            if (!c->s->dormant) {
                for (int l = -1; l <= 1; l += 2) {
                    double la = angle + (double)l * 1.35;
                    double len = h * 0.09;
                    blade(b, off, la, len, h * 0.010, len * 0.25, len * 0.18,
                          c->t->dark, c->t->rib, 1, LP_MAT_SOLID);
                }
            }
            y += jh * 0.92;
        }
        if (c->s->frozen && c->s->standing) {
            for (int t = 0; t < 2; t++) {
                double ta = angle + ((double)t ? 1.0 : -1.0) * 1.1;
                double base2[3] = { base[0], y * 0.45, base[2] };
                double tip2[3] = { base2[0] + cos(ta) * sp * 0.12,
                                   base2[1] + h * 0.16, base2[2] + sin(ta) * sp * 0.12 };
                segment(b, base2, tip2, h * 0.008, 0.25, 3, c->t->dry, LP_MAT_SOLID);
            }
        }
        if (c->season == PD_SEASON_AUTUMN) {
            for (int t = 0; t < 2; t++) {
                double ta = angle + ((double)t ? 1.0 : -1.0) * 1.2;
                double lb[3] = { base[0], y * 0.55, base[2] };
                double len = h * 0.14;
                blade(b, lb, ta, len, h * 0.014, len * 0.30, len * 0.50,
                      c->t->dry, NO_RIB, 2, LP_MAT_SOLID);
            }
        }
    }
}

/* 幼苗 */
static void build_seedling(LpBuilder *b, const HerbCtx *c) {
    double h = c->h * 0.30;
    double sp = c->sp * 0.45;
    double root[3] = { 0.0, 0.0, 0.0 };
    double crown[3] = { 0.0, h * 0.24, 0.0 };
    segment(b, root, crown, h * 0.030, 0.7, 4, c->t->stem, LP_MAT_SOLID);
    for (int i = 0; i < 4; i++) {
        double angle = (double)i * GOLDEN;
        double base[3] = { 0.0, h * 0.20, 0.0 };
        double len = h * (0.55 + 0.15 * (double)(i % 2));
        blade(b, base, angle, len, c->p->leaf_width * 0.7,
              len * 0.62 * c->p->arch, len * 0.30, i % 2 ? c->t->leaf : c->t->dark,
              c->t->rib, 2, LP_MAT_SOLID);
    }
    if (qlcg_pa_has_snow_target(c->def, "canopy") ||
        qlcg_pa_has_snow_target(c->def, "top")) {
        double at[3] = { 0.0, h * 0.55, 0.0 };
        qlcg_lp_socket(b, at, LP_SOCKET_SNOW_CANOPY);
    }
}

/* 干枯株 */
static void build_withered(LpBuilder *b, const HerbCtx *c) {
    double h = c->h, sp = c->sp;
    LpRgb dry = qlcg_pa_tone(c->def, "withered", 1.0);
    LpRgb dry_dark = qlcg_pa_tone(c->def, "witheredDark", 1.0);
    LpRgb stem = qlcg_pa_tone(c->def, "witheredStem", 1.0);
    double origin[3] = { 0.0, 0.0, 0.0 };
    qlcg_lp_prism(b, origin, sp * 0.26, sp * 0.26, 0.0, h * 0.05, 5, stem, NULL);
    int stems = c->p->stems > 0 ? c->p->stems : 5;
    if (stems > 7) stems = 7;
    for (int s = 0; s < stems; s++) {
        double angle = (double)s * GOLDEN + 0.15;
        double base[3] = { cos(angle) * sp * 0.15, 0.0, sin(angle) * sp * 0.15 };
        double len = h * (0.30 + 0.35 * qlcg_lp_rng_next(c->rng));
        double tip[3] = { base[0] + cos(angle) * len * 0.30,
                          base[1] + len * 0.86, base[2] + sin(angle) * len * 0.30 };
        segment(b, base, tip, h * 0.013, 0.3, 4, (s % 2) ? stem : dry_dark, LP_MAT_SOLID);
        int twigs = c->p->habit == HERB_BRAMBLE ? 3 : 1;
        for (int t = 0; t < twigs; t++) {
            double ta = angle + ((double)t - 0.5) * 1.4;
            double tl = len * 0.35;
            double tw[3] = { tip[0] + cos(ta) * tl * 0.7, tip[1] + tl * 0.35, tip[2] + sin(ta) * tl * 0.7 };
            segment(b, tip, tw, h * 0.007, 0.2, 3, dry_dark, LP_MAT_SOLID);
            if (t == 0) {
                double dl = len * 0.5;
                blade(b, tw, ta, dl, sp * 0.03, dl * 0.10, dl * 0.75, dry, NO_RIB, 2, LP_MAT_SOLID);
            }
        }
    }
    if (c->p->habit == HERB_ROSETTE) {
        double at[3] = { 0.0, h * 0.10, 0.0 };
        LpOpts o = qlcg_lp_opts();
        o.rings = 0; o.squash = 0.4; o.jitter = 0.2; o.rand = c->rng;
        qlcg_lp_dome(b, at, sp * 0.40, h * 0.16, sp * 0.38, 6, dry_dark, &o);
    }
}

/* 挂点：雪与果 */
static void add_sockets(LpBuilder *b, const HerbCtx *c) {
    const PdDef *def = c->def;
    if (c->s->frozen && qlcg_pa_wants_snow_on(def, "ground")) {
        double at[3] = { 0.0, 0.02, 0.0 };
        qlcg_lp_socket(b, at, LP_SOCKET_SNOW_GROUND);
    }
    if (c->s->frozen && (qlcg_pa_wants_snow_on(def, "canopy") ||
                         qlcg_pa_wants_snow_on(def, "top"))) {
        double at[3] = { 0.0, c->h * 0.82, 0.0 };
        qlcg_lp_socket(b, at, LP_SOCKET_SNOW_CANOPY);
    }
    if (!def->fruit) return;
    int requested = qlcg_pa_js_round(qlcg_pa_pnum(def, "fruitCount", 0));
    int count = qlcg_pa_fruit_count_for(def, requested, 3);
    double size = c->p->fruit_size > 0 ? c->p->fruit_size : c->h * 0.05;
    if (!c->fruit_sites) {
        double ctr[3] = { 0.0, c->h * 0.72, 0.0 };
        qlcg_pa_fruit_layout(b, def, ctr, c->sp * 0.55, count, c->rng, size, 1, NULL);
        if (c->s->fruit) {
            LpRng layout = *c->rng;
            qlcg_pa_fruit_layout(b, def, ctr, c->sp * 0.55, count, &layout, size, 0, draw_fruit);
        }
        return;
    }
    int available = c->fruit_sites->count;
    if (count > available) count = available;
    double explicit_size = qlcg_pa_pnum(def, "fruitRadius", 0.0);
    double radius = explicit_size > 0.0 ? qlcg_pa_clampd(explicit_size, 0.02, 0.5)
                                      : qlcg_pa_clampd(size, 0.035, 0.24);
    for (int i = 0; i < count; i++) {
        int site = (2 * i + 1) * available / (2 * count);
        const double *at = c->fruit_sites->points[site];
        qlcg_lp_socket(b, at, LP_SOCKET_FRUIT);
        if (c->s->fruit) draw_fruit(b, def, at, radius, c->rng);
    }
}

/* 草本模型统一入口 */
void qlcg_pa_build_herb(LpBuilder *b, const PdDef *def, int season, int form,
                        const HerbProfile *p) {
    HerbSeason s = season_of(def, season, p);
    HerbTone t;
    tones_of(def, &s, &t);
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    HerbFruitSites fruit_sites = {0};
    double species_h = qlcg_pa_pnum(def, "height", 0.6);
    double species_sp = qlcg_pa_pnum(def, "spread", species_h * 0.45);
    HerbCtx c;
    c.def = def;
    c.p = p;
    c.s = &s;
    c.t = &t;
    c.rng = &rng;
    c.fruit_sites = def->fruit && p->fruit_form != HERB_HEAD_NONE ? &fruit_sites : NULL;
    c.season = season;
    c.h = fmax(0.08, species_h) * s.height;
    c.sp = fmax(0.05, species_sp) * s.spread;
    c.h *= p->height_scale > 0 ? p->height_scale : 1.0;
    c.sp *= p->spread_scale > 0 ? p->spread_scale : 1.0;
    if (def->state == PD_STATE_SUBADULT) {
        c.h *= 0.74;
        c.sp *= 0.78;
    }
    if (p->habit == HERB_CUSHION) {
        build_cushion(b, &c, form);
        return;
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
    int grass_like = p->habit == HERB_TUSSOCK || p->habit == HERB_STOLON ||
                     p->habit == HERB_FERN || p->habit == HERB_FORB ||
                     p->habit == HERB_BRAMBLE || p->habit == HERB_SUAEDA ||
                     p->habit == HERB_SALICORNIA;
    if (s.dormant && grass_like) {
        double spread = fmin(c.sp * 0.28, c.h * 0.40);
        double mound[3] = { 0.0, 0.0, 0.0 };
        LpRgb mud = qlcg_lp_mix(t.dry, t.stem, 0.30);
        LpOpts om = qlcg_lp_opts();
        om.floor_on = 0.0; om.jitter = 0.20; om.rand = &rng;
        qlcg_lp_dome(b, mound, spread, c.h * 0.30, spread * 0.9, 5, mud, &om);
        int stubs = 4 + qlcg_pa_js_round(c.sp * 3.0);
        if (stubs > 7) stubs = 7;
        for (int i = 0; i < stubs; i++) {
            double angle = (double)i * (TAU / (double)stubs) + 0.3;
            double len = c.h * (0.24 + 0.22 * qlcg_lp_rng_next(&rng));
            double base[3] = { cos(angle) * spread * 0.45, c.h * 0.03, sin(angle) * spread * 0.45 };
            double tip[3] = { base[0] + cos(angle) * len * 0.25, base[1] + len, base[2] + sin(angle) * len * 0.25 };
            segment(b, base, tip, c.h * 0.010, 0.25, 3, t.dry, LP_MAT_SOLID);
            fruit_site(&c, tip);
        }
        for (int i = 0; i < 2; i++) {
            double angle = (double)i * 2.5 + 0.8;
            double base[3] = { cos(angle) * spread * 0.6, c.h * 0.03, sin(angle) * spread * 0.6 };
            double len = c.h * 0.28;
            blade(b, base, angle, len, c.p->leaf_width * 1.2,
                  len * 0.15, len * 0.85, t.dry, NO_RIB, 2, LP_MAT_SOLID);
        }
        add_sockets(b, &c);
        return;
    }
    switch (p->habit) {
    case HERB_STOLON: build_stolon(b, &c); break;
    case HERB_FERN: build_fern(b, &c); break;
    case HERB_FORB: build_forb(b, &c); break;
    case HERB_BRAMBLE: build_bramble(b, &c); break;
    case HERB_STRAP: build_strap(b, &c); break;
    case HERB_ROSETTE: build_rosette(b, &c); break;
    case HERB_SUAEDA: build_suaeda(b, &c); break;
    case HERB_SALICORNIA: build_salicornia(b, &c); break;
    case HERB_TUSSOCK:
    default: build_tussock(b, &c); break;
    }
    add_sockets(b, &c);
}
