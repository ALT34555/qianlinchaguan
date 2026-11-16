/* shu.luoye */

#include "shu_model.h"
#include "../../plant_forms.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const ShuShape shapes[] = {
    {1.0, 1.0, 1.0, 0},
    {1.17, 0.78, 1.13, -1},
    {0.88, 1.24, 0.86, 1},
};

/* 色板取色回退 */
static LpRgb pick_color(const PdDict *pal, const char *k1, const char *k2, const char *fallback) {
    const char *v = k1 ? pd_str(pal, k1, NULL) : NULL;
    if (!v && k2) v = pd_str(pal, k2, NULL);
    if (!v) v = fallback;
    return qlcg_lp_hex(v, 255, 0, 255);
}

/* 浮点参数提取回退 */
static double num_or(const PdDict *d, const char *key, double dflt) {
    double v = pd_num(d, key, NAN);
    return isnan(v) ? dflt : v;
}

static ShuSeason season_growth(const PdDef *def, int season) {
    return qlcg_pa_shu_season(def, season, 0, 0, 3);
}

/* 简模高裸干轻巧疏冠 */
static void build_simple(LpBuilder *b, const PdDef *def, int season) {
    const PdDict *p = &def->params;
    double h = fmax(0.1, num_or(p, "height", 16.0));
    double r = fmax(0.05, num_or(p, "canopy", h * 0.22)) * season_growth(def, season).size;
    LpRgb leaf = qlcg_lp_wind_color(pick_color(&def->palette, "foliage", "larch", "#5d8a44"), LP_WIND_LEAF);
    LpRgb bark = qlcg_lp_wind_color(pick_color(&def->palette, "bark", "barkLight", "#725338"), LP_WIND_BARK);

    /* 高耸光洁裸干 */
    double c1[3] = { 0, 0, 0 };
    LpOpts oc1 = qlcg_lp_opts(); oc1.radius_ratio = 0.55;
    qlcg_lp_cone(b, c1, h * 0.032, h * 0.032, h * 0.54, 5, bark, &oc1);

    if (qlcg_pa_has_snow_target(def, "canopy") || qlcg_pa_has_snow_target(def, "top")) {
        double cs[3] = { 0, h * 0.88, 0 };
        qlcg_lp_socket(b, cs, LP_SOCKET_SNOW_CANOPY);
    }

    /* 冬季落叶后保留枝干 */
    if (season == PD_SEASON_WINTER) {
        double cb[3] = { 0, h * 0.46, 0 };
        LpOpts ocb = qlcg_lp_opts(); ocb.radius_ratio = 0.25;
        qlcg_lp_cone(b, cb, h * 0.017, h * 0.017, h * 0.44, 4, bark, &ocb);
        return;
    }

    /* 春夏秋轻盈卵形冠 */
    double c2[3] = { 0, h * 0.46, 0 };
    LpOpts oc = qlcg_lp_opts(); oc.floor_on = 1.0; oc.squash = 0.35;
    qlcg_lp_dome(b, c2, r, h * 0.48, r, 5, leaf, &oc);
}

/* 成年态高裸干疏朗轻羽冠 */
static void build_adult(LpBuilder *b, const PdDef *def, int season) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    ShuSeason growth = season_growth(def, season);
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 16.0);
    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.44);
    height *= shape->height;
    trunk *= shape->trunk;
    LpRgb bark = qlcg_pa_tone(def, "bark", 1.0);
    LpRgb bark_light = qlcg_pa_tone(def, "barkWarm", 1.0);
    double trunk_r = qlcg_pa_pnum(def, "trunkRadius", fmax(0.18, height * 0.032));
    double canopy = qlcg_pa_pnum(def, "canopy", height * 0.22);
    double base_radius = qlcg_pa_pnum(def, "layerRadius", canopy);
    base_radius *= shape->crown;
    double span = height - trunk;
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 5.0), 4.0, 7.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.16);
    int is_winter = (season == PD_SEASON_WINTER);

    /* 锥化挺拔裸露主干 */
    double top_y = trunk + span * 0.84;
    double st_h = span * 0.18;
    double p0[3] = { 0, 0, 0 };
    LpOpts ot0 = qlcg_lp_opts();
    ot0.jitter = jitter * 0.4; ot0.rand = rand; ot0.radius_ratio = 0.72;
    qlcg_lp_cone(b, p0, trunk_r * 1.25, trunk_r * 1.25, trunk * 0.45, sides, bark, &ot0);

    double p1[3] = { 0, trunk * 0.38, 0 };
    LpOpts ot1 = qlcg_lp_opts();
    ot1.jitter = jitter * 0.4; ot1.rand = rand; ot1.radius_ratio = 0.70;
    qlcg_lp_cone(b, p1, trunk_r * 0.88, trunk_r * 0.88, trunk * 0.68, sides, bark, &ot1);

    /* 树冠延伸锥化中轴 */
    double p2_top = is_winter ? (trunk + span * 0.88) : (top_y + st_h * 0.25);
    double p2[3] = { 0, trunk * 0.96, 0 };
    LpOpts ot2 = qlcg_lp_opts();
    ot2.jitter = jitter * 0.2; ot2.rand = rand; ot2.radius_ratio = 0.16;
    qlcg_lp_cone(b, p2, trunk_r * 0.60, trunk_r * 0.60, p2_top - p2[1], sides, bark, &ot2);

    /* 中上部斜展大骨干分枝 */
    int limb_count = 5 + shape->branches;
    double tips[6][3];
    for (int k = 0; k < limb_count; k++) {
        double a = ((double)k / (double)limb_count) * TAU + qlcg_lp_rng_next(rand) * 0.6;
        double up = 0.45 + qlcg_lp_rng_next(rand) * 0.35;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double dlen = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        double ly = trunk + span * (0.12 + (double)k * 0.13) + (qlcg_lp_rng_next(rand) - 0.5) * 0.3;
        double start[3] = { 0, ly, 0 };
        double llen = base_radius * (0.85 + qlcg_lp_rng_next(rand) * 0.35) * (1.0 - (double)k * 0.12);
        double rad = trunk_r * 0.38;
        LpOpts ol = qlcg_lp_opts(); ol.sides = 4; ol.tip_ratio = 0.42; ol.cap = 1.0;
        qlcg_lp_limb(b, start, dir, llen, rad, bark_light, &ol);
        tips[k][0] = start[0] + dir[0] / dlen * llen;
        tips[k][1] = start[1] + dir[1] / dlen * llen;
        tips[k][2] = start[2] + dir[2] / dlen * llen;

        /* 冬季落叶期次级光秃枝桠 */
        if (is_winter) {
            double a2 = a + (qlcg_lp_rng_next(rand) - 0.5) * 0.9;
            double up2 = up * 0.6 - 0.15;
            double dir2[3] = { cos(a2) * cos(up2), sin(up2), sin(a2) * cos(up2) };
            double s2[3] = {
                tips[k][0] - dir[0] / dlen * rad * 0.55,
                tips[k][1] - dir[1] / dlen * rad * 0.55,
                tips[k][2] - dir[2] / dlen * rad * 0.55
            };
            LpOpts ol2 = qlcg_lp_opts(); ol2.sides = 3; ol2.tip_ratio = 0.40; ol2.cap = 1.0;
            qlcg_lp_limb(b, s2, dir2, llen * 0.5, trunk_r * 0.18, bark, &ol2);
        }
    }

    /* 冬季落叶形态与枝头落雪 */
    if (is_winter) {
        if (qlcg_pa_has_snow_target(def, "canopy")) {
            for (int k = 0; k < limb_count; k++) {
                double sc[3] = { tips[k][0], tips[k][1] + 0.08, tips[k][2] };
                qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
            }
            double tc[3] = { 0, trunk + span * 0.88, 0 };
            qlcg_lp_socket(b, tc, LP_SOCKET_SNOW_CANOPY);
        }
        return;
    }

    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1.0);

    /* 骨干枝端嵌合叶丛团 */
    double tip_r[6] = { 0 };
    for (int k = 0; k < limb_count; k++) {
        double *tip = tips[k];
        double cr = (base_radius * 0.42 * (0.85 + qlcg_lp_rng_next(rand) * 0.3)) * growth.size;
        tip_r[k] = cr;
        double dc[3] = { tip[0], tip[1] - cr * 0.08, tip[2] };
        LpOpts od = qlcg_lp_opts();
        od.jitter = 0.2; od.rand = rand; od.floor_on = 1.0; od.squash = 0.42;
        qlcg_lp_dome(b, dc, cr, cr * 0.65, cr, 5, (k % 2 == 0) ? fol : dark, &od);

        /* 羽毛状下垂小枝片 */
        int feathers = 3;
        for (int f = 0; f < feathers; f++) {
            double fa = (double)f * 2.1 + qlcg_lp_rng_next(rand) * 0.4;
            double fpos[3] = { tip[0] + cos(fa) * cr * 0.18, tip[1] - cr * 0.02, tip[2] + sin(fa) * cr * 0.18 };
            LpOpts of = qlcg_lp_opts();
            of.pitch = -0.38; of.cup = 0.35; of.rand = rand;
            qlcg_lp_petal(b, fpos, fa, cr * 1.05, cr * 0.42, fol, &of);
        }
    }

    /* 主轴中上部透气叶簇 */
    int central_puffs = 4;
    for (int c = 0; c < central_puffs; c++) {
        double ct = (double)c / (double)central_puffs;
        double cy = trunk + span * (0.35 + ct * 0.55);
        double cr = (base_radius * (0.65 - ct * 0.35) * (0.85 + qlcg_lp_rng_next(rand) * 0.3)) * growth.size;
        double a = (double)c * GOLDEN + (double)def->build_seed * 0.01;
        double cpos[3] = { cos(a) * cr * 0.22, cy, sin(a) * cr * 0.22 };
        LpOpts oc = qlcg_lp_opts();
        oc.jitter = 0.18; oc.rand = rand; oc.floor_on = 1.0; oc.squash = 0.42;
        qlcg_lp_dome(b, cpos, cr, cr * 0.72, cr, 5, (c % 2 == 0) ? fol : dark, &oc);
    }

    /* 梢头轻柔窄尖顶 */
    double st[3] = { 0, top_y, 0 };
    LpOpts os = qlcg_lp_opts();
    os.jitter = 0.10; os.rand = rand; os.cap = 1.0; os.radius_ratio = 0.0;
    qlcg_lp_cone(b, st, base_radius * 0.25, base_radius * 0.25, st_h, 4, fol, &os);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "canopy")) {
        double sc[3] = { 0, top_y + st_h * 0.45, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }

    /* 开花期叶丛雄球花 */
    int pollen = qlcg_pa_bloom_count_for(def, 0, 14);
    if (pollen > 0) {
        LpRgb pcol = qlcg_pa_tone(def, "pollen", 1.0);
        LpRgb twig_c = qlcg_pa_deepen(fol, 0.2);
        for (int k = 0; k < pollen; k++) {
            int bi = k % limb_count;
            double *tip = tips[bi];
            double cr = tip_r[bi];
            double a = (double)k * GOLDEN + 0.33;
            double dlen = sqrt(0.85 * 0.85 + 0.38 * 0.38);
            double dir[3] = { cos(a) * 0.85 / dlen, 0.38 / dlen, sin(a) * 0.85 / dlen };
            double plen = cr * 0.72;
            double gp[3] = {
                tip[0] + dir[0] * plen,
                tip[1] + dir[1] * plen,
                tip[2] + dir[2] * plen
            };
            LpOpts ol = qlcg_lp_opts();
            ol.sides = 3; ol.tip_ratio = 0.65; ol.cap = 1.0;
            qlcg_lp_limb(b, tip, dir, plen, 0.020, twig_c, &ol);
            double sz[3] = { 0.08, 0.10, 0.08 };
            LpOpts op = qlcg_lp_opts();
            op.mat = LP_MAT_BLOOM; op.rand = rand;
            qlcg_lp_ico(b, gp, sz, 3, pcol, &op);
        }
    }
}

/* 幼苗期细长落叶松苗 */
static void build_seedling(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double species_h = qlcg_pa_pnum(def, "height", 0.5);
    double height = fmin(species_h, fmax(0.08, species_h * 0.42));
    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.52);
    if (trunk > height * 0.62) trunk = height * 0.62;
    LpRgb stem = qlcg_pa_tone(def, "barkLight", 1.0);
    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    LpRgb fol_dark = qlcg_pa_tone(def, "foliageDark", 1.0);
    double leaf_r = qlcg_pa_pnum(def, "canopy", height * 0.3);
    if (leaf_r > height * 0.4) leaf_r = height * 0.4;
    height *= shape->height;
    trunk *= shape->trunk;
    leaf_r *= shape->crown;

    /* 细幼直茎 */
    double stem_r = fmax(0.012, height * 0.05);
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.jitter = 0.1; o0.radius_ratio = 0.35;
    qlcg_lp_cone(b, p0, stem_r * 1.25, stem_r * 1.25, trunk + leaf_r * 0.22, 4, stem, &o0);

    /* 柔韧松针子叶簇 */
    for (int i = 0; i < 2; i++) {
        double a = (double)i * PI + qlcg_lp_rng_next(rand) * 0.5;
        double pos[3] = { cos(a) * stem_r * 0.45, trunk * 0.52, sin(a) * stem_r * 0.45 };
        double plen = leaf_r * (0.95 + qlcg_lp_rng_next(rand) * 0.3);
        LpOpts o = qlcg_lp_opts();
        o.pitch = -0.06 + qlcg_lp_rng_next(rand) * 0.2; o.cup = 0.35; o.rand = rand;
        qlcg_lp_petal(b, pos, a, plen, leaf_r * 0.75, i ? fol : fol_dark, &o);
    }
    for (int i = 0; i < 4; i++) {
        double a = (double)i * GOLDEN + (double)def->build_seed * 0.0017 + qlcg_lp_rng_next(rand) * 0.4;
        double len = leaf_r * (0.7 + qlcg_lp_rng_next(rand) * 0.4);
        double pos[3] = { cos(a) * stem_r * 0.40, trunk * (0.62 + (double)i * 0.06), sin(a) * stem_r * 0.40 };
        LpOpts o = qlcg_lp_opts();
        o.pitch = 0.12 + qlcg_lp_rng_next(rand) * 0.3; o.cup = 0.4; o.rand = rand;
        qlcg_lp_petal(b, pos, a, len, len * 0.52, (i % 2) ? fol : fol_dark, &o);
    }
    double dc[3] = { 0, trunk + leaf_r * 0.24, 0 };
    LpOpts od = qlcg_lp_opts();
    od.jitter = 0.18; od.rand = rand; od.floor_on = 1.0; od.squash = 0.45;
    qlcg_lp_dome(b, dc, leaf_r * 0.5, leaf_r * 0.62, leaf_r * 0.5, 5, fol_dark, &od);

    /* 幼苗顶生针束 */
    int bud_blobs = qlcg_pa_js_round(qlcg_pa_pnum(def, "blobs", 2.0));
    if (bud_blobs > 2) bud_blobs = 2;
    int buds = 2 + (bud_blobs > 0 ? bud_blobs : 0);
    for (int i = 0; i < buds; i++) {
        double a = (double)i * GOLDEN + qlcg_lp_rng_next(rand);
        double len = leaf_r * (0.55 + qlcg_lp_rng_next(rand) * 0.35);
        double pos[3] = { 0, trunk + leaf_r * (0.34 + qlcg_lp_rng_next(rand) * 0.18), 0 };
        LpOpts o = qlcg_lp_opts();
        o.pitch = 0.45 + qlcg_lp_rng_next(rand) * 0.3; o.cup = 0.42; o.rand = rand;
        qlcg_lp_petal(b, pos, a, len, len * 0.55, (i % 2) ? fol : fol_dark, &o);
    }
    if (qlcg_pa_has_snow_target(def, "canopy")) {
        double sc[3] = { 0, trunk + leaf_r * 0.5, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }
}

/* 枯萎态干瘦裸枝桩 */
static void build_withered(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 15.0);
    height *= shape->height;
    LpRgb wood = qlcg_pa_tone(def, "witheredDark", 1.0);
    LpRgb stem = qlcg_pa_tone(def, "withered", 1.0);
    LpRgb dry_leaf = qlcg_pa_tone(def, "witheredStem", 1.0);
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 5.0), 3.0, 8.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.2);

    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.42) * 0.6;
    double trunk_r = fmax(0.08, qlcg_pa_pnum(def, "trunkRadius", height * 0.032));
    int limbs = 4;

    /* 枯立主干高桩 */
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.jitter = jitter * 0.4; o0.rand = rand; o0.radius_ratio = 0.72;
    qlcg_lp_cone(b, p0, trunk_r * 1.3, trunk_r * 1.3, trunk * 0.36, sides, wood, &o0);
    double p1[3] = { 0, trunk * 0.28, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.jitter = jitter * 0.5; o1.rand = rand; o1.radius_ratio = 0.36;
    qlcg_lp_cone(b, p1, trunk_r * 0.92, trunk_r * 0.92, trunk * 0.72, sides, stem, &o1);

    /* 稀疏枯硬斜枝 */
    double tips[4][3];
    for (int i = 0; i < limbs; i++) {
        double a = ((double)i / (double)limbs) * TAU + qlcg_lp_rng_next(rand) * 0.8;
        double up = 0.25 + qlcg_lp_rng_next(rand) * 0.4;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double dlen = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        double y = trunk * (0.35 + qlcg_lp_rng_next(rand) * 0.5);
        double len = height * (0.2 + qlcg_lp_rng_next(rand) * 0.18);
        double start[3] = { 0, y, 0 };
        double rad = trunk_r * 0.32;
        LpOpts ol = qlcg_lp_opts(); ol.sides = 4; ol.tip_ratio = 0.42; ol.cap = 1.0;
        qlcg_lp_limb(b, start, dir, len, rad, stem, &ol);
        tips[i][0] = start[0] + dir[0] / dlen * len;
        tips[i][1] = start[1] + dir[1] / dlen * len;
        tips[i][2] = start[2] + dir[2] / dlen * len;
        if (qlcg_lp_rng_next(rand) < 0.65) {
            double a2 = a + (qlcg_lp_rng_next(rand) - 0.5) * 1.2;
            double up2 = up * 0.65 - 0.1;
            double dir2[3] = { cos(a2) * cos(up2), sin(up2), sin(a2) * cos(up2) };
            double len2 = len * 0.42;
            double s2[3] = {
                tips[i][0] - dir[0] / dlen * rad * 0.6,
                tips[i][1] - dir[1] / dlen * rad * 0.6,
                tips[i][2] - dir[2] / dlen * rad * 0.6
            };
            LpOpts ol2 = qlcg_lp_opts(); ol2.sides = 3; ol2.tip_ratio = 0.40; ol2.cap = 1.0;
            qlcg_lp_limb(b, s2, dir2, len2, trunk_r * 0.2, wood, &ol2);
        }
    }

    /* 少数残存枯死细片 */
    for (int i = 0; i < 3; i++) {
        int li = i % limbs;
        double a = (double)i * GOLDEN + (double)def->build_seed * 0.0017;
        double len = fmin(height * 0.08, 0.45);
        double bearing = a + (qlcg_lp_rng_next(rand) - 0.5) * 0.8;
        LpRgb col = qlcg_pa_deepen(dry_leaf, qlcg_lp_rng_next(rand) * 0.3);
        LpOpts o = qlcg_lp_opts();
        o.pitch = -0.25; o.cup = 0.5; o.rand = rand;
        qlcg_lp_petal(b, tips[li], bearing, len, len * 0.5, col, &o);
    }
}

/* 落叶松物种构建主入口 */
void qlcg_plant_shu_luoye(LpBuilder *b, const PdDef *def, int season, int form) {
    if (form == VEG_FORM_SIMPLE) {
        build_simple(b, def, season);
        return;
    }
    switch (def->state) {
        case PD_STATE_SEEDLING: build_seedling(b, def); break;
        case PD_STATE_WITHERED: build_withered(b, def); break;
        default: build_adult(b, def, season); break;
    }
}
