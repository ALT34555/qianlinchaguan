#include "nong_model.h"
#include "../../plant_forms.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 农作物季节结构 */
typedef struct {
    double height, spread, leaf_len, leaf_w, density, droop, dry;
    int bloom, fruit;
} CropSeason;

/* 农作物配色 */
typedef struct {
    LpRgb leaf, dark, stem, dry, bloom, fruit;
} CropTone;

/* 挂点集合 */
enum { CROP_FRUIT_CAPACITY = 16 };
typedef struct {
    double fruit_pts[CROP_FRUIT_CAPACITY][3];
    int fruit_count;
    double snow_pts[16][3];
    int snow_count;
} CropSockets;

/* 构建上下文 */
typedef struct {
    const PdDef *def;
    const CropProfile *p;
    const CropSeason *s;
    const CropTone *t;
    LpRng *rng;
    CropSockets *sockets;
    double h, sp;
    int season;
} CropCtx;

/* 提取耐性等级 */
static int crop_tolerance(const PdDef *def, const char *prefix, int fallback) {
    char tag[16];
    for (int i = 0; i < 4; i++) {
        snprintf(tag, sizeof(tag), "%s_%d", prefix, i);
        if (qlcg_pa_tag_has(def, tag)) return i;
    }
    return fallback;
}

/* 枯草色混合 */
static LpRgb crop_dry_of(LpRgb c, double amount) {
    LpRgb straw = qlcg_lp_rgb(168.0, 138.0, 78.0);
    double t = qlcg_pa_clampd(amount, 0.0, 1.0) * 0.75;
    return qlcg_lp_deepen(qlcg_lp_mix(c, straw, t), 0.15 * amount);
}

/* 计算季节形变参数 */
static CropSeason crop_season_of(const PdDef *def, int season, const CropProfile *p) {
    int cold_level = crop_tolerance(def, "naihan", p->cold);
    int heat_level = crop_tolerance(def, "naire", p->heat);
    double chill = (4.0 - qlcg_pa_clampd((double)cold_level, 0.0, 4.0)) / 4.0;
    double warmth = (4.0 - qlcg_pa_clampd((double)heat_level, 0.0, 4.0)) / 4.0;
    double win = 1.0 - chill;
    double sum = 1.0 - warmth;
    double keep = qlcg_pa_clampd(p->winter_leaf, 0.02, 0.90);

    CropSeason s;
    memset(&s, 0, sizeof(s));
    s.height = 1.0;
    s.spread = 1.0;
    s.leaf_len = 1.0;
    s.leaf_w = 1.0;
    s.density = 1.0;

    if (season == PD_SEASON_SPRING) {
        s.height = 0.72 + 0.22 * chill + 0.06 * keep;
        s.spread = 0.85 + 0.12 * chill;
        s.leaf_len = 0.75 + 0.20 * chill;
        s.leaf_w = 0.85 + 0.12 * chill;
        s.density = 0.60 + 0.30 * chill + 0.10 * keep;
        s.droop = 0.08;
        s.dry = 0.04;
    } else if (season == PD_SEASON_SUMMER) {
        s.height = 1.02 - 0.10 * sum;
        s.leaf_len = 1.0 - 0.08 * sum;
        s.leaf_w = 1.0 - 0.15 * sum;
        s.density = 1.0 - 0.25 * sum;
        s.droop = 0.12 + 0.40 * sum;
        s.dry = 0.02 + 0.25 * sum;
    } else if (season == PD_SEASON_AUTUMN) {
        s.height = 0.96 - 0.06 * win;
        s.spread = 0.96 + 0.08 * win;
        s.leaf_len = 0.88 - 0.12 * win;
        s.leaf_w = 0.90 - 0.12 * win;
        s.density = 0.76 - 0.24 * win;
        s.droop = 0.20 + 0.30 * win;
        s.dry = 0.32 + 0.45 * win;
    } else {
        double keep_w = keep * (1.0 - 0.35 * win);
        s.height = 0.65 - 0.25 * win;
        s.spread = 0.68 - 0.20 * win;
        s.leaf_len = keep_w * (0.80 - 0.15 * win);
        s.leaf_w = keep_w * (0.75 - 0.10 * win);
        s.density = keep_w * 0.75;
        s.droop = 0.30 + 0.35 * win;
        s.dry = 0.40 + 0.55 * win;
    }

    s.bloom = season == p->bloom_season;
    s.fruit = def->state == PD_STATE_FRUITING;
    if (def->state == PD_STATE_FLOWERING) s.bloom = 1;
    if (s.fruit || def->state == PD_STATE_SUBADULT) s.bloom = 0;
    return s;
}

/* 作物颜色提取 */
static void crop_tones_of(const PdDef *def, const CropSeason *s, CropTone *t) {
    t->leaf = qlcg_pa_tone(def, "foliage", 1.0);
    t->dark = qlcg_pa_tone(def, "foliageDark", 1.0);
    if (pd_str(&def->palette, "barkLight", NULL)) {
        t->stem = qlcg_pa_tone(def, "barkLight", 1.0);
    } else if (pd_str(&def->palette, "sorghum", NULL)) {
        t->stem = qlcg_pa_tone(def, "sorghum", 1.0);
    } else {
        t->stem = qlcg_lp_mix(t->leaf, crop_dry_of(t->leaf, 0.45), 0.55);
        t->stem.wind = LP_WIND_BARK;
    }
    t->dry = crop_dry_of(t->leaf, 0.35 + 0.55 * s->dry);
    t->bloom = qlcg_pa_tone(def, "bloom", 1.0);
    t->fruit = qlcg_pa_tone(def, "fruit", 1.0);
}

/* 记录果实挂点 */
static void crop_record_fruit(CropCtx *c, const double pt[3]) {
    if (c->sockets->fruit_count < CROP_FRUIT_CAPACITY) {
        memcpy(c->sockets->fruit_pts[c->sockets->fruit_count++], pt, sizeof(double) * 3);
    }
}

/* 记录雪花挂点 */
static void crop_record_snow(CropCtx *c, const double pt[3]) {
    if (c->sockets->snow_count < 16) {
        memcpy(c->sockets->snow_pts[c->sockets->snow_count++], pt, sizeof(double) * 3);
    }
}

/* 直立茎段建模 */
static void crop_segment(LpBuilder *b, const double p0[3], const double p1[3],
                         double r, double taper, int sides, LpRgb col, int mat) {
    double d[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    double len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len <= 1e-5) return;
    d[0] /= len; d[1] /= len; d[2] /= len;
    LpOpts o = qlcg_lp_opts();
    o.sides = sides;
    o.tip_ratio = taper;
    o.mat = mat;
    qlcg_lp_limb(b, p0, d, len, r, col, &o);
}

static void crop_stem_seg(LpBuilder *b, const double p0[3], const double p1[3],
                          double r, double taper, int sides, LpRgb col) {
    crop_segment(b, p0, p1, r, taper, sides, col, LP_MAT_SOLID);
}

/* 带旋向和盖面茎段 */
static void crop_seg_rot(LpBuilder *b, const double p0[3], const double p1[3],
                         double r, double taper, int sides, double rot,
                         int cap, LpRgb col, int mat) {
    double d[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    double len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len <= 1e-5) return;
    d[0] /= len; d[1] /= len; d[2] /= len;
    double ref[3] = { 0.0, 1.0, 0.0 };
    if (fabs(d[1]) > 0.99) {
        ref[0] = 1.0; ref[1] = 0.0; ref[2] = 0.0;
    }
    double tx = ref[1] * d[2] - ref[2] * d[1];
    double ty = ref[2] * d[0] - ref[0] * d[2];
    double tz = ref[0] * d[1] - ref[1] * d[0];
    double tl = sqrt(tx * tx + ty * ty + tz * tz);
    tx /= tl; ty /= tl; tz /= tl;
    double bx = d[1] * tz - d[2] * ty;
    double by = d[2] * tx - d[0] * tz;
    double bz = d[0] * ty - d[1] * tx;
    double r1 = r * taper;
    double bottom_anchor[3], top_anchor[3];
    for (int i = 0; i < sides; i++) {
        double a0 = rot + TAU * (double)i / (double)sides;
        double a1 = rot + TAU * (double)(i + 1) / (double)sides;
        double ca0 = cos(a0), sa0 = sin(a0);
        double ca1 = cos(a1), sa1 = sin(a1);
        double q0[3] = {
            p0[0] + (tx * ca0 + bx * sa0) * r,
            p0[1] + (ty * ca0 + by * sa0) * r,
            p0[2] + (tz * ca0 + bz * sa0) * r
        };
        double q1[3] = {
            p0[0] + (tx * ca1 + bx * sa1) * r,
            p0[1] + (ty * ca1 + by * sa1) * r,
            p0[2] + (tz * ca1 + bz * sa1) * r
        };
        double q2[3] = {
            p1[0] + (tx * ca1 + bx * sa1) * r1,
            p1[1] + (ty * ca1 + by * sa1) * r1,
            p1[2] + (tz * ca1 + bz * sa1) * r1
        };
        double q3[3] = {
            p1[0] + (tx * ca0 + bx * sa0) * r1,
            p1[1] + (ty * ca0 + by * sa0) * r1,
            p1[2] + (tz * ca0 + bz * sa0) * r1
        };
        qlcg_lp_quad(b, q0, q1, q2, q3, col, mat);
        if (i == 0) {
            memcpy(bottom_anchor, q0, sizeof(q0));
            memcpy(top_anchor, q3, sizeof(q3));
        } else if (i + 1 < sides) {
            if (cap && r > 1e-5 && p0[1] > 1e-4)
                qlcg_lp_triangle(b, bottom_anchor, q1, q0, col, mat);
            if (cap && r1 > 1e-5)
                qlcg_lp_triangle(b, top_anchor, q3, q2, col, mat);
        }
    }
}

/* 带弧度叶片 */
static void crop_blade(LpBuilder *b, const double base[3], double dir, double len,
                       double w, double rise, double fall, LpRgb col) {
    double cos_a = cos(dir), sin_a = sin(dir);
    double mid[3] = {
        base[0] + cos_a * len * 0.45,
        base[1] + rise,
        base[2] + sin_a * len * 0.45
    };
    double tip[3] = {
        base[0] + cos_a * len,
        base[1] + rise - fall,
        base[2] + sin_a * len
    };
    double left[3] = { mid[0] - sin_a * w * 0.5, mid[1], mid[2] + cos_a * w * 0.5 };
    double right[3] = { mid[0] + sin_a * w * 0.5, mid[1], mid[2] - cos_a * w * 0.5 };
    qlcg_lp_triangle(b, base, left, mid, col, LP_MAT_SOLID);
    qlcg_lp_triangle(b, base, mid, right, col, LP_MAT_SOLID);
    qlcg_lp_triangle(b, mid, left, tip, col, LP_MAT_SOLID);
    qlcg_lp_triangle(b, mid, right, tip, col, LP_MAT_SOLID);
}

/* 卵形带齿叶片 */
static void crop_ovate_leaf(LpBuilder *b, const double base[3], double dir, double len,
                            double w, double elevation, LpRgb col, LpRgb underside) {
    double seed = base[0] * 13.7 + base[1] * 17.3 + base[2] * 11.9 + dir * 2.71;
    dir += 0.14 * sin(seed);
    elevation = fmax(0.05, elevation + 0.14 * cos(seed * 1.37) + 0.08 * sin(seed * 2.93));
    double cos_a = cos(dir), sin_a = sin(dir);
    double up = sin(elevation);
    double fwd = cos(elevation);
    double tip[3] = {
        base[0] + cos_a * len * fwd,
        base[1] + len * up,
        base[2] + sin_a * len * fwd
    };
    double mid[3] = {
        base[0] + cos_a * len * 0.5 * fwd,
        base[1] + len * 0.55 * up,
        base[2] + sin_a * len * 0.5 * fwd
    };
    double rs = sin(seed * 1.73);
    double roll = w * ((rs >= 0.0 ? 0.10 : -0.10) + 0.12 * rs);
    double l[3] = { mid[0] - sin_a * w * 0.5, mid[1] + roll, mid[2] + cos_a * w * 0.5 };
    double r[3] = { mid[0] + sin_a * w * 0.5, mid[1] - roll, mid[2] - cos_a * w * 0.5 };
    double lift = fmax(w * 0.055, 0.003);
    double tm[3] = { mid[0], mid[1] + lift, mid[2] };
    double bm[3] = { mid[0], mid[1] - lift, mid[2] };
    qlcg_lp_triangle(b, base, l, tm, col, LP_MAT_SOLID);
    qlcg_lp_triangle(b, l, tip, tm, col, LP_MAT_SOLID);
    qlcg_lp_triangle(b, base, tm, r, col, LP_MAT_SOLID);
    qlcg_lp_triangle(b, tm, tip, r, col, LP_MAT_SOLID);
    qlcg_lp_triangle(b, base, bm, l, underside, LP_MAT_SOLID);
    qlcg_lp_triangle(b, l, bm, tip, underside, LP_MAT_SOLID);
    qlcg_lp_triangle(b, base, r, bm, underside, LP_MAT_SOLID);
    qlcg_lp_triangle(b, bm, r, tip, underside, LP_MAT_SOLID);
}

/* 掌状多裂小叶片 */
static void crop_lobed_leaf(LpBuilder *b, const double base[3], double dir, double len,
                            double w, double lift, LpRgb col) {
    double offsets[3] = { -0.38, 0.0, 0.38 };
    double lens[3] = { len * 0.82, len, len * 0.82 };
    for (int i = 0; i < 3; i++) {
        double d = dir + offsets[i];
        crop_blade(b, base, d, lens[i], w * 0.35, lift, lift * 0.25, col);
    }
}

/* 四瓣十字花冠 */
static void crop_four_petal_flower(LpBuilder *b, const double pos[3], double sz,
                                   LpRgb col, LpRng *rng) {
    double base_a = qlcg_lp_rng_next(rng) * TAU;
    for (int i = 0; i < 4; i++) {
        double a = base_a + (double)i * (TAU / 4.0);
        LpOpts o = qlcg_lp_opts();
        o.pitch = 0.15;
        o.cup = 0.35;
        o.rand = rng;
        qlcg_lp_petal(b, pos, a, sz, sz * 0.52, col, &o);
    }
    double center[3] = { pos[0], pos[1] + sz * 0.08, pos[2] };
    double c_sz[3] = { sz * 0.25, sz * 0.25, sz * 0.25 };
    LpOpts oc = qlcg_lp_opts();
    oc.mat = LP_MAT_SOLID;
    qlcg_lp_ico(b, center, c_sz, 4, qlcg_lp_rgb(235.0, 180.0, 50.0), &oc);
}

/* 五瓣锦葵状大花 */
static void crop_mallow_flower(LpBuilder *b, const double pos[3], double sz,
                               LpRgb col, LpRng *rng) {
    double base_a = qlcg_lp_rng_next(rng) * TAU;
    for (int i = 0; i < 5; i++) {
        double a = base_a + (double)i * (TAU / 5.0);
        LpOpts o = qlcg_lp_opts();
        o.pitch = 0.35;
        o.cup = 0.45;
        o.rand = rng;
        qlcg_lp_petal(b, pos, a, sz, sz * 0.68, col, &o);
    }
    double calyx[3] = { pos[0], pos[1] - sz * 0.25, pos[2] };
    double calyx_sz[3] = { sz * 0.45, sz * 0.25, sz * 0.45 };
    LpOpts oc = qlcg_lp_opts();
    oc.mat = LP_MAT_SOLID;
    qlcg_lp_ico(b, calyx, calyx_sz, 5, qlcg_lp_rgb(70.0, 105.0, 45.0), &oc);
}

/* 吐絮棉铃 */
static void crop_cotton_boll(LpBuilder *b, const double pos[3], double sz,
                             LpRgb boll_col, LpRng *rng) {
    double calyx_sz[3] = { sz * 0.6, sz * 0.4, sz * 0.6 };
    LpOpts ob = qlcg_lp_opts();
    ob.mat = LP_MAT_SOLID;
    qlcg_lp_ico(b, pos, calyx_sz, 5, qlcg_lp_rgb(78.0, 56.0, 38.0), &ob);
    for (int i = 0; i < 4; i++) {
        double a = (double)i * (TAU / 4.0) + 0.3;
        double r = sz * 0.32;
        double puff[3] = { pos[0] + cos(a) * r, pos[1] + sz * 0.22, pos[2] + sin(a) * r };
        double puff_sz[3] = { sz * 0.42, sz * 0.42, sz * 0.42 };
        LpOpts op = qlcg_lp_opts();
        op.mat = LP_MAT_FRUIT;
        op.jitter = 0.12;
        op.rand = rng;
        qlcg_lp_ico(b, puff, puff_sz, 5, boll_col, &op);
    }
}

static void crop_body_rot(LpBuilder *b, const double at[3], double x, double y, double z,
                          double rot_y, LpRgb color, int material) {
    double size[3] = { x, y, z };
    LpOpts opts = qlcg_lp_opts();
    opts.rings = 0;
    opts.rot_y = rot_y;
    opts.mat = material;
    qlcg_lp_ico(b, at, size, 5, color, &opts);
}

static void crop_body(LpBuilder *b, const double at[3], double x, double y, double z,
                      LpRgb color, int material) {
    crop_body_rot(b, at, x, y, z, 0.0, color, material);
}

static void crop_fruit_mesh(LpBuilder *b, CropCtx *c, const double at[3], double a) {
    double r = c->p->fruit_size;
    LpRgb col = c->t->fruit;
    double x = cos(a), z = sin(a);
    switch (c->p->produce) {
    case CROP_BAST:
    case CROP_BARK:
        for (int i = -1; i <= 1; i++) {
            double rib[3] = {at[0] - z * r * .20 * i, at[1], at[2] + x * r * .20 * i};
            LpOpts opts = qlcg_lp_opts();
            opts.mat = LP_MAT_FRUIT;
            opts.pitch = 1.25;
            opts.both = 1;
            qlcg_lp_petal(b, rib, a, r * (3.2 - .45 * abs(i)), r * .40,
                          i == 0 ? col : qlcg_lp_deepen(col, .18), &opts);
        }
        break;
    case CROP_POD: {
        double prev[3] = {at[0], at[1], at[2]};
        for (int i = 0; i < 3; i++) {
            double node[3] = {at[0] + x * r * (.28 + i * .24), at[1] - r * (.30 + i * .52),
                              at[2] + z * r * (.28 + i * .24)};
            crop_segment(b, prev, node, r * (i == 0 ? .13 : .25), .92, 5, col, LP_MAT_FRUIT);
            crop_body_rot(b, node, r * .39, r * .52, r * .39, a + i * .4,
                          i % 2 ? qlcg_lp_lighten(col, .15) : col, LP_MAT_FRUIT);
            memcpy(prev, node, sizeof(prev));
        }
        break;
    }
    case CROP_SESAME:
    case CROP_CAPSULE: {
        int is_cap = c->p->produce == CROP_CAPSULE;
        if (is_cap) {
            double st[3] = {at[0], at[1] - r * .22, at[2]};
            double tp[3] = {at[0], at[1] + r * .45, at[2]};
            crop_seg_rot(b, st, tp, fmax(r * .18, .005), .75, 5, a, 1,
                         c->t->stem, LP_MAT_SOLID);
        }
        double rk = is_cap ? .25 : .31;
        double yoff = is_cap ? .42 : .58;
        for (int i = 0; i < 4; i++) {
            double bearing = a + TAU * i / 4;
            double lobe[3] = {at[0] + cos(bearing) * r * rk, at[1] + r * yoff,
                              at[2] + sin(bearing) * r * rk};
            double height = is_cap ? r * .77 : r * 1.75;
            crop_body_rot(b, lobe, r * .46, height, r * .46, bearing + .15 * i,
                          i % 2 ? col : qlcg_lp_lighten(col, .12), LP_MAT_FRUIT);
        }
        break;
    }
    case CROP_SILIQUE: {
        double top[3] = {at[0], at[1] + r * 2.8, at[2]};
        crop_stem_seg(b, at, top, r * .10, .36, 3, c->t->stem);
        for (int i = 0; i < 4; i++) {
            double theta = a + i * GOLDEN;
            double pod[3] = {at[0] + cos(theta) * r * .55, at[1] + r * (.40 + i * .72),
                             at[2] + sin(theta) * r * .55};
            crop_body_rot(b, pod, r * .23, r * 1.17, r * .23, theta, col, LP_MAT_FRUIT);
            double node[3] = {at[0], pod[1], at[2]};
            crop_stem_seg(b, node, pod, r * .064, .53, 3, c->t->stem);
        }
        break;
    }
    case CROP_ALLIUM_HEAD:
    case CROP_PERILLA_HEAD:
    case CROP_WATER_FRUIT:
    case CROP_MULBERRY: {
        int upright = c->p->produce == CROP_PERILLA_HEAD;
        int pendent = c->p->produce == CROP_MULBERRY;
        int count = upright ? 11 : 7;
        double start[3] = {at[0], at[1] + (pendent ? r * .12 : -r * .15), at[2]};
        double axis[3] = {at[0], at[1] + r * (upright ? 2.48 : pendent ? -1.45 : .88), at[2]};
        crop_seg_rot(b, start, axis, fmax(r * .14, c->p->stem_r * .32), .45, 5, a, 1,
                     c->t->stem, LP_MAT_SOLID);
        for (int i = 0; i < count; i++) {
            double theta = a + i * GOLDEN;
            double reach = r * (upright || pendent ? .40 : .62);
            double height = r * (upright ? .18 + i * .22 : pendent ? -.34 - i * .17 : .20 + (i % 3) * .26);
            double lobe[3] = {at[0] + cos(theta) * reach, at[1] + height, at[2] + sin(theta) * reach};
            double node[3] = {at[0], lobe[1], at[2]};
            crop_seg_rot(b, node, lobe, r * .075, .45, 4, theta, 1, c->t->stem, LP_MAT_SOLID);
            crop_body_rot(b, lobe, r * .35, r * .43, r * .35, theta,
                          i % 2 ? col : qlcg_lp_lighten(col, .12), LP_MAT_FRUIT);
        }
        break;
    }
    case CROP_CARROT:
    case CROP_RADISH: {
        int carrot = c->p->produce == CROP_CARROT;
        double depth = r * (carrot ? 5.4 : 4.2);
        double crown[3] = {at[0], at[1] - r * .01, at[2]};
        crop_body_rot(b, crown, r * .75, r * .30, r * .75, a, col, LP_MAT_FRUIT);
        double top[3] = {at[0], at[1], at[2]};
        LpOpts opts = qlcg_lp_opts();
        opts.mat = LP_MAT_FRUIT;
        opts.sides = 7;
        for (int i = 0; i < 3; i++) {
            double widths[] = {.65, 1, .64, .05};
            double ratios[] = {.10, .47, .43};
            double down[3] = {0, -1, 0};
            double len = depth * ratios[i];
            opts.tip_ratio = widths[i + 1] / widths[i];
            qlcg_lp_limb(b, top, down, len, r * widths[i], col, &opts);
            top[1] -= len;
        }
        break;
    }
    default:
        break;
    }
}

static void crop_produce_at(LpBuilder *b, CropCtx *c, const double at[3], double angle) {
    crop_record_fruit(c, at);
    if (c->s->fruit) crop_fruit_mesh(b, c, at, angle);
}

static void crop_leaf_of(LpBuilder *b, CropCtx *c, const double at[3], double a,
                         double len, double width, double rise, LpRgb color) {
    switch (c->p->leaf_form) {
    case CROP_LEAF_TRIFOLIATE:
    case CROP_LEAF_PALMATE: {
        int fingers = c->p->leaf_form == CROP_LEAF_TRIFOLIATE ? 1 : 2;
        double leaf_base[3] = {at[0] + cos(a) * len * .29, at[1] + len * .12, at[2] + sin(a) * len * .29};
        crop_stem_seg(b, at, leaf_base, c->p->stem_r * .26, .62, 3, c->t->stem);
        for (int i = -fingers; i <= fingers; i++) {
            double ps = sin(a * 1.9 + (double)i * 1.31);
            crop_ovate_leaf(b, leaf_base, a + .64 * i + (i ? .03 * i : .02) + .06 * ps,
                            len * (.74 - .13 * abs(i)), width * (.57 - .08 * abs(i)),
                            rise + (double)(i + fingers) * .055 - (i % 2 ? .03 : 0.0) + .06 * cos(a * 2.3 + (double)i),
                            color, c->t->dark);
        }
        break;
    }
    case CROP_LEAF_FEATHER: {
        double lift = sin(rise) * len * .53;
        double prev[3] = {at[0], at[1], at[2]};
        for (int i = 1; i <= 4; i++) {
            double t = i * .19;
            double node[3] = {at[0] + cos(a) * len * t, at[1] + lift * (2.0 * t - t * t),
                              at[2] + sin(a) * len * t};
            crop_seg_rot(b, prev, node, fmax(width * .045, .0035), .85,
                         i % 2 ? 4 : 5, a + i * .35, 1, c->t->stem, LP_MAT_SOLID);
            double side = width * (1.2 - t);
            crop_lobed_leaf(b, node, a - .83, side, side * .49, side * .19, color);
            crop_lobed_leaf(b, node, a + .83, side, side * .49, side * .19, c->t->dark);
            memcpy(prev, node, sizeof(prev));
        }
        double tip[3] = {at[0] + cos(a) * len, at[1] + lift, at[2] + sin(a) * len};
        crop_seg_rot(b, prev, tip, fmax(width * .032, .0025), .35, 4, a, 1,
                     c->t->stem, LP_MAT_SOLID);
        crop_blade(b, prev, a, len * .24, width * .22, (lift - (prev[1] - at[1])) * 1.1, 0, color);
        break;
    }
    case CROP_LEAF_LOBED:
        crop_ovate_leaf(b, at, a + 0.06 * sin(at[0] * 17.0 + at[1] * 23.0),
                        len, width, rise + 0.04 * cos(at[2] * 19.0 + a), color, c->t->dark);
        for (int i = -1; i <= 1; i += 2) {
            double s = sin(a * 2.7 + at[1] * 19.0 + (double)i * 1.13);
            crop_ovate_leaf(b, at, a + (.65 + 0.09 * s) * i, len * .61, width * .58,
                            rise * ((i < 0 ? 0.74 : 0.98) + 0.10 * s), color, c->t->dark);
        }
        break;
    case CROP_LEAF_CUP: {
        LpOpts opts = qlcg_lp_opts();
        opts.pitch = rise;
        opts.cup = .52;
        opts.both = 1;
        qlcg_lp_petal(b, at, a, len, width, color, &opts);
        break;
    }
    default:
        crop_ovate_leaf(b, at, a, len, width, rise, color, c->t->dark);
        break;
    }
}

static void crop_grain_head(LpBuilder *b, CropCtx *c, const double at[3], double a) {
    double size = c->p->fruit_size;
    double x = cos(a), z = sin(a);
    LpRgb grain = c->t->fruit;
    if (c->p->produce == CROP_RICE) {
        double knee[3] = {at[0] + x * size * .35, at[1] + size * .66, at[2] + z * size * .35};
        double end[3] = {at[0] + x * size * 1.19, at[1] + size * .22, at[2] + z * size * 1.19};
        crop_stem_seg(b, at, knee, size * .030, .72, 3, grain);
        crop_stem_seg(b, knee, end, size * .022, .53, 3, grain);
        for (int i = 0; i < 5; i++) {
            double t = (i + .4) / 5;
            double side = i % 2 ? -.38 : .38;
            double base[3] = {knee[0] + (end[0] - knee[0]) * t, knee[1] + (end[1] - knee[1]) * t,
                              knee[2] + (end[2] - knee[2]) * t};
            double tip[3] = {base[0] - z * size * side, base[1] - size * .22, base[2] + x * size * side};
            crop_stem_seg(b, base, tip, size * .015, .44, 3, grain);
            for (int j = 1; j <= 2; j++) {
                double u = j * .45;
                double seed[3] = {base[0] + (tip[0] - base[0]) * u, base[1] + (tip[1] - base[1]) * u,
                                  base[2] + (tip[2] - base[2]) * u};
                crop_body(b, seed, size * .074, size * .14, size * .074, grain, LP_MAT_SOLID);
            }
        }
    } else if (c->p->produce == CROP_WHEAT) {
        double sub[3] = {at[0], at[1] - size * .06, at[2]};
        double top[3] = {at[0], at[1] + size * 1.03, at[2]};
        crop_seg_rot(b, sub, top, size * .095, .26, 5, a, 1, grain, LP_MAT_SOLID);
        for (int i = 0; i < 5; i++) {
            for (int j = -1; j <= 1; j += 2) {
                double offset = size * (.14 - .016 * i) * j;
                double row[3] = {at[0] + x * offset, at[1] + size * (.15 + i * .17), at[2] + z * offset};
                crop_body_rot(b, row, size * .124, size * .176, size * .125, a + i * .25 * j,
                              grain, LP_MAT_SOLID);
                double awn[3] = {row[0] + x * offset * .82, row[1] + size * .35, row[2] + z * offset * .82};
                crop_stem_seg(b, row, awn, size * .016, .07, 3, c->t->bloom);
            }
        }
    } else {
        double sub[3] = {at[0], at[1] - size * .05, at[2]};
        double top[3] = {at[0] + x * size * .16, at[1] + size * .98, at[2] + z * size * .16};
        crop_seg_rot(b, sub, top, fmax(size * .06, c->p->stem_r * .45), .45, 5, a, 1, grain, LP_MAT_SOLID);
        double core[3] = {at[0] + x * size * .08, at[1] + size * .48, at[2] + z * size * .08};
        crop_body_rot(b, core, size * .28, size * .60, size * .28, a, grain, LP_MAT_SOLID);
        for (int i = 0; i < 14; i++) {
            double dir = a + i * GOLDEN + (double)(i % 3) * .17;
            double t = (i + 1.0) / 15;
            double reach = size * (.08 + .13 * sin(PI * t));
            double ax[3] = {at[0] + x * size * .16 * t, at[1] + size * t, at[2] + z * size * .16 * t};
            double tuft[3] = {ax[0] + cos(dir) * reach, at[1] + size * t, ax[2] + sin(dir) * reach};
            crop_seg_rot(b, ax, tuft, size * .025, .60, 4, dir, 1, grain, LP_MAT_SOLID);
            crop_body_rot(b, tuft, size * .08, size * (.115 + .022 * (double)(i % 3)),
                          size * .08, dir + (double)i * .21, c->t->bloom, LP_MAT_SOLID);
        }
    }
}

static void build_crop_grain(LpBuilder *b, CropCtx *c, int simple) {
    int stubble = c->def->state == PD_STATE_NORMAL &&
                  (c->season == PD_SEASON_AUTUMN || c->season == PD_SEASON_WINTER);
    int stalks = simple ? (c->p->stems + 1) / 2 : c->p->stems;
    double h = c->h * c->s->height;
    for (int i = 0; i < stalks; i++) {
        double a = i * GOLDEN + qlcg_lp_rng_next(c->rng) * .33;
        double r = c->sp * (.15 + .59 * qlcg_lp_rng_next(c->rng));
        double base[3] = {cos(a) * r, 0, sin(a) * r};
        double height = h * (.79 + .24 * qlcg_lp_rng_next(c->rng));
        if (stubble) height *= c->season == PD_SEASON_AUTUMN ? .21 : .15;
        double lean = stubble ? .02 : .10;
        double tip[3] = {base[0] + cos(a) * c->sp * lean, height, base[2] + sin(a) * c->sp * lean};
        LpRgb straw = qlcg_lp_mix(c->t->dry, c->t->fruit, .63);
        crop_stem_seg(b, base, tip, c->p->stem_r, stubble ? .9 : .44, 4, stubble || c->s->fruit ? straw : c->t->stem);
        if (stubble) {
            crop_blade(b, base, a + .64, c->sp * .39, c->p->leaf_w * .55, height * .21, height * .09, straw);
            if (c->season == PD_SEASON_WINTER && i == 0) crop_record_snow(c, base);
            continue;
        }
        int leaves = simple ? 2 : (c->season == PD_SEASON_SPRING ? 3 : 4);
        for (int j = 0; j < leaves; j++) {
            double t = .21 + .15 * j;
            double node[3] = {base[0] + (tip[0] - base[0]) * t, height * t, base[2] + (tip[2] - base[2]) * t};
            double len = c->p->leaf_len * c->s->leaf_len * (1 - .10 * j);
            crop_blade(b, node, a + j * 2.75, len, c->p->leaf_w, len * .58, len * .17,
                       c->s->fruit ? straw : j % 2 ? c->t->dark : c->t->leaf);
        }
        if (c->s->fruit) {
            qlcg_lp_socket(b, tip, LP_SOCKET_FRUIT_CROP);
            crop_grain_head(b, c, tip, a);
        } else if (c->def->state == PD_STATE_FLOWERING) {
            crop_body(b, tip, c->p->fruit_size * .12, c->p->fruit_size * .34, c->p->fruit_size * .12,
                      c->t->bloom, LP_MAT_BLOOM);
        }
    }
}

/* 甘蔗株型构建 */
static void build_crop_cane(LpBuilder *b, CropCtx *c) {
    int culm_count = c->p->stems;
    if (culm_count < 2) culm_count = 2;
    double h = c->h * c->s->height;
    double sp = c->sp * c->s->spread;
    LpRgb culm_col = c->s->dry > 0.4 ? c->t->dry : c->t->stem;
    LpRgb leaf_col = c->s->dry > 0.4 ? c->t->dry : c->t->leaf;
    if (c->s->fruit) culm_col = qlcg_lp_deepen(culm_col, .16);

    if (c->season == PD_SEASON_WINTER) {
        for (int i = 0; i < culm_count; i++) {
            double angle = (double)i * (TAU / (double)culm_count) + 0.25;
            double r = sp * 0.35;
            double base[3] = { cos(angle) * r, 0.0, sin(angle) * r };
            double stub_h = h * (0.16 + 0.12 * qlcg_lp_rng_next(c->rng));
            double tip[3] = { base[0], stub_h, base[2] };
            crop_stem_seg(b, base, tip, c->p->stem_r * 1.1, 0.9, 4, culm_col);
            crop_record_fruit(c, tip);
            if (i == 0) crop_record_snow(c, tip);
        }
        double ground_pt[3] = { 0.0, 0.02, 0.0 };
        crop_record_snow(c, ground_pt);
        return;
    }

    for (int i = 0; i < culm_count; i++) {
        double angle = (double)i * (TAU / (double)culm_count) + (double)i * 0.15;
        double rad = sp * (0.18 + 0.15 * qlcg_lp_rng_next(c->rng));
        double base[3] = { cos(angle) * rad, 0.0, sin(angle) * rad };
        double cur[3] = { base[0], base[1], base[2] };
        int nodes = 5 + qlcg_pa_js_round(3.0 * c->s->height);
        double node_step = h / (double)nodes;

        for (int n = 0; n < nodes; n++) {
            double next[3] = {
                cur[0] + cos(angle) * node_step * 0.04,
                cur[1] + node_step,
                cur[2] + sin(angle) * node_step * 0.04
            };
            double seg_r = c->p->stem_r * (1.0 - (double)n * 0.05);
            double p0[3] = {
                cur[0] - (next[0] - cur[0]) * (n > 0 ? 0.04 : 0.0),
                cur[1] - (n > 0 ? node_step * 0.04 : 0.0),
                cur[2] - (next[2] - cur[2]) * (n > 0 ? 0.04 : 0.0)
            };
            double rot = angle + (double)n * 0.43;
            crop_seg_rot(b, p0, next, seg_r, 0.95, (i + n) % 2 ? 5 : 6, rot,
                         (n == nodes - 1) ? 1 : 0, culm_col, LP_MAT_SOLID);
            if (c->s->fruit) {
                double r0[3] = {next[0], next[1] - node_step * .05, next[2]};
                double r1[3] = {next[0], next[1] + node_step * .01, next[2]};
                crop_seg_rot(b, r0, r1, seg_r * 1.18, 1.0, (i + n) % 2 ? 6 : 5,
                             rot + 0.52, 1, qlcg_lp_lighten(culm_col, .19), LP_MAT_SOLID);
            }
            int leaf_start = c->s->fruit ? nodes * 2 / 3 : nodes / 2;
            if (n >= leaf_start && c->s->density > 0.2) {
                int blade_pair = 2;
                double lb[3] = {next[0], next[1] - fmax(seg_r * 0.6, node_step * 0.05), next[2]};
                for (int l = 0; l < blade_pair; l++) {
                    double l_dir = angle + (double)l * 2.3 + (double)n * 0.8;
                    double l_len = c->p->leaf_len * c->s->leaf_len * (0.8 + 0.3 * qlcg_lp_rng_next(c->rng));
                    double rise = l_len * (0.28 - c->s->droop * 0.1);
                    double fall = l_len * (0.45 + c->s->droop * 0.4);
                    crop_blade(b, lb, l_dir, l_len, c->p->leaf_w * c->s->leaf_w, rise, fall, leaf_col);
                }
            }
            cur[0] = next[0]; cur[1] = next[1]; cur[2] = next[2];
        }
        crop_record_fruit(c, cur);

        if (c->s->bloom && i == 0) {
            double ts_sub[3] = { cur[0], cur[1] - h * 0.03, cur[2] };
            double tassel_top[3] = { cur[0], cur[1] + h * 0.28, cur[2] };
            crop_seg_rot(b, ts_sub, tassel_top, c->p->stem_r * 0.45, 0.35, 5, angle, 1,
                         c->t->bloom, LP_MAT_SOLID);
            double plume_sz[3] = { sp * 0.25, h * 0.24, sp * 0.25 };
            LpOpts op = qlcg_lp_opts();
            op.mat = LP_MAT_SOLID;
            op.jitter = 0.25;
            op.rand = c->rng;
            qlcg_lp_ico(b, tassel_top, plume_sz, 5, c->t->bloom, &op);
        }
    }
}

/* 油菜株型构建 */
static void build_crop_brassica(LpBuilder *b, CropCtx *c) {
    double h = c->h * c->s->height;
    double sp = c->sp * c->s->spread;
    LpRgb leaf_col = c->s->dry > 0.4 ? c->t->dry : c->t->leaf;

    if (c->season == PD_SEASON_WINTER) {
        int rosette_leaves = 10;
        double center[3] = { 0.0, 0.03, 0.0 };
        for (int i = 0; i < rosette_leaves; i++) {
            double a = (double)i * (TAU / (double)rosette_leaves) + 0.2;
            double len = sp * (0.45 + 0.15 * qlcg_lp_rng_next(c->rng));
            crop_blade(b, center, a, len, len * 0.42, len * 0.12, len * 0.08, leaf_col);
        }
        double mound[3] = { 0.0, 0.0, 0.0 };
        LpOpts od = qlcg_lp_opts();
        od.mat = LP_MAT_SOLID;
        od.floor_on = 0.0;
        qlcg_lp_dome(b, mound, sp * 0.35, h * 0.15, sp * 0.35, 5, leaf_col, &od);
        crop_record_snow(c, center);
        crop_record_fruit(c, center);
        return;
    }

    double base[3] = { 0.0, 0.0, 0.0 };
    double main_tip[3] = { 0.0, h * 0.95, 0.0 };
    crop_seg_rot(b, base, main_tip, c->p->stem_r, 0.45, 5, 0.23, 1, c->t->stem, LP_MAT_SOLID);
    if (!c->s->fruit) crop_record_fruit(c, main_tip);

    int basal = c->season == PD_SEASON_SUMMER ? 9 : c->season == PD_SEASON_AUTUMN ? 5 : 7;
    for (int i = 0; i < basal; i++) {
        double a = (double)i * (TAU / (double)basal) + 0.3;
        double l_len = sp * 0.45 * c->s->leaf_len;
        crop_blade(b, base, a, l_len, l_len * 0.38, l_len * 0.18, l_len * 0.12, leaf_col);
    }

    int branch_count = c->p->branches;
    for (int i = 0; i < branch_count; i++) {
        double frac = 0.45 + (double)i * (0.50 / (double)branch_count);
        double attach[3] = { 0.0, h * frac, 0.0 };
        double b_dir = (double)i * 2.3999;
        double b_len = sp * (0.42 - (double)i * 0.04);
        double b_tip[3] = {
            attach[0] + cos(b_dir) * b_len,
            attach[1] + b_len * 0.85,
            attach[2] + sin(b_dir) * b_len
        };
        crop_seg_rot(b, attach, b_tip, c->p->stem_r * 0.6, 0.4, 5,
                     (double)i * 0.47 + 0.19, 1, c->t->stem, LP_MAT_SOLID);
        if (!c->s->fruit) crop_record_fruit(c, b_tip);

        if (c->s->bloom) {
            double fl_sub[3] = { b_tip[0] - cos(b_dir) * 0.01, b_tip[1] - 0.01, b_tip[2] - sin(b_dir) * 0.01 };
            double fl_top[3] = { b_tip[0] + cos(b_dir) * 0.02, b_tip[1] + 0.115, b_tip[2] + sin(b_dir) * 0.02 };
            crop_seg_rot(b, fl_sub, fl_top, c->p->stem_r * 0.38, 0.40, 4,
                         b_dir + (double)i * 0.37, 1, c->t->stem, LP_MAT_SOLID);
            int flowers = 4;
            for (int f = 0; f < flowers; f++) {
                double t = (double)f / (double)flowers;
                double u = (double)f / 3.0;
                double node[3] = {
                    b_tip[0] + (fl_top[0] - b_tip[0]) * u,
                    b_tip[1] + (double)f * 0.035,
                    b_tip[2] + (fl_top[2] - b_tip[2]) * u
                };
                double fl_pos[3] = {
                    node[0] + cos(b_dir + (double)f) * 0.028,
                    node[1] + 0.01,
                    node[2] + sin(b_dir + (double)f) * 0.028
                };
                crop_seg_rot(b, node, fl_pos, c->p->stem_r * 0.22, 0.65, 3,
                             b_dir + (double)(i * 3 + f) * 0.41, 1, c->t->stem, LP_MAT_SOLID);
                crop_four_petal_flower(b, fl_pos, c->p->bloom_size * (0.8 + 0.4 * t), c->t->bloom, c->rng);
            }
        } else if (c->s->fruit) {
            int pods = 3;
            for (int p_idx = 0; p_idx < pods; p_idx++) {
                double p_base[3] = {
                    attach[0] + (b_tip[0] - attach[0]) * (0.4 + p_idx * 0.25),
                    attach[1] + (b_tip[1] - attach[1]) * (0.4 + p_idx * 0.25),
                    attach[2] + (b_tip[2] - attach[2]) * (0.4 + p_idx * 0.25)
                };
                double pod_tip[3] = {
                    p_base[0] + cos(b_dir + 0.5) * 0.09,
                    p_base[1] + 0.12,
                    p_base[2] + sin(b_dir + 0.5) * 0.09
                };
                crop_segment(b, p_base, pod_tip, .012, .2, 3, c->t->fruit, LP_MAT_FRUIT);
                if (p_idx == 1) crop_record_fruit(c, p_base);
            }
        }
    }
}

/* 苎麻株型构建 */
static void build_crop_fiber_herb(LpBuilder *b, CropCtx *c) {
    int culm_count = c->p->stems;
    double h = c->h * c->s->height;
    double sp = c->sp * c->s->spread;
    LpRgb leaf_col = c->s->dry > 0.4 ? c->t->dry : c->t->leaf;
    LpRgb under_col = qlcg_lp_rgb(215.0, 225.0, 205.0);

    if (c->season == PD_SEASON_WINTER) {
        for (int i = 0; i < culm_count; i++) {
            double angle = (double)i * (TAU / (double)culm_count) + 0.2;
            double r = sp * 0.35;
            double base[3] = { cos(angle) * r, 0.0, sin(angle) * r };
            double stub[3] = { base[0], h * (0.14 + 0.10 * qlcg_lp_rng_next(c->rng)), base[2] };
            crop_seg_rot(b, base, stub, c->p->stem_r * 0.9, 0.6, 5,
                         angle + (double)i * 0.47, 1, c->t->dry, LP_MAT_SOLID);
            crop_record_fruit(c, stub);
            if (i == 0) crop_record_snow(c, stub);
        }
        double ground_pt[3] = { 0.0, 0.02, 0.0 };
        crop_record_snow(c, ground_pt);
        return;
    }

    for (int i = 0; i < culm_count; i++) {
        double angle = (double)i * (TAU / (double)culm_count) + (double)i * 0.18 + 0.27;
        double rad = sp * (0.16 + 0.18 * qlcg_lp_rng_next(c->rng));
        double base[3] = { cos(angle) * rad, 0.0, sin(angle) * rad };
        double tip[3] = {
            base[0] + cos(angle) * sp * 0.22,
            h * (0.85 + 0.25 * qlcg_lp_rng_next(c->rng)),
            base[2] + sin(angle) * sp * 0.22
        };
        crop_seg_rot(b, base, tip, c->p->stem_r, 0.45, 5,
                     angle + (double)i * 0.47 + 0.19, 1, c->t->stem, LP_MAT_SOLID);
        double fiber[3] = {base[0] + (tip[0] - base[0]) * .32 + cos(angle) * c->p->stem_r * .30,
                           tip[1] * .32, base[2] + (tip[2] - base[2]) * .32 + sin(angle) * c->p->stem_r * .30};
        crop_produce_at(b, c, fiber, angle);

        int leaves = 6 + qlcg_pa_js_round(4.0 * c->s->density);
        for (int l = 0; l < leaves; l++) {
            double jph = (double)i * 2.13 + (double)l * 1.47;
            double t = 0.25 + (double)l * (0.72 / (double)leaves) + 0.025 * sin(jph);
            if (t > 0.97) t = 0.97;
            double l_pos[3] = {
                base[0] + (tip[0] - base[0]) * t,
                base[1] + (tip[1] - base[1]) * t,
                base[2] + (tip[2] - base[2]) * t
            };
            double l_dir = angle + (double)l * 2.3999 + (double)i * 0.73;
            double l_len = c->p->leaf_len * c->s->leaf_len * (0.8 + 0.3 * qlcg_lp_rng_next(c->rng));
            double l_w = c->p->leaf_w * c->s->leaf_w;
            if (c->p->leaf_form == CROP_LEAF_PALMATE) {
                crop_leaf_of(b, c, l_pos, l_dir + 0.09 * cos(jph), l_len, l_w,
                             .25 + 0.09 * sin(jph), leaf_col);
            } else {
                crop_ovate_leaf(b, l_pos, l_dir + 0.09 * cos(jph), l_len, l_w,
                                .23 - c->s->droop * 0.2 + 0.10 * sin(jph),
                                leaf_col, under_col);
            }

            if (c->s->bloom && l % 2 == 0) {
                double fl_pt[3] = { l_pos[0], l_pos[1] + 0.03, l_pos[2] };
                double fl_sz[3] = { 0.04, 0.08, 0.04 };
                LpOpts of = qlcg_lp_opts();
                of.mat = LP_MAT_SOLID;
                of.rot_y = angle + (double)l * 0.61 + (double)i * 0.43;
                qlcg_lp_ico(b, fl_pt, fl_sz, 4, c->t->bloom, &of);
            }
        }
    }
}

/* 棉花株型构建 */
static void build_crop_boll_shrub(LpBuilder *b, CropCtx *c) {
    double h = c->h * c->s->height;
    double sp = c->sp * c->s->spread;
    LpRgb leaf_col = c->s->dry > 0.4 ? c->t->dry : c->t->leaf;

    double base[3] = { 0.0, 0.0, 0.0 };
    double trunk_top[3] = { 0.0, h * 0.88, 0.0 };
    crop_stem_seg(b, base, trunk_top, c->p->stem_r * 1.2, 0.45, 5, c->t->stem);
    if (!c->s->fruit) crop_record_fruit(c, trunk_top);

    if (c->season == PD_SEASON_WINTER) {
        int branches = 6;
        for (int i = 0; i < branches; i++) {
            double frac = 0.35 + (double)i * (0.55 / (double)branches);
            double attach[3] = { 0.0, h * frac, 0.0 };
            double b_dir = (double)i * 2.15;
            double reach = sp * 0.48;
            double b_tip[3] = {
                cos(b_dir) * reach,
                attach[1] + reach * 0.35,
                sin(b_dir) * reach
            };
            crop_stem_seg(b, attach, b_tip, c->p->stem_r * 0.55, 0.3, 3, c->t->stem);
            crop_record_fruit(c, b_tip);
            if (i % 2 == 0) crop_record_snow(c, b_tip);
        }
        crop_record_snow(c, trunk_top);
        return;
    }

    int branch_count = c->p->branches;
    for (int i = 0; i < branch_count; i++) {
        double frac = 0.28 + (double)i * (0.65 / (double)branch_count);
        double attach[3] = { 0.0, h * frac, 0.0 };
        double b_dir = (double)i * 2.25;
        double reach = sp * (0.55 - (double)i * 0.03) * c->s->spread;
        double b_tip[3] = {
            cos(b_dir) * reach,
            attach[1] + reach * 0.38,
            sin(b_dir) * reach
        };
        crop_stem_seg(b, attach, b_tip, c->p->stem_r * 0.65, 0.35, 4, c->t->stem);
        if (!c->s->fruit) crop_record_fruit(c, b_tip);

        int leaves = c->season == PD_SEASON_SPRING ? 2 : c->season == PD_SEASON_SUMMER ? 4 : 3;
        for (int l = 0; l < leaves; l++) {
            double t = .31 + .64 * (l + 1.0) / leaves;
            double l_pos[3] = {
                attach[0] + (b_tip[0] - attach[0]) * t,
                attach[1] + (b_tip[1] - attach[1]) * t,
                attach[2] + (b_tip[2] - attach[2]) * t
            };
            double l_len = c->p->leaf_len * c->s->leaf_len;
            double l_w = c->p->leaf_w * c->s->leaf_w;
            crop_lobed_leaf(b, l_pos, b_dir + (double)l * 0.6, l_len, l_w, 0.08, leaf_col);
        }

        if (c->s->bloom) {
            double fl_pos[3] = { b_tip[0], b_tip[1] + 0.05, b_tip[2] };
            crop_mallow_flower(b, fl_pos, c->p->bloom_size, c->t->bloom, c->rng);
        } else if (c->s->fruit) {
            crop_record_fruit(c, b_tip);
            crop_cotton_boll(b, b_tip, c->p->fruit_size, c->t->fruit, c->rng);
        }
    }
}

static void crop_bloom_head(LpBuilder *b, CropCtx *c, const double at[3], double a) {
    double size = c->p->bloom_size;
    if (c->p->produce == CROP_CAPSULE) {
        crop_mallow_flower(b, at, size, c->t->bloom, c->rng);
    } else if (c->p->produce == CROP_ALLIUM_HEAD || c->p->produce == CROP_CARROT) {
        int count = c->p->produce == CROP_CARROT ? 9 : 7;
        double orig[3] = {at[0], at[1] - size * .05, at[2]};
        for (int i = 0; i < count; i++) {
            double dir = a + TAU * i / count;
            double tip[3] = {at[0] + cos(dir) * size * .86, at[1] + size * (.39 + .21 * (i % 3)),
                             at[2] + sin(dir) * size * .86};
            crop_seg_rot(b, orig, tip, fmax(c->p->stem_r * .22, size * .035), .45,
                         i % 2 ? 4 : 5, dir, 1, c->t->stem, LP_MAT_SOLID);
            crop_four_petal_flower(b, tip, size * .27, c->t->bloom, c->rng);
        }
    } else {
        crop_four_petal_flower(b, at, size, c->t->bloom, c->rng);
    }
}

static void build_crop_herb(LpBuilder *b, CropCtx *c) {
    double h = c->h * c->s->height;
    double sp = c->sp * c->s->spread;
    int winter = c->season == PD_SEASON_WINTER;
    int leaves = winter ? 2 : (c->season == PD_SEASON_SPRING ? 4 : c->season == PD_SEASON_SUMMER ? 7 : 5);
    int stems = winter ? (c->p->stems + 1) / 2 : c->p->stems;
    for (int i = 0; i < stems; i++) {
        double a = i * GOLDEN + .28;
        double x = cos(a), z = sin(a);
        double base[3] = {x * sp * .29, 0, z * sp * .29};
        double tip[3] = {base[0] + x * sp * .25, h * (.83 + .06 * (i % 4)), base[2] + z * sp * .25};
        if (winter) tip[1] *= .49;
        crop_stem_seg(b, base, tip, c->p->stem_r, .46, 4, winter ? c->t->dry : c->t->stem);
        for (int j = 0; j < leaves; j++) {
            double jph = (double)i * 2.13 + (double)j * 1.37;
            double t = fmin(0.95, .20 + .69 * (j + 1) / leaves + 0.025 * sin(jph));
            double at[3] = {base[0] + (tip[0] - base[0]) * t, tip[1] * t, base[2] + (tip[2] - base[2]) * t};
            double angle = a + j * GOLDEN + (double)i * 0.37;
            double r_at = c->p->stem_r * (1.0 - .54 * t);
            double len = fmax(c->p->leaf_len * c->s->leaf_len * (1.17 - .42 * t), r_at * 4.5);
            double width = fmax(c->p->leaf_w * c->s->leaf_w, r_at * 1.2);
            LpRgb col = winter ? c->t->dry : j % 2 ? c->t->leaf : c->t->dark;
            double ph = angle * 2.13 + (double)j * 1.37 + at[1] * 11.0;
            crop_leaf_of(b, c, at, angle + 0.08 * cos(ph), len, width,
                         .23 + 0.08 * sin(ph), col);
            if ((c->p->produce == CROP_POD || c->p->produce == CROP_SESAME) && j % 2 == 1) {
                double p_ang = angle + .35;
                double p_reach = fmax(len * .21, r_at * 1.9);
                double p_rise = fmax(len * .075, r_at * .8);
                double pedicel[3] = {at[0] + cos(p_ang) * p_reach, at[1] + p_rise,
                                     at[2] + sin(p_ang) * p_reach};
                crop_seg_rot(b, at, pedicel, c->p->stem_r * .30, .48, 4, p_ang + .5,
                             1, c->t->stem, LP_MAT_SOLID);
                if (c->s->bloom) crop_bloom_head(b, c, pedicel, p_ang);
                crop_produce_at(b, c, pedicel, p_ang);
            }
        }
        if (c->p->produce == CROP_PERILLA_HEAD) {
            if (c->s->bloom) {
                double end[3] = {tip[0], tip[1] + c->p->bloom_size * 2.72, tip[2]};
                crop_stem_seg(b, tip, end, c->p->stem_r * .38, .23, 3, c->t->stem);
                for (int j = 0; j < 5; j++) {
                    double pos[3] = {tip[0], tip[1] + c->p->bloom_size * j * .68, tip[2]};
                    crop_bloom_head(b, c, pos, a + j * GOLDEN);
                }
            }
            crop_produce_at(b, c, tip, a);
        }
        if (winter && i == 0) crop_record_snow(c, base);
    }
}

static void build_crop_allium(LpBuilder *b, CropCtx *c) {
    double h = c->h * c->s->height;
    double sp = c->sp * c->s->spread;
    int winter = c->season == PD_SEASON_WINTER;
    int n = winter ? (c->p->stems + 1) / 2 : c->p->stems;
    for (int i = 0; i < n; i++) {
        double a = i * GOLDEN;
        double dx = cos(a), dz = sin(a);
        double r = sp * (.14 + .49 * (i % 3) / 2);
        double height = h * (.73 + .085 * (i % 4));
        if (c->p->leaf_form == CROP_LEAF_FLAT) height *= .69;
        double base[3] = {dx * r, 0, dz * r};
        double knee[3] = {base[0] + dx * sp * .17, height * .62, base[2] + dz * sp * .17};
        double reach = sp * (c->p->leaf_form == CROP_LEAF_FLAT ? 1.03 : .53);
        double tip[3] = {base[0] + dx * reach, height, base[2] + dz * reach};
        double w = c->p->leaf_w * c->s->leaf_w;
        LpRgb color = i % 2 ? c->t->leaf : c->t->dark;
        if (c->p->leaf_form == CROP_LEAF_TUBE) {
            double sheath[3] = {base[0], height * .21, base[2]};
            double sh_dn[3] = {sheath[0], sheath[1] * .95, sheath[2]};
            double kn_dn[3] = {
                knee[0] - (tip[0] - knee[0]) * .04,
                knee[1] - (tip[1] - knee[1]) * .04,
                knee[2] - (tip[2] - knee[2]) * .04
            };
            crop_seg_rot(b, base, sheath, w * .55, .88, i % 2 ? 5 : 6,
                         a + .21 + (double)i * .41, 0, qlcg_lp_rgb(215, 224, 187), LP_MAT_SOLID);
            crop_seg_rot(b, sh_dn, knee, w * .49, .77, i % 2 ? 6 : 5,
                         a + .68 + (double)i * .53, 0, color, LP_MAT_SOLID);
            crop_seg_rot(b, kn_dn, tip, w * .38, .16, i % 2 ? 5 : 6,
                         a + 1.15 + (double)i * .67, 1, color, LP_MAT_SOLID);
        } else {
            LpOpts opts = qlcg_lp_opts();
            opts.both = 1;
            opts.pitch = atan2(height, reach) + .06 * (double)(((i * 2) % 5) - 2);
            opts.cup = (c->p->leaf_form == CROP_LEAF_KEEL ? .49 : .12)
                       + .05 * (double)(((i * 3) % 7) - 3);
            double aj = .04 * ((double)((i * 5) % 6) - 2.5);
            qlcg_lp_petal(b, base, a + aj, hypot(height, reach), w, color, &opts);
        }
    }
    int scapes = c->p->branches;
    for (int i = 0; i < scapes; i++) {
        double a = i * GOLDEN + .28;
        double base[3] = {cos(a) * sp * .28, 0, sin(a) * sp * .28};
        double tip[3] = {base[0] + cos(a) * sp * .10, h * (1.01 + .10 * i), base[2] + sin(a) * sp * .10};
        if (c->s->bloom || c->s->fruit)
            crop_seg_rot(b, base, tip, c->p->stem_r, .55, 7,
                         a + .57 + (double)i * .83, 1, c->t->stem, LP_MAT_SOLID);
        if (c->s->bloom) crop_bloom_head(b, c, tip, a);
        crop_produce_at(b, c, c->s->bloom || c->s->fruit ? tip : base, a);
    }
}

static void build_crop_rosette(LpBuilder *b, CropCtx *c) {
    double origin[3] = {0, 0, 0};
    int winter = c->season == PD_SEASON_WINTER;
    int n = winter ? (c->p->stems + 1) / 2 : c->p->stems;
    double len = c->p->leaf_len * c->s->leaf_len * c->p->spread_scale * (winter ? 1.35 : 1);
    double w = c->p->leaf_w * c->s->leaf_w * c->p->spread_scale * (winter ? 1.45 : 1);
    double bend = c->p->leaf_form == CROP_LEAF_FEATHER ? .95 : .36;
    for (int i = 0; i < n; i++) {
        double angle = i * GOLDEN;
        double inner = (double)i / n;
        crop_leaf_of(b, c, origin, angle, len * (1.17 - .43 * inner), w * (1.13 - .32 * inner),
                     bend + .50 * inner, i % 2 ? c->t->leaf : c->t->dark);
    }
    if (c->s->bloom || (c->s->fruit && c->p->produce == CROP_SILIQUE)) {
        double tip[3] = {c->sp * .19, c->h * .98, 0};
        crop_stem_seg(b, origin, tip, c->p->stem_r, .48, 4, c->t->stem);
        if (c->s->bloom) {
            crop_bloom_head(b, c, tip, .1);
        } else {
            crop_produce_at(b, c, tip, .1);
            return;
        }
    }
    crop_produce_at(b, c, origin, 0);
    if (winter) crop_record_snow(c, origin);
}

static void build_crop_floating(LpBuilder *b, CropCtx *c) {
    int winter = c->season == PD_SEASON_WINTER;
    int n = winter ? (c->p->stems + 1) / 2 : c->p->stems;
    double h = c->h * c->s->height;
    for (int i = 0; i < n; i++) {
        double a = i * GOLDEN;
        double dx = cos(a), dz = sin(a);
        double reach = c->sp * c->s->spread * (.43 + .12 * (i % 4));
        double base[3] = {dx * reach * .18, 0, dz * reach * .18};
        double mid[3] = {dx * reach * .62, h * .42, dz * reach * .62};
        double tip[3] = {dx * reach, h * (.84 + .022 * i), dz * reach};
        crop_stem_seg(b, base, mid, c->p->stem_r, .82, 4, c->t->stem);
        crop_stem_seg(b, mid, tip, c->p->stem_r * .82, .56, 4, c->t->stem);
        double len = c->p->leaf_len * c->s->leaf_len * (winter ? 1.42 : 1);
        double w = c->p->leaf_w * c->s->leaf_w * (winter ? 1.40 : 1);
        double under[3] = {tip[0], tip[1] - fmax(w * .03, .003), tip[2]};
        for (int j = 0; j < 10; j++) {
            double alpha = TAU * j / 10, beta = TAU * (j + 1) / 10;
            double left[3] = {tip[0] + dx * cos(alpha) * len - dz * sin(alpha) * w,
                              tip[1] + w * .075, tip[2] + dz * cos(alpha) * len + dx * sin(alpha) * w};
            double right[3] = {tip[0] + dx * cos(beta) * len - dz * sin(beta) * w,
                               tip[1] + w * .075, tip[2] + dz * cos(beta) * len + dx * sin(beta) * w};
            qlcg_lp_triangle(b, tip, right, left, j % 2 ? c->t->leaf : c->t->dark, LP_MAT_SOLID);
            qlcg_lp_triangle(b, under, left, right, qlcg_lp_rgb(98, 63, 78), LP_MAT_SOLID);
        }
        if (i % 2 == 0) {
            double at[3] = {tip[0] + dx * len * 1.10, tip[1] + w * .29, tip[2] + dz * len * 1.10};
            if (c->s->bloom || c->s->fruit) crop_stem_seg(b, mid, at, c->p->stem_r * .71, .43, 4, c->t->stem);
            if (c->s->bloom) crop_bloom_head(b, c, at, a);
            crop_produce_at(b, c, at, a);
        }
    }
}

static void build_crop_woody(LpBuilder *b, CropCtx *c) {
    double h = c->h * c->s->height;
    double sp = c->sp * c->s->spread;
    int branches = c->p->branches;
    double origin[3] = {0, 0, 0};
    int shrub = c->p->stems > 1;
    double top[3] = {sp * .12, h * .86, 0};
    if (!shrub) crop_stem_seg(b, origin, top, c->p->stem_r, .29, 6, c->t->stem);
    int leaves = qlcg_pa_js_round(10 * c->s->density);
    if (c->season == PD_SEASON_WINTER) leaves = c->p->winter_leaf < .18 ? 0 : leaves;
    for (int i = 0; i < branches; i++) {
        double a = i * GOLDEN + .29;
        double t = .29 + .58 * (i + 1) / branches;
        double root[3] = {top[0] * t, top[1] * t, 0};
        double reach = sp * (1.00 - .51 * t);
        double shoulder = root[1] + h * .095, crown = root[1] + h * .22;
        if (shrub) {
            root[0] = cos(a) * sp * .07;
            root[1] = 0;
            root[2] = sin(a) * sp * .07;
            shoulder = h * (.36 + .16 * t);
            crown = h * (.60 + .28 * t);
            reach = sp * (.67 + .13 * (i % c->p->stems));
        }
        double mid[3] = {root[0] + cos(a) * reach * .56, shoulder, root[2] + sin(a) * reach * .56};
        double end[3] = {root[0] + cos(a) * reach, crown, root[2] + sin(a) * reach};
        double width = c->p->stem_r * (.55 - .27 * t);
        crop_seg_rot(b, root, mid, width, .72, 5, (double)i * .47 + .19, 1, c->t->stem, LP_MAT_SOLID);
        crop_seg_rot(b, mid, end, width * .72, .25, 5, (double)i * .47 + .61, 1, c->t->stem, LP_MAT_SOLID);
        for (int j = 0; j < leaves; j++) {
            double ph = (double)i * 2.13 + (double)j * 1.37;
            double f = fmin(0.96, .20 + .72 * (j + 1.0) / leaves + 0.03 * sin(ph));
            double at[3] = {mid[0] + (end[0] - mid[0]) * f, mid[1] + (end[1] - mid[1]) * f,
                            mid[2] + (end[2] - mid[2]) * f};
            double dir = a + (j % 2 ? -.88 : .88) + 0.16 * cos(ph);
            double len = c->p->leaf_len * c->s->leaf_len * (1.27 - .52 * f);
            crop_leaf_of(b, c, at, dir, len, c->p->leaf_w * c->s->leaf_w,
                         .24 + 0.14 * sin(ph), j % 2 ? c->t->leaf : c->t->dark);
        }
        if (c->p->produce == CROP_BARK) {
            double tf = t * .93;
            double tr = c->p->stem_r * (1.0 - .71 * tf);
            double at[3] = {
                shrub ? root[0] + cos(a) * reach * .28 : top[0] * tf + cos(a) * tr * .45,
                shrub ? root[1] + (shoulder - root[1]) * .45 : top[1] * tf,
                shrub ? root[2] + sin(a) * reach * .28 : top[2] * tf + sin(a) * tr * .45
            };
            crop_produce_at(b, c, at, a);
        } else {
            if (c->s->bloom) crop_bloom_head(b, c, end, a);
            crop_produce_at(b, c, end, a);
        }
        if (i % 2 == 0) crop_record_snow(c, end);
    }
}

/* 幼苗状态构建 */
static void build_crop_seedling(LpBuilder *b, CropCtx *c) {
    double base[3] = { 0.0, 0.0, 0.0 };
    double h = c->h * 0.28;
    double tip[3] = { 0.0, h, 0.0 };
    crop_stem_seg(b, base, tip, c->p->stem_r * 0.6, 0.5, 4, c->t->stem);
    int leaves = 4;
    for (int i = 0; i < leaves; i++) {
        double a = (double)i * (TAU / (double)leaves);
        double len = c->sp * 0.35;
        crop_blade(b, tip, a, len, len * 0.38, len * 0.25, len * 0.15, c->t->leaf);
    }
}

/* 枯萎状态构建 */
static void build_crop_withered(LpBuilder *b, CropCtx *c) {
    double h = c->h * 0.55;
    double sp = c->sp * 0.55;
    int stalks = c->p->stems > 1 ? c->p->stems : 3;
    for (int i = 0; i < stalks; i++) {
        double a = (double)i * (TAU / (double)stalks);
        double rad = sp * (.32 + .06 * (double)(i % 3));
        double base[3] = { cos(a) * rad, 0.0, sin(a) * rad };
        double tip[3] = {
            base[0] + cos(a + 0.6) * sp * 0.35,
            h * (0.35 + 0.35 * qlcg_lp_rng_next(c->rng)),
            base[2] + sin(a + 0.6) * sp * 0.35
        };
        crop_seg_rot(b, base, tip, c->p->stem_r * 0.8, 0.3, 5,
                     (double)i * 0.67 + 0.31, 1, c->t->dry, LP_MAT_SOLID);
    }
}

/* 添加积雪与果实挂点 */
static void crop_add_sockets(LpBuilder *b, CropCtx *c) {
    for (int i = 0; i < c->sockets->fruit_count; i++) {
        qlcg_lp_socket(b, c->sockets->fruit_pts[i],
                      c->p->fiber ? LP_SOCKET_FRUIT_FIBER : LP_SOCKET_FRUIT);
    }
    if (c->season == PD_SEASON_WINTER) {
        for (int i = 0; i < c->sockets->snow_count && i < 4; i++) {
            int kind = (c->sockets->snow_pts[i][1] > c->h * 0.25)
                       ? LP_SOCKET_SNOW_CANOPY : LP_SOCKET_SNOW_GROUND;
            qlcg_lp_socket(b, c->sockets->snow_pts[i], kind);
        }
    }
}

/* 农作物主构建入口 */
void qlcg_pa_build_crop(LpBuilder *b, const PdDef *def, int season, int form,
                        const CropProfile *p) {
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    CropSeason s = crop_season_of(def, season, p);
    CropTone t;
    crop_tones_of(def, &s, &t);
    CropSockets skts;
    memset(&skts, 0, sizeof(skts));

    double base_h = qlcg_pa_pnum(def, "height", 1.5);
    double base_sp = qlcg_pa_pnum(def, "spread", base_h * 0.4);
    if (p->height_scale > 0) base_h *= p->height_scale;
    if (p->spread_scale > 0) base_sp *= p->spread_scale;

    CropCtx c = {
        .def = def, .p = p, .s = &s, .t = &t,
        .rng = &rng, .sockets = &skts,
        .h = base_h, .sp = base_sp, .season = season
    };

    if (form == VEG_FORM_SIMPLE && p->habit == CROP_GRAIN) {
        build_crop_grain(b, &c, 1);
        crop_add_sockets(b, &c);
        return;
    }
    if (form == VEG_FORM_SIMPLE) {
        double o[3] = { 0.0, 0.0, 0.0 };
        qlcg_lp_cross_quad(b, o, c.sp * 2.0, c.h, t.leaf, NULL);
        return;
    }
    if (def->state == PD_STATE_WITHERED) {
        build_crop_withered(b, &c);
        crop_add_sockets(b, &c);
        return;
    }
    if (def->state == PD_STATE_SEEDLING) {
        build_crop_seedling(b, &c);
        crop_add_sockets(b, &c);
        return;
    }
    if (def->state == PD_STATE_SUBADULT) {
        c.h *= .78;
        c.sp *= .75;
    }

    switch (p->habit) {
    case CROP_CANE:
        build_crop_cane(b, &c);
        break;
    case CROP_BRASSICA:
        build_crop_brassica(b, &c);
        break;
    case CROP_FIBER_HERB:
        build_crop_fiber_herb(b, &c);
        break;
    case CROP_BOLL_SHRUB:
        build_crop_boll_shrub(b, &c);
        break;
    case CROP_GRAIN:
        build_crop_grain(b, &c, 0);
        break;
    case CROP_STEM_HERB:
        build_crop_herb(b, &c);
        break;
    case CROP_ALLIUM:
        build_crop_allium(b, &c);
        break;
    case CROP_ROSETTE:
        build_crop_rosette(b, &c);
        break;
    case CROP_FLOATING:
        build_crop_floating(b, &c);
        break;
    case CROP_WOODY:
        build_crop_woody(b, &c);
        break;
    }
    crop_add_sockets(b, &c);
}
