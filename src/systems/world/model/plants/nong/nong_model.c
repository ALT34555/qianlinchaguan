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
    } else if (season == PD_SEASON_WINTER) {
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
    s.fruit = def->state == PD_STATE_FRUITING ||
              (p->produce == CROP_COIX && def->state == PD_STATE_NORMAL && season == PD_SEASON_AUTUMN);
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

static void cereal_point(double out[3], const double base[3], const double dir[2],
                         double reach, double rise, double side) {
    out[0] = base[0] + dir[0] * reach - dir[1] * side;
    out[1] = base[1] + rise;
    out[2] = base[2] + dir[1] * reach + dir[0] * side;
}

/* 秆叶在同一节点受风 */
static void cereal_stem(LpBuilder *b, const double nodes[4][3], const double dir[2],
                        double radius, LpRgb col) {
    const double radii[] = {1.0, .83, .67, .43};
    for (int i = 0; i < 2; i++) {
        double cross[2] = {i ? -dir[1] : dir[0], i ? dir[0] : dir[1]};
        double left[4][3], right[4][3];
        for (int j = 0; j < 4; j++) {
            cereal_point(left[j], nodes[j], cross, 0, 0, -radius * radii[j]);
            cereal_point(right[j], nodes[j], cross, 0, 0, radius * radii[j]);
            if (j) qlcg_lp_quad(b, left[j - 1], right[j - 1], right[j], left[j], col, LP_MAT_SOLID);
        }
    }
}

/* 折脊剑叶 */
static void cereal_blade(LpBuilder *b, const double root[3], double angle, double len,
                         double width, double lift, double tip_lift, LpRgb col, int rows) {
    double dir[2] = {cos(angle), sin(angle)};
    double prev[3][3], next[3][3], tip[3];
    LpRgb lit = qlcg_lp_lighten(col, .14);
    LpRgb shade = qlcg_lp_deepen(col, .12);
    const double reach_three[] = {.19, .45, .73};
    const double lift_three[] = {.43, .85, 1.0};
    const double sweep_three[] = {.008, .024, .065};
    const double widths_three[] = {.36, .50, .29};
    for (int i = 0; i < rows; i++) {
        double reach = len * (rows == 3 ? reach_three[i] : i ? .66 : .34);
        double rise = lift * (rows == 3 ? lift_three[i] : i ? 1.0 : .68);
        double sweep = len * (rows == 3 ? sweep_three[i] : i ? .054 : .013);
        double half = width * (rows == 3 ? widths_three[i] : i ? .33 : .50);
        cereal_point(next[0], root, dir, reach, rise - half * .46, sweep - half);
        cereal_point(next[1], root, dir, reach, rise + half * .25, sweep);
        cereal_point(next[2], root, dir, reach, rise + half * .85, sweep + half);
        if (i == 0) {
            qlcg_lp_triangle(b, root, next[1], next[0], lit, LP_MAT_SOLID);
            qlcg_lp_triangle(b, root, next[2], next[1], shade, LP_MAT_SOLID);
        } else {
            qlcg_lp_quad(b, prev[0], prev[1], next[1], next[0], lit, LP_MAT_SOLID);
            qlcg_lp_quad(b, prev[1], prev[2], next[2], next[1], shade, LP_MAT_SOLID);
        }
        memcpy(prev, next, sizeof(prev));
    }
    cereal_point(tip, root, dir, len, tip_lift, len * .12);
    qlcg_lp_triangle(b, prev[0], prev[1], tip, lit, LP_MAT_SOLID);
    qlcg_lp_triangle(b, prev[1], prev[2], tip, shade, LP_MAT_SOLID);
}

/* 稻穗的弧形穗轴与垂粒 */
static void rice_panicle(LpBuilder *b, const double at[3], double angle, double size,
                         LpRgb grain, int ripe, int simple) {
    double dir[2] = {cos(angle), sin(angle)};
    const double reaches[] = {0, .22, .49, .76, .96};
    const double rises[] = {0, .22, ripe ? .27 : .44, ripe ? .13 : .51, ripe ? -.19 : .48};
    const double branch_nodes[] = {1.08, 1.55, 2.10, 2.53, 3.08, 3.59};
    LpRgb rachis = qlcg_lp_deepen(grain, .10);
    double axis[5][3], edge[4][3];
    for (int i = 0; i < 4; i++) {
        cereal_point(axis[i], at, dir, size * reaches[i], size * rises[i], 0);
        cereal_point(edge[i], axis[i], dir, 0, size * .025, size * (.050 - i * .009));
        if (i) qlcg_lp_quad(b, axis[i - 1], axis[i], edge[i], edge[i - 1], rachis, LP_MAT_SOLID);
    }
    cereal_point(axis[4], at, dir, size * reaches[4], size * rises[4], 0);
    qlcg_lp_triangle(b, axis[3], axis[4], edge[3], rachis, LP_MAT_SOLID);
    int step = simple ? 2 : 1;
    LpRgb lit = qlcg_lp_lighten(grain, .28);
    for (int i = 0; i < 6; i += step) {
        int segment = (int)branch_nodes[i];
        double t = branch_nodes[i] - segment;
        double root[3], outer[3], shoulder[3], inner[3], tip[3];
        for (int k = 0; k < 3; k++)
            root[k] = axis[segment][k] + (axis[segment + 1][k] - axis[segment][k]) * t;
        double side = (i / step) % 2 ? -1.0 : 1.0;
        double yaw = angle + side * (1.23 - .13 * i);
        double husk[2] = {cos(yaw), sin(yaw)};
        double len = size * (.35 - i * .027);
        double drop = len * (ripe ? .87 : .52);
        cereal_point(outer, root, husk, len * .53, -drop * .44, -len * .15);
        cereal_point(shoulder, root, husk, len * .70, -drop * .71, -len * .13);
        cereal_point(inner, root, husk, len * .57, -drop * .62, len * .16);
        cereal_point(tip, root, husk, len * .83, -drop, len * .02);
        qlcg_lp_triangle(b, root, outer, inner, grain, LP_MAT_SOLID);
        qlcg_lp_triangle(b, outer, shoulder, inner, lit, LP_MAT_SOLID);
        qlcg_lp_triangle(b, shoulder, tip, inner, grain, LP_MAT_SOLID);
    }
}

/* 簇生分蘖的水稻 */
static void build_crop_rice(LpBuilder *b, CropCtx *c, int simple) {
    int state = c->def->state;
    int young = state == PD_STATE_SEEDLING;
    int ripe = c->s->fruit;
    int stubble = state == PD_STATE_NORMAL && c->season >= PD_SEASON_AUTUMN;
    int dry = stubble || state == PD_STATE_WITHERED;
    int stalks = simple ? (c->p->stems + 1) / 2 : c->p->stems;
    double height = c->h * c->s->height;
    double spread = c->sp * c->s->spread;
    double leaf_width = c->p->leaf_w * (.74 + .26 * c->s->leaf_w);
    double leaf_reach = c->p->leaf_len * c->p->spread_scale * (.82 + .18 * c->s->leaf_len);
    double bearing = qlcg_lp_rng_next(c->rng) * TAU;
    LpRgb color = c->t->leaf;
    LpRgb gold = qlcg_lp_mix(c->t->fruit, c->t->bloom, .34);
    if (ripe) color = qlcg_lp_mix(color, qlcg_lp_rgb(107, 142, 66), .65);
    if (dry) color = c->t->dry;
    LpRgb leaf = qlcg_lp_wind_color(color, LP_WIND_LEAF);
    LpRgb stem = qlcg_lp_mix(leaf, c->t->stem, .20);
    if (ripe) stem = qlcg_lp_mix(leaf, gold, .47);
    LpRgb head = qlcg_lp_wind_color(ripe ? gold : qlcg_lp_mix(leaf, c->t->bloom, .36), LP_WIND_LEAF);

    int fans = young ? 4 : stalks;
    int rows = dry || simple ? 1 : 3;
    for (int i = 0; i < fans; i++) {
        double angle = bearing + i * TAU / fans + .25 * qlcg_lp_rng_next(c->rng);
        double dir[2] = {cos(angle), sin(angle)};
        double rad = spread * (.075 + .08 * qlcg_lp_rng_next(c->rng));
        double root[3] = {dir[0] * rad, 0, dir[1] * rad};
        double grow = qlcg_lp_rng_next(c->rng);
        double reach = leaf_reach * (.76 + .36 * grow);
        double lift = height * (.55 + .21 * (1 - grow));
        double width = leaf_width * (.85 + .27 * grow);
        double fall = .26 + .12 * grow;
        if (state == PD_STATE_SUBADULT) { reach *= .67; width *= .73; }
        if (young) { reach = lift * (.31 + .44 * grow); width = lift * .14; fall = .08; }
        if (dry) { reach *= .40; lift *= stubble ? .19 : .45; width *= .5; fall = .53; }
        LpRgb blade = qlcg_lp_mix(leaf, c->t->dark, .16 * grow);
        cereal_blade(b, root, angle, reach, width, lift, lift * (1 - fall), blade, rows);
    }
    if (young) return;

    for (int i = 0; i < stalks; i++) {
        double angle = bearing + i * GOLDEN + .42 + .26 * qlcg_lp_rng_next(c->rng);
        double dir[2] = {cos(angle), sin(angle)};
        double reach = spread * (i == 0 ? .21 : .47 + .30 * qlcg_lp_rng_next(c->rng));
        double h = height * (i == 0 ? .99 : .73 + .23 * qlcg_lp_rng_next(c->rng));
        double rad = spread * (.043 + .057 * qlcg_lp_rng_next(c->rng));
        double nodes[4][3] = {{dir[0] * rad, 0, dir[1] * rad}};
        const double *base = nodes[0];
        if (dry) {
            h *= stubble ? (c->season == PD_SEASON_WINTER ? .12 : .21) : .37;
            cereal_point(nodes[3], base, dir, reach * .34, h, 0);
            crop_stem_seg(b, base, nodes[3], c->p->stem_r * .74, .58, 3, stem);
            continue;
        }
        cereal_point(nodes[1], base, dir, reach * .12, h * .31, 0);
        cereal_point(nodes[2], base, dir, reach * .47, h * .69, 0);
        cereal_point(nodes[3], base, dir, reach, h, 0);
        cereal_stem(b, nodes, dir, c->p->stem_r, stem);
        double length = leaf_reach * (state == PD_STATE_SUBADULT ? .38 : .64);
        double flag_lift = h * (i % 2 ? .24 : .36);
        cereal_blade(b, nodes[2], angle + (i % 2 ? -.83 : 1.12), length, leaf_width * .74,
                     flag_lift, flag_lift * .84, leaf, simple ? 1 : 2);
        if (ripe || c->s->bloom) {
            if (ripe) qlcg_lp_socket(b, nodes[3], LP_SOCKET_FRUIT_CROP);
            double size = c->p->fruit_size * (ripe ? 1.0 : .83) * c->p->height_scale;
            rice_panicle(b, nodes[3], angle + .08, size, head, ripe, simple);
        }
    }
    if (stubble && c->season == PD_SEASON_WINTER)
        crop_record_snow(c, (double[3]){0, 0, 0});
}

/* 麦穗的交错小穗与细芒 */
static void wheat_ear(LpBuilder *b, const double at[3], double angle, double size, LpRgb grain) {
    double dir[2] = {cos(angle), sin(angle)};
    double base[3], belly[3], crown[3], ring[3][3];
    cereal_point(base, at, dir, 0, -size * .028, 0);
    cereal_point(belly, at, dir, size * .044, size * .40, 0);
    cereal_point(crown, at, dir, size * .10, size * .97, 0);
    for (int i = 0; i < 3; i++) {
        double turn = angle + i * TAU / 3;
        double radial[2] = {cos(turn), sin(turn)};
        cereal_point(ring[i], belly, radial, size * .157, 0, 0);
    }
    for (int i = 0; i < 3; i++) {
        int next = (i + 1) % 3;
        qlcg_lp_triangle(b, base, ring[i], ring[next], grain, LP_MAT_SOLID);
        qlcg_lp_triangle(b, ring[i], crown, ring[next], grain, LP_MAT_SOLID);
    }
    LpRgb glume = qlcg_lp_lighten(grain, .22);
    LpRgb awn = qlcg_lp_lighten(grain, .40);
    for (int tier = 0; tier < 3; tier++) {
        double y = .045 + tier * .23;
        double width = size * (1 - tier * .12);
        for (int i = 0; i < 2; i++) {
            double turn = angle + tier * PI / 3.0 + i * PI;
            double radial[2] = {cos(turn), sin(turn)};
            double root[3], left[3], right[3], ridge[3], tip[3];
            cereal_point(root, at, dir, size * .10 * y, size * (y + i * .05), 0);
            cereal_point(left, root, radial, width * .142, size * .16, -width * .126);
            cereal_point(right, root, radial, width * .149, size * .19, width * .126);
            cereal_point(ridge, root, radial, width * .231, size * .235, 0);
            cereal_point(tip, root, radial, width * .123, size * (.46 - tier * .055), 0);
            qlcg_lp_triangle(b, root, left, ridge, grain, LP_MAT_SOLID);
            qlcg_lp_triangle(b, root, ridge, right, glume, LP_MAT_SOLID);
            qlcg_lp_triangle(b, left, tip, ridge, glume, LP_MAT_SOLID);
            qlcg_lp_triangle(b, ridge, tip, right, grain, LP_MAT_SOLID);
            if (tier) {
                double heel[3], point[3];
                for (int k = 0; k < 3; k++) heel[k] = tip[k] + (right[k] - tip[k]) * .12;
                cereal_point(point, tip, radial, size * (.12 + .022 * i), size * (.41 + .03 * tier), 0);
                qlcg_lp_triangle(b, heel, point, tip, awn, LP_MAT_SOLID);
            }
        }
    }
}

static void build_crop_wheat(LpBuilder *b, CropCtx *c, int simple) {
    int ripe = c->s->fruit;
    int seedling = c->def->state == PD_STATE_SEEDLING;
    int withered = c->def->state == PD_STATE_WITHERED;
    int stubble = c->def->state == PD_STATE_NORMAL && c->season >= PD_SEASON_AUTUMN;
    int stalks = simple ? (c->p->stems + 1) / 2 : c->p->stems;
    if (seedling) stalks = (stalks + 2) / 2;
    double h = c->h * c->s->height;
    double spread = c->sp * c->s->spread;
    LpRgb gold = qlcg_lp_mix(c->t->fruit, c->t->bloom, .27);
    gold = qlcg_lp_mix(gold, qlcg_lp_rgb(232, 171, 104), .43);
    LpRgb straw = qlcg_lp_mix(c->t->dry, gold, .77);
    LpRgb foliage = qlcg_lp_wind_color(c->t->leaf, LP_WIND_LEAF);
    LpRgb stem = qlcg_lp_wind_color(qlcg_lp_mix(c->t->stem, foliage, .54), LP_WIND_LEAF);
    LpRgb head = qlcg_lp_wind_color(ripe ? gold : qlcg_lp_mix(foliage, c->t->bloom, .42), LP_WIND_LEAF);
    if (ripe) {
        foliage = qlcg_lp_mix(foliage, straw, .86);
        stem = qlcg_lp_mix(stem, straw, .90);
    }
    if (stubble || withered) foliage = stem = qlcg_lp_wind_color(c->t->dry, LP_WIND_LEAF);
    for (int i = 0; i < stalks; i++) {
        double angle = i * GOLDEN + qlcg_lp_rng_next(c->rng) * .48;
        double rad = spread * (.10 + qlcg_lp_rng_next(c->rng) * .44);
        double height = h * (.79 + .23 * qlcg_lp_rng_next(c->rng));
        double lean = spread * .16 * (.67 + .66 * qlcg_lp_rng_next(c->rng));
        double dir[2] = {cos(angle), sin(angle)};
        double nodes[4][3] = {{dir[0] * rad, 0, dir[1] * rad}};
        const double *base = nodes[0];
        if (seedling) {
            double len = height * .49;
            cereal_blade(b, base, angle, len, height * .11, height * .83, height * .95, foliage, 2);
            continue;
        }
        if (stubble || withered) {
            height *= withered ? .45 : c->season == PD_SEASON_AUTUMN ? .23 : .16;
            lean *= withered ? .9 : .32;
        }
        cereal_point(nodes[1], base, dir, lean * .23, height * .23, 0);
        cereal_point(nodes[2], base, dir, lean * .56, height * .56, 0);
        cereal_point(nodes[3], base, dir, lean, height, 0);
        if (stubble || withered)
            crop_stem_seg(b, base, nodes[3], c->p->stem_r, stubble ? .76 : .49, 3, stem);
        else
            cereal_stem(b, nodes, dir, c->p->stem_r, stem);
        int leaves = stubble || withered || simple ? 1 : 2;
        for (int j = 0; j < leaves; j++) {
            double len = c->p->leaf_len * (j ? .74 : 1.0) * (.78 + .22 * c->s->leaf_len);
            double width = c->p->leaf_w * (j ? .81 : 1.0) * (.65 + .35 * c->s->leaf_w);
            if (c->def->state == PD_STATE_SUBADULT) { len *= .65; width *= .74; }
            if (stubble || withered) { len *= .49; width *= .61; }
            double lift = len * .74;
            double end = lift - len * .23;
            if (stubble || withered) { lift *= .32; end = -height * .13; }
            LpRgb color = j ? qlcg_lp_lighten(foliage, .12) : foliage;
            int rows = !simple && !j && !stubble && !withered ? 2 : 1;
            cereal_blade(b, nodes[j + 1], angle + (j ? 2.08 : -.33), len, width, lift, end, color, rows);
        }
        if (ripe || (!withered && c->s->bloom)) {
            if (ripe) qlcg_lp_socket(b, nodes[3], LP_SOCKET_FRUIT_CROP);
            double size = c->p->fruit_size * (ripe ? 1.0 : .78) * c->p->height_scale;
            wheat_ear(b, nodes[3], angle, size, head);
        }
        if (stubble && c->season == PD_SEASON_WINTER && i == 0) crop_record_snow(c, base);
    }
}

static void crop_between(double p[3], const double a[3], const double b[3], double t) {
    for (int k = 0; k < 3; k++) p[k] = a[k] + (b[k] - a[k]) * t;
}

static LpRgb crop_leaf_tone(const CropCtx *c) {
    LpRgb color = qlcg_lp_wind_color(c->t->leaf, LP_WIND_LEAF);
    return qlcg_lp_mix(color, c->t->dry, c->s->dry * .30);
}

static void crop_berry(LpBuilder *b, const double at[3], double x, double y, double z,
                       int sides, double angle, LpRgb color, int mat) {
    LpOpts opts = qlcg_lp_opts();
    opts.rot_y = angle;
    opts.mat = mat;
    qlcg_lp_ico(b, at, (double[3]){x, y, z}, sides, color, &opts);
}

/* 锥度变化的连环截面 */
static void crop_rings(LpBuilder *b, const double centers[][3], const double radius[],
                       int count, int sides, double angle, const LpRgb colors[], int mat) {
    double prev[5][3], next[5][3];
    int down = centers[count - 1][1] < centers[0][1];
    for (int row = 0; row < count; row++) {
        for (int i = 0; i < sides; i++) {
            double yaw = angle + i * TAU / sides;
            next[i][0] = centers[row][0] + cos(yaw) * radius[row];
            next[i][1] = centers[row][1];
            next[i][2] = centers[row][2] + sin(yaw) * radius[row];
        }
        if (row) {
            for (int i = 0; i < sides; i++) {
                int j = (i + 1) % sides;
                int l = down ? j : i, r = down ? i : j;
                if (radius[row] == 0)
                    qlcg_lp_triangle(b, prev[l], next[l], prev[r], colors[row - 1], mat);
                else
                    qlcg_lp_quad(b, prev[l], next[l], next[r], prev[r], colors[row - 1], mat);
            }
        }
        memcpy(prev, next, (size_t)sides * sizeof(prev[0]));
    }
    if (radius[count - 1] > 0) {
        for (int i = 1; i < sides - 1; i++) {
            int l = down ? i : i + 1, r = down ? i + 1 : i;
            qlcg_lp_triangle(b, prev[0], prev[l], prev[r], colors[count - 2], mat);
        }
    }
}

typedef enum { LEAF_OVAL, LEAF_TOOTH, LEAF_THREE, LEAF_LYRE } FieldLeaf;

typedef struct { double reach, side; } LeafEdge;

static void crop_outline(LpBuilder *b, const double root[3], double angle,
                         double len, double width, double lift, LpRgb col, FieldLeaf shape) {
    static const LeafEdge oval[] = {
        {0,0}, {.20,-.32}, {.51,-.50}, {.79,-.29},
        {1,0}, {.78,.31}, {.47,.50}, {.19,.29}
    };
    static const LeafEdge tooth[] = {
        {0,0}, {.14,-.31}, {.31,-.51}, {.37,-.40}, {.58,-.49}, {.66,-.32},
        {1,0}, {.69,.30}, {.59,.46}, {.36,.39}, {.26,.50}, {.13,.29}
    };
    static const LeafEdge three[] = {
        {0,0}, {.23,-.32}, {.49,-.59}, {.49,-.28}, {.68,-.21},
        {1,0}, {.67,.21}, {.47,.28}, {.48,.58}, {.21,.31}
    };
    static const LeafEdge lyre[] = {
        {0,0}, {.22,-.23}, {.35,-.38}, {.39,-.21}, {.58,-.45}, {.77,-.51},
        {1,0}, {.77,.51}, {.58,.45}, {.39,.21}, {.35,.38}, {.22,.23}
    };
    static const struct { const LeafEdge *points; int count; } outlines[] = {
        {oval, 8}, {tooth, 12}, {three, 10}, {lyre, 12}
    };
    double dir[2] = {cos(angle), sin(angle)};
    double edge[12][3], rib[3];
    const LeafEdge *points = outlines[shape].points;
    int count = outlines[shape].count;
    cereal_point(rib, root, dir, len * .49, lift * .69 + width * .10, 0);
    for (int i = 0; i < count; i++) {
        double x = points[i].reach;
        double y = lift * x * (1.29 - .29 * x) + width * points[i].side * .20;
        cereal_point(edge[i], root, dir, len * x, y, width * points[i].side);
    }
    LpRgb lit = qlcg_lp_lighten(col, .15);
    LpRgb shade = qlcg_lp_deepen(col, .11);
    for (int i = 0; i < count; i++)
        qlcg_lp_triangle(b, rib, edge[(i + 1) % count], edge[i],
                         i < count / 2 ? lit : shade, LP_MAT_SOLID);
}

/* 细枝和花梗 */
static void crop_twig(LpBuilder *b, const double a[3], const double tip[3],
                      double angle, double width, LpRgb col) {
    double dir[2] = {cos(angle), sin(angle)}, left[3], right[3];
    cereal_point(left, a, dir, 0, 0, -width);
    cereal_point(right, a, dir, 0, 0, width);
    qlcg_lp_triangle(b, left, tip, right, col, LP_MAT_SOLID);
}

static void crop_petal(LpBuilder *b, const double root[3], double angle, double len,
                       double width, double lift, LpRgb color, int mat) {
    double dir[2] = {cos(angle), sin(angle)}, left[3], right[3], tip[3];
    cereal_point(left, root, dir, len * .55, lift * .54 + width * .18, -width * .49);
    cereal_point(right, root, dir, len * .61, lift * .66, width * .48);
    cereal_point(tip, root, dir, len, lift, 0);
    qlcg_lp_triangle(b, root, right, tip, color, mat);
    qlcg_lp_triangle(b, root, tip, left, qlcg_lp_lighten(color, .15), mat);
}

static void crop_corolla(LpBuilder *b, const double root[3], double angle, double size,
                         int petals, LpRgb color, double cup) {
    LpRgb pale = qlcg_lp_lighten(color, .22);
    for (int i = 0; i < petals; i++) {
        double a = angle + i * TAU / petals;
        double tilt = .23 * cos(i * TAU / petals);
        crop_petal(b, root, a, size, size * .88, size * (cup + tilt),
                   i % 2 ? color : pale, LP_MAT_BLOOM);
    }
}

static void crop_umbel(LpBuilder *b, const double root[3], double angle, double size,
                       int rays, int flat, LpRgb stem, LpRgb flower, int ripe) {
    for (int i = 0; i < rays; i++) {
        double a = angle + i * GOLDEN;
        double dir[2] = {cos(a), sin(a)}, at[3];
        double r = size * (.37 + .51 * (i % 3) / 2.0);
        double up = size * (flat ? .52 + .048 * (i % 2) : .62 + .31 * cos(i * 1.37));
        cereal_point(at, root, dir, r, up, 0);
        crop_twig(b, root, at, a, .026 * size, stem);
        if (ripe)
            crop_berry(b, at, size * .17, size * .22, size * .17, 3, a, flower, LP_MAT_FRUIT);
        else
            crop_corolla(b, at, a, size * (flat ? .31 : .37), 3, flower, .32);
    }
}

static void crop_pod(LpBuilder *b, const double root[3], double angle, double len,
                     double width, double fall, LpRgb col) {
    double dir[2] = {cos(angle), sin(angle)};
    double prev[3][3], next[3][3], tip[3];
    LpRgb lit = qlcg_lp_lighten(col, .19), shade = qlcg_lp_deepen(col, .10);
    for (int j = 0; j < 2; j++) {
        double reach = len * (j ? .70 : .28), height = fall * (j ? .64 : .39);
        double side = width * (j ? .47 : .41);
        cereal_point(next[0], root, dir, reach, height, -side);
        cereal_point(next[1], root, dir, reach, height + width * .33, 0);
        cereal_point(next[2], root, dir, reach, height, side);
        if (j) {
            qlcg_lp_quad(b, prev[0], prev[1], next[1], next[0], lit, LP_MAT_FRUIT);
            qlcg_lp_quad(b, prev[1], prev[2], next[2], next[1], shade, LP_MAT_FRUIT);
        } else {
            qlcg_lp_triangle(b, root, next[1], next[0], lit, LP_MAT_FRUIT);
            qlcg_lp_triangle(b, root, next[2], next[1], shade, LP_MAT_FRUIT);
        }
        memcpy(prev, next, sizeof(prev));
    }
    cereal_point(tip, root, dir, len, fall, 0);
    qlcg_lp_triangle(b, prev[0], prev[1], tip, lit, LP_MAT_FRUIT);
    qlcg_lp_triangle(b, prev[1], prev[2], tip, shade, LP_MAT_FRUIT);
}

static int crop_count(int count, int divisor, int sparse) {
    return sparse ? (count + divisor - 1) / divisor : count;
}

/* 粟的紧实穗轴与细芒 */
static void crop_foxtail(LpBuilder *b, const double root[3], double angle,
                         double size, LpRgb col, int flower) {
    double dir[2] = {cos(angle), sin(angle)};
    double points[4][3] = {{root[0],root[1] - size * .045,root[2]}};
    cereal_point(points[1], root, dir, size * .12, size * .23, 0);
    cereal_point(points[2], root, dir, size * .32, size * .73, 0);
    cereal_point(points[3], root, dir, size * .61, size * 1.02, 0);
    double r[] = {size * .055, size * .165, size * .126, 0};
    LpRgb colors[] = {col, qlcg_lp_lighten(col, .15), qlcg_lp_lighten(col, .25)};
    crop_rings(b, points, r, 4, 4, angle, colors, LP_MAT_SOLID);
    for (int i = 0; i < 12; i++) {
        double yaw = angle + i * GOLDEN;
        double outward[2] = {cos(yaw), sin(yaw)}, at[3], tip[3];
        double node[3];
        crop_between(node, points[1], points[2], .07 + .87 * i / 12);
        cereal_point(at, node, outward, (r[1] + (r[2] - r[1]) * i / 12) * .66, 0, 0);
        cereal_point(tip, at, outward, size * (.105 + .024 * (i % 3)), size * .24, 0);
        crop_twig(b, at, tip, yaw, size * .017, qlcg_lp_lighten(col, flower ? .22 : .38));
    }
}

static void build_crop_millet(LpBuilder *b, CropCtx *c, int small) {
    int ripe = c->s->fruit, bloom = c->s->bloom;
    int stubble = c->def->state == PD_STATE_NORMAL && c->season >= PD_SEASON_AUTUMN;
    int stems = crop_count(c->p->stems, 2, small);
    double turn = qlcg_lp_rng_next(c->rng) * TAU;
    LpRgb leaf = crop_leaf_tone(c);
    LpRgb gold = qlcg_lp_mix(c->t->fruit, c->t->bloom, .27);
    LpRgb heads = qlcg_lp_mix(leaf, ripe ? gold : c->t->bloom, ripe ? .80 : .31);
    if (ripe || stubble) leaf = qlcg_lp_mix(leaf, gold, stubble ? .67 : .43);
    for (int i = 0; i < stems; i++) {
        double a = turn + i * GOLDEN, dir[2] = {cos(a), sin(a)};
        double spread = c->sp * (.16 + .38 * qlcg_lp_rng_next(c->rng));
        double height = c->h * c->s->height * (.80 + .23 * qlcg_lp_rng_next(c->rng));
        double lean = c->sp * (.26 + .12 * (i % 3));
        double nodes[4][3] = {{dir[0] * spread, 0, dir[1] * spread}};
        if (stubble) height *= .22;
        cereal_point(nodes[1], nodes[0], dir, lean * .20, height * .28, 0);
        cereal_point(nodes[2], nodes[0], dir, lean * .54, height * .62, 0);
        cereal_point(nodes[3], nodes[0], dir, lean, height, 0);
        cereal_stem(b, nodes, dir, c->p->stem_r * (small ? .91 : 1), leaf);
        double len = c->p->leaf_len * (stubble ? .32 : .78 + .22 * c->s->leaf_len);
        cereal_blade(b, nodes[1], a - .54, len, c->p->leaf_w, len * .56,
                     len * .23, leaf, small || stubble ? 1 : 2);
        if (!small && !stubble)
            cereal_blade(b, nodes[2], a + 1.63, len * .66, c->p->leaf_w * .82,
                         len * .39, len * .34, leaf, 1);
        if (ripe || bloom) {
            if (ripe) qlcg_lp_socket(b, nodes[3], LP_SOCKET_FRUIT_CROP);
            crop_foxtail(b, nodes[3], a, c->p->fruit_size * (ripe ? 1.0 : .74), heads, bloom);
        }
        if (stubble && i == 0) crop_record_snow(c, nodes[0]);
    }
}

/* 甘蔗蔗节和高冠 */
static void build_crop_cane(LpBuilder *b, CropCtx *c, int small) {
    static const double height_at[] = {0,.25,.27,.54,.56,.76,1};
    static const double taper[] = {1.02,.91,1.02,.77,.90,.67,.38};
    double h = c->h * c->s->height;
    int winter = c->season == PD_SEASON_WINTER && c->def->state == PD_STATE_NORMAL;
    int culms = crop_count(c->p->stems, 2, small);
    LpRgb leaf = crop_leaf_tone(c);
    LpRgb purple = qlcg_lp_mix(c->t->stem, c->t->leaf, .19);
    LpRgb cane = qlcg_lp_wind_color(c->s->fruit ? purple : qlcg_lp_mix(c->t->leaf, purple, .43),
                                     LP_WIND_LEAF);
    double phase = qlcg_lp_rng_next(c->rng);
    for (int i = 0; i < culms; i++) {
        double angle = i * GOLDEN + phase, dir[2] = {cos(angle), sin(angle)};
        double height = h * (.78 + .24 * qlcg_lp_rng_next(c->rng));
        double rad = c->sp * (.12 + .27 * qlcg_lp_rng_next(c->rng));
        double base[3] = {dir[0] * rad, 0, dir[1] * rad};
        if (winter) {
            double tip[3] = {base[0], height * .20, base[2]};
            crop_stem_seg(b, base, tip, c->p->stem_r, .87, 4, c->t->dry);
            crop_record_fruit(c, tip);
            crop_record_snow(c, tip);
            continue;
        }
        double centers[7][3], radius[7];
        LpRgb colors[6];
        for (int j = 0; j < 7; j++) {
            double t = height_at[j];
            cereal_point(centers[j], base, dir, c->sp * .31 * t * t, height * t, 0);
            radius[j] = c->p->stem_r * taper[j];
            if (j < 6) colors[j] = j == 1 || j == 3 ? qlcg_lp_lighten(cane, .32) : cane;
        }
        crop_rings(b, centers, radius, 7, 4, angle, colors, LP_MAT_SOLID);
        crop_record_fruit(c, centers[6]);
        for (int j = 0; j < (small ? 2 : 3); j++) {
            double len = c->p->leaf_len * (.73 + .18 * j) * (.78 + .22 * c->s->leaf_len);
            cereal_blade(b, centers[4 + j], angle + .58 + j * GOLDEN, len,
                         c->p->leaf_w * 1.75, len * .39, -len * (.15 + .10 * j),
                         j == 1 ? qlcg_lp_lighten(leaf, .16) : leaf, small || j == 2 ? 1 : 2);
        }
        if (i == 0 && c->s->bloom) {
            double axis[3];
            cereal_point(axis, centers[6], dir, height * .038, height * .145, 0);
            crop_stem_seg(b, centers[6], axis, c->p->stem_r * .28, .41, 3, cane);
            LpRgb plume = qlcg_lp_wind_color(c->t->bloom, LP_WIND_LEAF);
            double size = c->p->bloom_size;
            for (int j = 0; j < 6; j++) {
                double at[3], tip[3], a = angle + j * GOLDEN;
                double outward[2] = {cos(a), sin(a)};
                crop_between(at, centers[6], axis, .28 + j * .10);
                cereal_point(tip, at, outward, size * (.34 - j * .028), size * (.51 + j * .043), 0);
                crop_twig(b, at, tip, a, size * .052, plume);
                crop_twig(b, at, tip, a + PI * .50, size * .038, qlcg_lp_lighten(plume, .19));
            }
        }
    }
}

static void crop_brassica_spray(LpBuilder *b, CropCtx *c, const double at[3],
                                double angle, double length, int small) {
    double dir[2] = {cos(angle), sin(angle)}, top[3];
    cereal_point(top, at, dir, length * .18, length, 0);
    crop_twig(b, at, top, angle, c->p->stem_r * .39, c->t->stem);
    int sites = small ? 2 : 3;
    for (int i = 0; i < sites; i++) {
        double yaw = angle + i * GOLDEN;
        double branch[2] = {cos(yaw), sin(yaw)}, node[3], end[3];
        crop_between(node, at, top, (.15 + .83 * i / (sites - 1)));
        cereal_point(end, node, branch, length * (.24 - i * .065), length * .10, 0);
        crop_twig(b, node, end, yaw, c->p->stem_r * .13, c->t->stem);
        if (c->s->fruit)
            crop_pod(b, end, yaw, c->p->fruit_size * .62, c->p->fruit_size * .15,
                     c->p->fruit_size * 1.30, c->t->fruit);
        else
            crop_corolla(b, end, yaw, c->p->bloom_size * (1.0 - .12 * i), 4, c->t->bloom, .26);
    }
}

/* 油菜的莲座和分叉花序 */
static void build_crop_brassica(LpBuilder *b, CropCtx *c, int small) {
    double base[3] = {0, 0, 0};
    double h = c->h * c->s->height;
    double phase = qlcg_lp_rng_next(c->rng) * TAU;
    int n = small ? 4 : 6;
    LpRgb leaf = crop_leaf_tone(c);
    for (int i = 0; i < n; i++) {
        double length = c->p->leaf_len * (.65 + .18 * (i % 3));
        crop_outline(b, base, phase + i * GOLDEN, length, c->p->leaf_w,
                      length * (.21 + .18 * (i % 3)), leaf, LEAF_OVAL);
    }
    if (c->season == PD_SEASON_WINTER && c->def->state == PD_STATE_NORMAL) {
        crop_record_snow(c, base);
        crop_record_fruit(c, base);
        return;
    }
    double tip[3] = {c->sp * .10, h * .86, 0};
    LpRgb stalk = qlcg_lp_mix(leaf, c->t->stem, .28);
    crop_stem_seg(b, base, tip, c->p->stem_r, .34, 3, stalk);
    int flowering = c->s->bloom || c->s->fruit;
    int branches = crop_count(c->p->branches, 2, small);
    crop_record_fruit(c, tip);
    if (flowering) crop_brassica_spray(b, c, tip, phase, h * .18, small);
    for (int i = 0; i < branches; i++) {
        double t = .41 + .49 * i / branches;
        double a = phase + i * GOLDEN, dir[2] = {cos(a), sin(a)};
        double node[3], at[3];
        crop_between(node, base, tip, t);
        cereal_point(at, node, dir, c->sp * (.64 - .41 * t), h * (.24 - .10 * t), 0);
        if (!flowering || i < 2)
            crop_outline(b, node, a + .44, c->p->leaf_len * (.73 - .38 * t),
                          c->p->leaf_w * .60, .071, leaf, LEAF_OVAL);
        crop_twig(b, node, at, a, c->p->stem_r * .50, stalk);
        crop_record_fruit(c, at);
        if (flowering) crop_brassica_spray(b, c, at, a, h * (.12 + .05 * t), small);
    }
}

static void crop_palm(LpBuilder *b, const double root[3], double angle,
                      double len, double width, LpRgb leaf, int small) {
    double dir[2] = {cos(angle), sin(angle)}, hub[3];
    cereal_point(hub, root, dir, len * .28, len * .13, 0);
    crop_twig(b, root, hub, angle, width * .064, leaf);
    int wings = small ? 1 : 2;
    for (int i = -wings; i <= wings; i++) {
        double scale = 1 - .21 * abs(i);
        double yaw = angle + i * .58;
        crop_petal(b, hub, yaw, len * .80 * scale, width * .24 * scale,
                   len * (.31 - .077 * abs(i)), i == 0 ? qlcg_lp_lighten(leaf, .15) : leaf,
                   LP_MAT_SOLID);
    }
}

static void crop_trifoliate(LpBuilder *b, const double root[3], double angle,
                             double len, double width, LpRgb leaf) {
    double dir[2] = {cos(angle), sin(angle)}, hub[3];
    cereal_point(hub, root, dir, len * .35, len * .22, 0);
    crop_twig(b, root, hub, angle, width * .038, leaf);
    for (int i = -1; i <= 1; i++) {
        double scale = i ? .71 : .91;
        cereal_blade(b, hub, angle + i * 1.02, len * scale, width * .59 * scale,
                     len * .19, len * .12, i == 0 ? qlcg_lp_lighten(leaf, .12) : leaf,
                     i == 0 ? 2 : 1);
    }
}

static void crop_fiber_strip(LpBuilder *b, const double at[3], double angle, double len,
                             double skin, LpRgb col) {
    double dir[2] = {cos(angle), sin(angle)};
    double a[3], l[3], r[3], tip[3];
    cereal_point(a, at, dir, skin * .65, -len * .43, 0);
    cereal_point(l, at, dir, skin, 0, -skin * .56);
    cereal_point(r, at, dir, skin, len * .13, skin * .57);
    cereal_point(tip, at, dir, skin * 1.17, len * .58, 0);
    qlcg_lp_triangle(b, a, l, r, col, LP_MAT_FRUIT);
    qlcg_lp_triangle(b, l, tip, r, qlcg_lp_lighten(col, .16), LP_MAT_FRUIT);
}

/* 麻的掌叶与苎麻的阔叶 */
static void build_crop_fiber(LpBuilder *b, CropCtx *c, int small) {
    int winter = c->season == PD_SEASON_WINTER && c->def->state == PD_STATE_NORMAL;
    int palm = c->p->leaf_form == CROP_LEAF_PALMATE;
    int stems = crop_count(c->p->stems, 2, small);
    LpRgb leaf = crop_leaf_tone(c);
    LpRgb stem = qlcg_lp_mix(leaf, c->t->stem, .59);
    if (winter) stem = qlcg_lp_mix(stem, c->t->dry, .83);
    double h = c->h * c->s->height;
    double phase = qlcg_lp_rng_next(c->rng) * TAU;
    for (int i = 0; i < stems; i++) {
        double a = phase + i * GOLDEN, dir[2] = {cos(a), sin(a)};
        double rad = c->sp * (.13 + .21 * qlcg_lp_rng_next(c->rng));
        double height = h * (.83 + .23 * qlcg_lp_rng_next(c->rng));
        double lean = c->sp * (palm ? .16 : .30) * (.70 + .39 * qlcg_lp_rng_next(c->rng));
        double nodes[4][3] = {{dir[0] * rad, 0, dir[1] * rad}};
        if (winter) {
            cereal_point(nodes[1], nodes[0], dir, lean * .40, height * .23, 0);
            crop_stem_seg(b, nodes[0], nodes[1], c->p->stem_r, .68, 3, stem);
            crop_record_fruit(c, nodes[1]);
            if (i < 2) crop_record_snow(c, nodes[1]);
            continue;
        }
        cereal_point(nodes[1], nodes[0], dir, lean * .18, height * .38, 0);
        cereal_point(nodes[2], nodes[0], dir, lean * .57, height * .70, 0);
        cereal_point(nodes[3], nodes[0], dir, lean, height, 0);
        cereal_stem(b, nodes, dir, c->p->stem_r * .62, stem);
        crop_record_fruit(c, nodes[1]);
        if (c->s->fruit)
            crop_fiber_strip(b, nodes[1], a, h * .22, c->p->stem_r * .64, c->t->fruit);
        for (int j = 1; j <= (small ? 2 : 3); j++) {
            double yaw = a + j * (palm ? 2.41 : 2.69);
            double size = (1.16 - .19 * j) * (.71 + .29 * c->s->leaf_len);
            double len = c->p->leaf_len * size, width = c->p->leaf_w * size;
            if (palm) {
                crop_palm(b, nodes[j], yaw, len, width, leaf, small);
            } else {
                LpRgb tone = j % 2 ? leaf : qlcg_lp_mix(leaf, qlcg_lp_rgb(206, 221, 196), .38);
                crop_outline(b, nodes[j], yaw, len, width, len * (j == 2 ? .37 : -.17),
                             tone, LEAF_TOOTH);
            }
        }
        if (c->s->bloom) {
            double at[3], tip[3];
            double size = c->p->bloom_size;
            cereal_point(at, nodes[3], dir, size * .22, size * 1.12, 0);
            crop_twig(b, nodes[3], at, a, c->p->stem_r * .26, stem);
            for (int j = 0; j < 3; j++) {
                double yaw = a + j * TAU / 3, rays[2] = {cos(yaw), sin(yaw)};
                cereal_point(tip, at, rays, size * (.47 + j * .10), size * (.31 - j * .22), 0);
                crop_twig(b, at, tip, yaw, size * .19,
                          qlcg_lp_mix(c->t->bloom, leaf, palm ? .34 : .49));
            }
        }
    }
}

static void crop_open_boll(LpBuilder *b, const double at[3], double angle, double size, LpRgb col) {
    LpRgb husk = qlcg_lp_rgb(120, 86, 57);
    LpRgb ivory = qlcg_lp_mix(col, qlcg_lp_rgb(255, 246, 218), .27);
    for (int i = 0; i < 3; i++) {
        double a = angle + i * TAU / 3, dir[2] = {cos(a), sin(a)};
        double center[3], left[3], right[3], cusp[3];
        cereal_point(center, at, dir, size * .26, size * (.39 + .065 * i), 0);
        cereal_point(left, at, dir, size * .38, size * .13, -size * .25);
        cereal_point(right, at, dir, size * .35, size * .13, size * .25);
        cereal_point(cusp, at, dir, size * .79, size * .38, 0);
        qlcg_lp_triangle(b, left, cusp, right, husk, LP_MAT_SOLID);
        crop_berry(b, center, size * .38, size * (.39 - i * .035), size * .36, 4, a, ivory, LP_MAT_FRUIT);
    }
}

/* 棉花的分层枝和吐絮棉铃 */
static void build_crop_cotton(LpBuilder *b, CropCtx *c, int small) {
    double base[3] = {0, 0, 0};
    double h = c->h * c->s->height;
    double phase = qlcg_lp_rng_next(c->rng) * TAU;
    double top[3] = {c->sp * .066, h * .92, c->sp * .051};
    int winter = c->season == PD_SEASON_WINTER && c->def->state == PD_STATE_NORMAL;
    LpRgb leaf = crop_leaf_tone(c);
    LpRgb stem = qlcg_lp_mix(leaf, c->t->stem, .79);
    crop_stem_seg(b, base, top, c->p->stem_r * .78, .30, 4, stem);
    if (!c->s->fruit) crop_record_fruit(c, top);
    int branches = crop_count(c->p->branches, 2, small);
    for (int i = 0; i < branches; i++) {
        double t = .30 + .57 * i / branches;
        double angle = phase + i * GOLDEN, dir[2] = {cos(angle), sin(angle)};
        double node[3], at[3];
        crop_between(node, base, top, t);
        double reach = c->sp * (.74 - .45 * t) * (.89 + .19 * qlcg_lp_rng_next(c->rng));
        cereal_point(at, node, dir, reach, h * (.11 + .035 * (i % 3)), 0);
        crop_stem_seg(b, node, at, c->p->stem_r * .34, .26, 3, stem);
        crop_record_fruit(c, at);
        if (winter) {
            crop_record_snow(c, at);
            continue;
        }
        double len = c->p->leaf_len * (1.10 - .36 * t);
        crop_outline(b, at, angle + (i % 2 ? .54 : -.49), len,
                      c->p->leaf_w * 1.11, len * (i % 2 ? -.09 : .38), leaf, LEAF_THREE);
        if (i < 2 && !small)
            crop_outline(b, node, angle + 2.09, len * .93, c->p->leaf_w, len * .12, leaf, LEAF_THREE);
        if (c->s->fruit) {
            crop_open_boll(b, at, angle, c->p->fruit_size, c->t->fruit);
        } else if (c->s->bloom) {
            crop_corolla(b, at, angle, c->p->bloom_size * .82, 5, c->t->bloom, .48);
            double center[3] = {at[0], at[1] + .025, at[2]};
            crop_berry(b, center, .013, .023, .013, 3, angle, qlcg_lp_rgb(213, 173, 74), LP_MAT_BLOOM);
        }
    }
}

static void crop_perilla_spike(LpBuilder *b, CropCtx *c, const double at[3], double angle) {
    double dir[2] = {cos(angle), sin(angle)}, top[3];
    double len = c->p->bloom_size * (c->s->fruit ? 3.65 : 4.26);
    cereal_point(top, at, dir, len * .12, len, 0);
    crop_twig(b, at, top, angle, c->p->stem_r * .22, c->t->stem);
    for (int i = 0; i < 5; i++) {
        double a = angle + i * GOLDEN, side[2] = {cos(a), sin(a)};
        double node[3], tip[3];
        crop_between(node, at, top, (.18 + i * .19));
        double size = c->p->bloom_size * (.36 - i * .024);
        cereal_point(tip, node, side, size * .71, size * .22, 0);
        crop_twig(b, node, tip, a, size * .11, c->t->stem);
        if (c->s->fruit)
            crop_berry(b, tip, size * .43, size * .60, size * .43, 3, a, c->t->fruit, LP_MAT_FRUIT);
        else
            crop_corolla(b, tip, a, size, 3, c->t->bloom, .50);
    }
}

/* 大豆、胡麻和紫苏 */
static void build_crop_herb(LpBuilder *b, CropCtx *c, int small) {
    int soy = c->p->produce == CROP_POD;
    int sesame = c->p->produce == CROP_SESAME;
    int stems = crop_count(c->p->stems, 2, small);
    double phase = qlcg_lp_rng_next(c->rng) * TAU;
    LpRgb leaf = crop_leaf_tone(c);
    LpRgb stem = qlcg_lp_mix(leaf, c->t->stem, .34);
    if (!soy && !sesame) stem = qlcg_lp_mix(stem, c->t->dark, .31);
    for (int i = 0; i < stems; i++) {
        double a = phase + i * GOLDEN, dir[2] = {cos(a), sin(a)};
        double r = c->sp * (.07 + .11 * qlcg_lp_rng_next(c->rng));
        double h = c->h * c->s->height * (.80 + .20 * qlcg_lp_rng_next(c->rng));
        double lean = c->sp * (soy ? .63 : sesame ? .19 : .44);
        double nodes[4][3] = {{dir[0] * r, 0, dir[1] * r}};
        cereal_point(nodes[1], nodes[0], dir, lean * .30, h * .38, 0);
        cereal_point(nodes[2], nodes[0], dir, lean * .66, h * .69, 0);
        cereal_point(nodes[3], nodes[0], dir, lean, h, 0);
        cereal_stem(b, nodes, dir, c->p->stem_r * (soy ? .68 : .58), stem);
        for (int j = 1; j <= (small ? 2 : 3); j++) {
            double yaw = a + j * (sesame ? PI * .57 : GOLDEN);
            double len = c->p->leaf_len * (1.17 - .18 * j) * (.76 + .24 * c->s->leaf_len);
            double width = c->p->leaf_w * (1.11 - .14 * j);
            if (soy) {
                crop_trifoliate(b, nodes[j], yaw, len, width, leaf);
            } else if (sesame) {
                crop_outline(b, nodes[j], yaw, len, width * .76, len * .32, leaf, LEAF_OVAL);
                if (j == 1 && !small)
                    crop_outline(b, nodes[1], yaw + PI, len * .93, width * .73,
                                 len * .25, leaf, LEAF_OVAL);
            } else if (j < 3) {
                crop_outline(b, nodes[j], yaw, len, width, len * .30, leaf, LEAF_TOOTH);
                if (!small)
                    crop_outline(b, nodes[j], yaw + PI, len * .88, width,
                                 -len * .17, qlcg_lp_lighten(leaf, .11), LEAF_TOOTH);
            }
            if (soy || sesame) {
                crop_record_fruit(c, nodes[j]);
                if (c->s->fruit && (sesame || j < 3)) {
                    double size = c->p->fruit_size;
                    if (soy) {
                        crop_pod(b, nodes[j], yaw - .51, size * 1.43, size * .51, -size * 1.37, c->t->fruit);
                        if (!small)
                            crop_pod(b, nodes[j], yaw + .71, size * 1.12, size * .44, -size * 1.59, c->t->fruit);
                    } else {
                        double out[2] = {cos(yaw), sin(yaw)}, at[3];
                        cereal_point(at, nodes[j], out, size * .32, size * .68, 0);
                        crop_berry(b, at, size * .39, size * 1.10, size * .39, 4, yaw, c->t->fruit, LP_MAT_FRUIT);
                    }
                } else if (c->s->bloom && (sesame || j > 1)) {
                    double out[2] = {cos(yaw), sin(yaw)}, at[3];
                    double size = c->p->bloom_size;
                    cereal_point(at, nodes[j], out, size * .45, -size * (sesame ? .41 : .09), 0);
                    crop_twig(b, nodes[j], at, yaw, size * .14, stem);
                    if (sesame) {
                        double throat[3] = {at[0], at[1] + size * .17, at[2]};
                        crop_berry(b, throat, size * .23, size * .37, size * .23, 3, yaw,
                                   qlcg_lp_mix(c->t->bloom, qlcg_lp_rgb(149, 106, 159), .42), LP_MAT_BLOOM);
                    }
                    crop_corolla(b, at, yaw, size * (sesame ? .64 : .59), sesame ? 5 : 3,
                                 c->t->bloom, sesame ? .69 : .33);
                }
            }
        }
        if (!soy && !sesame) {
            crop_record_fruit(c, nodes[3]);
            if (c->s->bloom || c->s->fruit) crop_perilla_spike(b, c, nodes[3], a);
        }
    }
}

/* 葱管、韭带和薤的叶脊 */
static void build_crop_allium(LpBuilder *b, CropCtx *c, int small) {
    int tube = c->p->leaf_form == CROP_LEAF_TUBE;
    int flat = c->p->leaf_form == CROP_LEAF_FLAT;
    int winter = c->season == PD_SEASON_WINTER && c->def->state == PD_STATE_NORMAL;
    int count = crop_count(c->p->stems, 2, small || winter);
    double h = c->h * c->s->height;
    double spread = c->sp * c->s->spread;
    double phase = qlcg_lp_rng_next(c->rng) * TAU;
    LpRgb leaf = crop_leaf_tone(c);
    for (int i = 0; i < count; i++) {
        double a = phase + i * GOLDEN, dir[2] = {cos(a), sin(a)};
        double rad = spread * (.06 + .33 * qlcg_lp_rng_next(c->rng));
        double height = h * (.68 + .31 * qlcg_lp_rng_next(c->rng));
        double reach = spread * (tube ? .62 : flat ? 1.22 : .94) * (.75 + .32 * (i % 4));
        double base[3] = {dir[0] * rad, 0, dir[1] * rad};
        LpRgb color = i % 2 ? qlcg_lp_lighten(leaf, .13) : leaf;
        double width = c->p->leaf_w * (.71 + .29 * c->s->leaf_w);
        if (tube) {
            double nodes[4][3];
            memcpy(nodes[0], base, sizeof(base));
            cereal_point(nodes[1], base, dir, reach * .12, height * .23, 0);
            cereal_point(nodes[2], base, dir, reach * .42, height * .74, 0);
            cereal_point(nodes[3], base, dir, reach, height, 0);
            double radii[] = {width * .42, width * .46, width * .33, 0};
            LpRgb pale = qlcg_lp_mix(color, qlcg_lp_rgb(228, 237, 203), .82);
            LpRgb colors[] = {pale, color, qlcg_lp_lighten(color, .13)};
            crop_rings(b, nodes, radii, 4, 3, a, colors, LP_MAT_SOLID);
        } else {
            double top = height * (flat ? .68 : 1.01);
            double tip = top * (flat ? .62 : .80);
            cereal_blade(b, base, a, reach, width * (flat ? 1.12 : .84), top, tip, color, small ? 1 : 3);
        }
    }
    int heads = crop_count(c->p->branches, 2, small);
    for (int i = 0; i < heads; i++) {
        double a = phase + .15 + i * GOLDEN, dir[2] = {cos(a), sin(a)};
        double base[3] = {dir[0] * spread * .25, 0, dir[1] * spread * .25};
        if (!c->s->bloom && !c->s->fruit) {
            crop_record_fruit(c, base);
            continue;
        }
        double tip[3];
        cereal_point(tip, base, dir, spread * (.14 + .08 * i), h * (1.03 + .11 * i), 0);
        crop_stem_seg(b, base, tip, c->p->stem_r, .48, 3, leaf);
        crop_record_fruit(c, tip);
        crop_umbel(b, tip, a, c->p->bloom_size * (tube ? 1.05 : .84),
                   small ? 4 : 6, flat, leaf, c->s->fruit ? c->t->fruit : c->t->bloom, c->s->fruit);
    }
}

static void crop_carrot_leaf(LpBuilder *b, const double root[3], double angle,
                             double len, double width, double lift, LpRgb leaf, int small) {
    double dir[2] = {cos(angle), sin(angle)}, tip[3];
    cereal_point(tip, root, dir, len, lift, 0);
    crop_twig(b, root, tip, angle, width * .050, leaf);
    int pairs = small ? 2 : 4;
    for (int i = 0; i < pairs; i++) {
        double t = .30 + .52 * i / pairs;
        double at[3];
        crop_between(at, root, tip, t);
        for (int j = -1; j <= 1; j += 2) {
            double a = angle + j * (1.25 - .22 * t);
            double size = width * (1.35 - i * .19);
            crop_petal(b, at, a, size, size * .45, size * .72,
                       j > 0 ? leaf : qlcg_lp_lighten(leaf, .21), LP_MAT_SOLID);
        }
    }
    double at[3];
    crop_between(at, root, tip, .80);
    crop_petal(b, at, angle, len * .29, width * .26, len * .20, leaf, LP_MAT_SOLID);
}

static void crop_taproot(LpBuilder *b, const double base[3], double angle,
                         double r, int carrot, LpRgb color) {
    double top = r * (carrot ? .28 : .46);
    double dir[2] = {cos(angle), sin(angle)}, nodes[5][3];
    cereal_point(nodes[0], base, dir, 0, top, 0);
    cereal_point(nodes[1], base, dir, r * .03, -r * .43, 0);
    cereal_point(nodes[2], base, dir, r * .13, -r * (carrot ? 2.12 : 1.92), 0);
    cereal_point(nodes[3], base, dir, r * .29, -r * (carrot ? 3.60 : 2.78), 0);
    cereal_point(nodes[4], base, dir, r * .42, -r * (carrot ? 4.30 : 3.34), 0);
    double widths[] = {r * .69, r * .89, r * (carrot ? .39 : .78), r * (carrot ? .14 : .33), 0};
    LpRgb colors[] = {qlcg_lp_lighten(color, .15), color, color, qlcg_lp_deepen(color, .12)};
    crop_rings(b, nodes, widths, 5, 5, angle, colors, LP_MAT_FRUIT);
    double lip[3] = {base[0], base[1] + top + r * .11, base[2]};
    double rim[5][3];
    for (int i = 0; i < 5; i++) {
        double a = angle + i * TAU / 5, edge[2] = {cos(a), sin(a)};
        cereal_point(rim[i], nodes[0], edge, widths[0], 0, 0);
    }
    for (int i = 0; i < 5; i++)
        qlcg_lp_triangle(b, rim[i], lip, rim[(i + 1) % 5], colors[0], LP_MAT_FRUIT);
}

/* 菘的抱心叶与根菜叶冠 */
static void build_crop_rosette(LpBuilder *b, CropCtx *c, int small) {
    int carrot = c->p->produce == CROP_CARROT;
    int radish = c->p->produce == CROP_RADISH;
    int winter = c->season == PD_SEASON_WINTER && c->def->state == PD_STATE_NORMAL;
    int count = crop_count(c->p->stems, 2, small || winter);
    double base[3] = {0, 0, 0};
    double phase = qlcg_lp_rng_next(c->rng) * TAU;
    double leaf_scale = (.73 + .27 * c->s->leaf_len) * (c->p->spread_scale > 0 ? c->p->spread_scale : 1);
    LpRgb leaf = crop_leaf_tone(c);
    double r = c->p->fruit_size;
    if ((carrot || radish) && c->s->fruit) base[1] = r * (carrot ? .31 : .51);
    for (int i = 0; i < count; i++) {
        double t = (i + .5) / count;
        double angle = phase + i * GOLDEN;
        double len = c->p->leaf_len * leaf_scale * (1.27 - .53 * t);
        double width = c->p->leaf_w * leaf_scale * (1.02 - .18 * t);
        double rise = len * (.34 + t * 1.14);
        if (carrot) {
            crop_carrot_leaf(b, base, angle, len * .76, width * .79, rise, leaf, small);
        } else if (radish) {
            crop_outline(b, base, angle, len, width, rise * .68, leaf, LEAF_LYRE);
        } else {
            LpRgb col = qlcg_lp_mix(leaf, qlcg_lp_rgb(206, 220, 137), .60 * t);
            cereal_blade(b, base, angle, len * (1.04 - .52 * t), width * (1.18 - .44 * t),
                         rise, rise * (t < .38 ? .43 : .98), col, small ? 2 : 3);
        }
    }
    if (c->s->fruit && (carrot || radish))
        crop_taproot(b, (double[3]){0, 0, 0}, phase, r, carrot, c->t->fruit);
    if (c->s->bloom || (c->s->fruit && !carrot && !radish)) {
        double h = c->h * c->s->height;
        int sprays = small ? 2 : 3;
        for (int i = 0; i < sprays; i++) {
            double angle = phase + i * GOLDEN, dir[2] = {cos(angle), sin(angle)}, top[3];
            cereal_point(top, base, dir, c->sp * (.04 + .29 * i), h * (.94 + .22 * (i == 0)), 0);
            crop_stem_seg(b, base, top, c->p->stem_r * .63, .30, 3, leaf);
            if (carrot)
                crop_umbel(b, top, angle, c->p->bloom_size * 1.04, small ? 4 : 5,
                           1, leaf, c->t->bloom, 0);
            else
                crop_brassica_spray(b, c, top, angle, h * .29, 1);
            if (c->s->fruit) crop_record_fruit(c, top);
        }
    }
    if (!c->s->fruit || carrot || radish) crop_record_fruit(c, base);
    if (winter) crop_record_snow(c, base);
}

/* 莼菜水面椭圆叶 */
static void build_crop_floating(LpBuilder *b, CropCtx *c, int small) {
    int count = crop_count(c->p->stems, 2, small);
    int winter = c->season == PD_SEASON_WINTER && c->def->state == PD_STATE_NORMAL;
    if (winter) count = (count + 1) / 2;
    double h = c->h * c->s->height;
    double phase = qlcg_lp_rng_next(c->rng) * TAU;
    LpRgb leaf = crop_leaf_tone(c);
    LpRgb stem = qlcg_lp_mix(leaf, qlcg_lp_rgb(112, 65, 81), .44);
    for (int i = 0; i < count; i++) {
        double angle = phase + i * GOLDEN, dir[2] = {cos(angle), sin(angle)};
        double reach = c->sp * (.31 + .38 * qlcg_lp_rng_next(c->rng));
        double base[3] = {dir[0] * reach * .12, 0, dir[1] * reach * .12};
        double at[3], rim[10][3];
        cereal_point(at, base, dir, reach, h * (.77 + .10 * (i % 3)), 0);
        crop_stem_seg(b, base, at, c->p->stem_r, .48, 3, stem);
        double len = c->p->leaf_len * (.78 + .25 * (i % 3));
        double width = c->p->leaf_w * (.81 + .13 * (i % 3));
        int edges = small ? 8 : 10;
        for (int j = 0; j < edges; j++) {
            double a = j * TAU / edges;
            double bend = width * (.18 + .11 * cos(2.0 * a) + .09 * sin(a));
            cereal_point(rim[j], at, dir, cos(a) * len, bend, sin(a) * width);
        }
        for (int j = 0; j < edges; j++) {
            LpRgb face = qlcg_lp_mix(leaf, c->t->dark, .09 + .16 * (j % 3) / 2.0);
            qlcg_lp_triangle(b, at, rim[(j + 1) % edges], rim[j], face, LP_MAT_SOLID);
        }
        if (i % 2 == 0) {
            double end[3];
            cereal_point(end, at, dir, len * .77, width * .49, width * .95);
            crop_record_fruit(c, end);
            if (c->s->bloom || c->s->fruit)
                crop_twig(b, at, end, angle, c->p->stem_r * .33, stem);
            if (c->s->bloom) {
                crop_corolla(b, end, angle, c->p->bloom_size * .58, 4, c->t->bloom, .43);
            } else if (c->s->fruit) {
                crop_berry(b, end, c->p->fruit_size * .42, c->p->fruit_size * .63,
                           c->p->fruit_size * .42, 3, angle, c->t->fruit, LP_MAT_FRUIT);
            }
        }
    }
}

/* 错落的冠叶色块 */
static void crop_canopy(LpBuilder *b, const double at[3], double angle, double x,
                        double y, double z, LpRgb leaf) {
    static const double scale[] = {1.06, .89, 1.12, .96};
    static const double height[] = {.07, -.14, .25, -.09};
    double dir[2] = {cos(angle), sin(angle)}, rim[4][3], top[3], lower[3];
    cereal_point(top, at, dir, x * .19, y * .76, z * .10);
    cereal_point(lower, at, dir, -x * .13, -y * .51, -z * .04);
    for (int i = 0; i < 4; i++) {
        double t = i * PI * .50 + .13;
        cereal_point(rim[i], at, dir, x * cos(t) * scale[i], y * height[i], z * sin(t) * scale[i]);
    }
    LpRgb lit = qlcg_lp_lighten(leaf, .16);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        qlcg_lp_triangle(b, rim[i], top, rim[j], i % 2 ? lit : leaf, LP_MAT_SOLID);
        qlcg_lp_triangle(b, rim[i], rim[j], lower, leaf, LP_MAT_SOLID);
    }
}

static void crop_tea_branch(LpBuilder *b, CropCtx *c, const double node[3],
                             const double at[3], double angle, LpRgb leaf, int small) {
    double len = c->p->leaf_len * (.76 + .24 * c->s->leaf_len);
    double width = c->p->leaf_w * (.70 + .30 * c->s->leaf_w);
    LpRgb shade = qlcg_lp_mix(leaf, c->t->dark, .46);
    double shoulder[3];
    crop_between(shoulder, node, at, .84);
    crop_canopy(b, shoulder, angle, len * 1.08, len * .39, len * .84, shade);
    crop_outline(b, at, angle, len, width * .69, len * .28, leaf, LEAF_OVAL);
    crop_outline(b, at, angle - 1.18, len * .79, width * .77, len * .18, leaf, LEAF_OVAL);
    if (!small)
        crop_outline(b, shoulder, angle + 1.13, len * .87, width * .83,
                     len * .37, qlcg_lp_lighten(leaf, .15), LEAF_OVAL);
    crop_record_fruit(c, at);
    if (c->s->bloom)
        crop_corolla(b, at, angle, c->p->bloom_size * .94, 5, c->t->bloom, .31);
    else if (c->s->fruit)
        crop_berry(b, at, c->p->fruit_size * .86, c->p->fruit_size * .78,
                   c->p->fruit_size * .86, 4, angle, c->t->fruit, LP_MAT_FRUIT);
}

/* 茶蓬、桑冠和楮的疏叉 */
static void build_crop_woody(LpBuilder *b, CropCtx *c, int small) {
    int tea = c->p->produce == CROP_CAPSULE;
    int paper = c->p->produce == CROP_BARK;
    int bare = !tea && c->season == PD_SEASON_WINTER && c->def->state == PD_STATE_NORMAL;
    double h = c->h * c->s->height;
    double spread = c->sp * c->s->spread;
    double phase = qlcg_lp_rng_next(c->rng) * TAU;
    double ray[2] = {cos(phase), sin(phase)};
    double base[3] = {0, 0, 0};
    double trunk[3][3] = {{0,0,0}};
    cereal_point(trunk[1], base, ray, spread * .071, h * .36, 0);
    cereal_point(trunk[2], base, ray, spread * .15, h * .78, 0);
    LpRgb stem = qlcg_lp_wind_color(c->t->stem, LP_WIND_BARK);
    LpRgb leaf = crop_leaf_tone(c);
    if (!tea) {
        double widths[] = {c->p->stem_r, c->p->stem_r * .69, c->p->stem_r * .22};
        LpRgb tones[] = {qlcg_lp_deepen(stem, .09), stem};
        crop_rings(b, trunk, widths, 3, 5, phase, tones, LP_MAT_SOLID);
    }
    double forks[2][3];
    if (paper) {
        for (int i = 0; i < 2; i++) {
            cereal_point(forks[i], trunk[1], ray, spread * .12, h * (.24 + .085 * i),
                         spread * (i ? .35 : -.32));
            crop_stem_seg(b, trunk[1], forks[i], c->p->stem_r * .64, .36, 3, stem);
        }
    }
    int count = crop_count(c->p->branches, 2, small);
    for (int i = 0; i < count; i++) {
        double t = (i + .5) / count;
        double angle = phase + i * GOLDEN + .45 * qlcg_lp_rng_next(c->rng);
        double dir[2] = {cos(angle), sin(angle)};
        double start[3], node[3], at[3];
        if (tea) {
            cereal_point(start, base, dir, spread * .07, 0, 0);
            cereal_point(at, start, dir, spread * (.43 + .18 * (i % 3)), h * (.57 + .20 * t), 0);
            crop_stem_seg(b, start, at, c->p->stem_r * .51, .23, 3, stem);
            crop_tea_branch(b, c, start, at, angle, leaf, small);
        } else {
            crop_between(start, trunk[1], paper ? forks[i % 2] : trunk[2], .10 + t * .62);
            double width = spread * (paper ? .82 - .19 * t : .87 - .37 * t);
            cereal_point(node, start, dir, width * .54, h * (.06 + t * .032), -width * .11);
            cereal_point(at, node, dir, width * .45, h * (paper ? .21 + .048 * (i % 2) : .16 - .045 * t),
                         width * (i % 2 ? .16 : -.06));
            double r = c->p->stem_r * (.50 - .21 * t);
            crop_stem_seg(b, start, node, r, .62, 3, stem);
            crop_stem_seg(b, node, at, r * .63, .23, 3, stem);
            if (!bare) {
                double canopy[3], crown[3];
                cereal_point(canopy, at, dir, width * .05, h * .047, 0);
                cereal_point(crown, canopy, dir, -width * .34, h * (.061 + .012 * (i % 2)), width * .23);
                LpRgb shade = qlcg_lp_mix(leaf, c->t->dark, paper ? .27 : .38);
                double layer = h * (paper ? .117 : .115);
                crop_canopy(b, canopy, angle, width * .77, layer, width * (paper ? .52 : .77), shade);
                crop_canopy(b, crown, angle + .47, width * .69, layer * .94,
                            width * (paper ? .63 : .74), qlcg_lp_lighten(leaf, .10));
                if (!small) {
                    double mark[3];
                    cereal_point(mark, at, dir, width * .39, h * .047, 0);
                    crop_outline(b, mark, angle - .20, c->p->leaf_len, c->p->leaf_w, -h * .028,
                                 leaf, paper ? LEAF_THREE : LEAF_TOOTH);
                }
            }
            if (paper) {
                double slot[3];
                crop_between(slot, trunk[0], trunk[1], .31 + .55 * t);
                crop_record_fruit(c, slot);
                if (c->s->fruit)
                    crop_fiber_strip(b, slot, angle, h * .15, c->p->stem_r * .89, c->t->fruit);
            } else {
                crop_record_fruit(c, at);
                if (i % 2 == 0 && (c->s->bloom || c->s->fruit)) {
                    double berry[3] = {at[0], at[1] - c->p->fruit_size * .49, at[2]};
                    double size = c->s->fruit ? c->p->fruit_size : c->p->bloom_size;
                    LpRgb tone = c->s->fruit ? c->t->fruit : c->t->bloom;
                    crop_berry(b, berry, size * .48, size * .97, size * .48, 4, angle, tone,
                               c->s->fruit ? LP_MAT_FRUIT : LP_MAT_BLOOM);
                }
            }
        }
        if (i % 2 == 0) crop_record_snow(c, at);
    }
}

static void build_crop_sprout(LpBuilder *b, CropCtx *c) {
    double base[3] = {0, 0, 0};
    double h = c->h * (.80 + .20 * c->s->height);
    double phase = qlcg_lp_rng_next(c->rng) * TAU;
    double len = h * .60;
    double width = len * .61;
    LpRgb leaf = crop_leaf_tone(c);
    int stalked = c->p->habit == CROP_WOODY || c->p->habit == CROP_BOLL_SHRUB;
    double collar[3] = {0, h * .32, 0};
    if (stalked) crop_stem_seg(b, base, collar, c->p->stem_r * .21, .36, 3, leaf);
    for (int i = 0; i < 4; i++) {
        double a = phase + i * GOLDEN;
        double scale = .70 + .14 * i;
        const double *at = stalked ? collar : base;
        if (c->p->habit == CROP_GRAIN || c->p->habit == CROP_ALLIUM || c->p->habit == CROP_CANE) {
            cereal_blade(b, at, a, len * scale * .68, width * .36, len * (.83 + .22 * i),
                         len * (.75 + .24 * i), leaf, 2);
        } else if (c->p->produce == CROP_CARROT) {
            crop_carrot_leaf(b, at, a, len * scale, width * .46, len * .63, leaf, 1);
        } else {
            LpRgb tone = i % 2 ? qlcg_lp_lighten(leaf, .15) : leaf;
            crop_outline(b, at, a, len * scale, width * (1 - .14 * i),
                         len * (.34 + .19 * i), tone, LEAF_OVAL);
        }
    }
}

static void build_crop_wilt(LpBuilder *b, CropCtx *c) {
    int woody = c->p->habit == CROP_WOODY || c->p->habit == CROP_BOLL_SHRUB;
    double phase = qlcg_lp_rng_next(c->rng) * TAU;
    double h = c->h * c->s->height;
    int count = woody ? 4 : crop_count(c->p->stems + 1, 2, 1);
    LpRgb dry = qlcg_lp_wind_color(c->t->dry, woody ? LP_WIND_BARK : LP_WIND_LEAF);
    double origin[3] = {0, 0, 0}, trunk[3] = {c->sp * .09, h * .61, 0};
    if (woody) crop_stem_seg(b, origin, trunk, c->p->stem_r * .76, .29, 4, dry);
    for (int i = 0; i < count; i++) {
        double a = phase + i * GOLDEN, dir[2] = {cos(a), sin(a)};
        double base[3] = {0,0,0}, top[3];
        if (woody) crop_between(base, origin, trunk, .38 + i * .13);
        cereal_point(top, base, dir, c->sp * (.17 + .11 * (i % 3)), h * (.23 + .074 * (i % 3)), 0);
        crop_stem_seg(b, base, top, c->p->stem_r * (woody ? .43 : .64), .28, 3, dry);
        double len = c->p->leaf_len * (woody ? .26 : .46);
        cereal_blade(b, top, a + .48, len, c->p->leaf_w * .52,
                     len * .11, -fmin(len * .43, top[1] * .79), dry, 1);
    }
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

static void crop_coix_head(LpBuilder *b, CropCtx *c, const double at[3], double a) {
    double size = c->p->fruit_size;
    double x = cos(a), z = sin(a);
    LpRgb grain = c->t->fruit;
    if (c->p->produce == CROP_COIX) {
        double knee[3] = {at[0] + x * size * .35, at[1] + size * .66, at[2] + z * size * .35};
        double end[3] = {at[0] + x * size * 1.19, at[1] + size * .22, at[2] + z * size * 1.19};
        crop_stem_seg(b, at, knee, size * .030, .72, 3, grain);
        crop_stem_seg(b, knee, end, size * .022, .53, 3, grain);
        int branches = 4;
        for (int i = 0; i < branches; i++) {
            double t = (i + .4) / branches;
            double side = (i % 2 ? -1 : 1) * .43;
            double base[3] = {knee[0] + (end[0] - knee[0]) * t, knee[1] + (end[1] - knee[1]) * t,
                              knee[2] + (end[2] - knee[2]) * t};
            double tip[3] = {base[0] - z * size * side, base[1] - size * .22, base[2] + x * size * side};
            crop_stem_seg(b, base, tip, size * .015, .44, 3, grain);
            for (int j = 1; j <= 2; j++) {
                double u = j * .45;
                double seed[3] = {base[0] + (tip[0] - base[0]) * u, base[1] + (tip[1] - base[1]) * u,
                                  base[2] + (tip[2] - base[2]) * u};
                double radius = size * .10;
                LpRgb color = j == 2 ? qlcg_lp_lighten(grain, .12) : grain;
                crop_body(b, seed, radius, size * .12, radius, color, LP_MAT_FRUIT);
            }
        }
    }
}

static void build_crop_coix(LpBuilder *b, CropCtx *c, int simple) {
    int coix = c->p->produce == CROP_COIX;
    int stubble = c->def->state == PD_STATE_NORMAL &&
                  ((!coix && c->season == PD_SEASON_AUTUMN) || c->season == PD_SEASON_WINTER);
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
            crop_coix_head(b, c, tip, a);
        } else if (c->def->state == PD_STATE_FLOWERING || (coix && c->s->bloom)) {
            crop_body(b, tip, c->p->fruit_size * .12, c->p->fruit_size * .34, c->p->fruit_size * .12,
                      c->t->bloom, LP_MAT_BLOOM);
        }
    }
}

/* 幼苗状态构建 */
static void build_coix_seedling(LpBuilder *b, CropCtx *c) {
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
static void build_coix_withered(LpBuilder *b, CropCtx *c) {
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

    int simple = form == VEG_FORM_SIMPLE;
    if (p->produce == CROP_RICE || p->produce == CROP_WHEAT) {
        if (p->produce == CROP_RICE) build_crop_rice(b, &c, simple);
        else build_crop_wheat(b, &c, simple);
        crop_add_sockets(b, &c);
        return;
    }
    if (p->produce == CROP_COIX) {
        if (!simple && def->state == PD_STATE_WITHERED)
            build_coix_withered(b, &c);
        else if (!simple && def->state == PD_STATE_SEEDLING)
            build_coix_seedling(b, &c);
        else {
            if (def->state == PD_STATE_SUBADULT) { c.h *= .78; c.sp *= .75; }
            build_crop_coix(b, &c, simple);
        }
        crop_add_sockets(b, &c);
        return;
    }
    if (def->state == PD_STATE_SEEDLING) {
        build_crop_sprout(b, &c);
        return;
    }
    if (def->state == PD_STATE_WITHERED) {
        build_crop_wilt(b, &c);
        return;
    }
    CropProfile younger = *p;
    if (def->state == PD_STATE_SUBADULT) {
        c.h *= .78;
        c.sp *= .75;
        younger.stem_r *= .76;
        younger.leaf_len *= .68;
        younger.leaf_w *= .73;
        c.p = &younger;
    }

    switch (p->habit) {
    case CROP_CANE:       build_crop_cane(b, &c, simple); break;
    case CROP_BRASSICA:   build_crop_brassica(b, &c, simple); break;
    case CROP_FIBER_HERB: build_crop_fiber(b, &c, simple); break;
    case CROP_BOLL_SHRUB: build_crop_cotton(b, &c, simple); break;
    case CROP_GRAIN:      build_crop_millet(b, &c, simple); break;
    case CROP_STEM_HERB:  build_crop_herb(b, &c, simple); break;
    case CROP_ALLIUM:    build_crop_allium(b, &c, simple); break;
    case CROP_ROSETTE:   build_crop_rosette(b, &c, simple); break;
    case CROP_FLOATING:  build_crop_floating(b, &c, simple); break;
    case CROP_WOODY:     build_crop_woody(b, &c, simple); break;
    }
    crop_add_sockets(b, &c);
}
