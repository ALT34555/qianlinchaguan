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
    qlcg_lp_prism(b, c1, h * 0.03, h * 0.03, 0, h * 0.46, 5, bark, NULL);

    if (qlcg_pa_has_snow_target(def, "canopy") || qlcg_pa_has_snow_target(def, "top")) {
        double cs[3] = { 0, h * 0.88, 0 };
        qlcg_lp_socket(b, cs, LP_SOCKET_SNOW_CANOPY);
    }

    /* 冬季落叶后保留枝干 */
    if (season == PD_SEASON_WINTER) {
        double cb[3] = { 0, h * 0.45, 0 };
        qlcg_lp_prism(b, cb, h * 0.02, h * 0.02, 0, h * 0.45, 4, bark, NULL);
        return;
    }

    /* 春夏秋轻盈卵形冠 */
    double c2[3] = { 0, h * 0.46, 0 };
    LpOpts oc = qlcg_lp_opts(); oc.floor_on = 0;
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

    /* 高大挺拔裸露主干 */
    double p0[3] = { 0, 0, 0 };
    LpOpts ot0 = qlcg_lp_opts();
    ot0.jitter = jitter * 0.4; ot0.rand = rand; ot0.cap = 0;
    qlcg_lp_prism(b, p0, trunk_r * 1.25, trunk_r * 1.25, 0, trunk * 0.45, sides, bark, &ot0);

    double p1[3] = { 0, trunk * 0.42, 0 };
    LpOpts ot1 = qlcg_lp_opts();
    ot1.jitter = jitter * 0.5; ot1.rand = rand; ot1.cap = 0;
    qlcg_lp_prism(b, p1, trunk_r * 0.9, trunk_r * 0.9, 0, trunk * 0.58, sides, bark, &ot1);

    /* 树冠内延伸中轴主干 */
    double p2[3] = { 0, trunk, 0 };
    LpOpts ot2 = qlcg_lp_opts();
    ot2.jitter = jitter * 0.3; ot2.rand = rand; ot2.cap = 0;
    qlcg_lp_prism(b, p2, trunk_r * 0.65, trunk_r * 0.65, 0, span * 0.85, sides, bark, &ot2);

    /* 中上部斜展大骨干分枝 */
    int limb_count = 5 + shape->branches;
    double tips[6][3];
    for (int k = 0; k < limb_count; k++) {
        double a = ((double)k / (double)limb_count) * TAU + qlcg_lp_rng_next(rand) * 0.6;
        double up = 0.45 + qlcg_lp_rng_next(rand) * 0.35;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double ly = trunk + span * (0.12 + (double)k * 0.14) + (qlcg_lp_rng_next(rand) - 0.5) * 0.5;
        double start[3] = { 0, ly, 0 };
        double llen = base_radius * (0.85 + qlcg_lp_rng_next(rand) * 0.35) * (1.0 - (double)k * 0.12);
        qlcg_pa_limb_to(b, start, dir, llen, trunk_r * 0.38, bark_light, tips[k]);

        /* 冬季落叶期次级光秃枝桠 */
        if (is_winter) {
            double a2 = a + (qlcg_lp_rng_next(rand) - 0.5) * 0.9;
            double up2 = up * 0.6 - 0.15;
            double dir2[3] = { cos(a2) * cos(up2), sin(up2), sin(a2) * cos(up2) };
            qlcg_pa_limb_to(b, tips[k], dir2, llen * 0.5, trunk_r * 0.18, bark, NULL);
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

    /* 骨干枝端羽状叶丛团 */
    for (int k = 0; k < limb_count; k++) {
        double *tip = tips[k];
        double cr = (base_radius * 0.42 * (0.85 + qlcg_lp_rng_next(rand) * 0.3)) * growth.size;
        double dc[3] = { tip[0], tip[1] + cr * 0.25, tip[2] };
        LpOpts od = qlcg_lp_opts();
        od.jitter = 0.2; od.rand = rand; od.floor_on = 0;
        qlcg_lp_dome(b, dc, cr, cr * 0.65, cr, 5, (k % 2 == 0) ? fol : dark, &od);

        /* 羽毛状下垂小枝片 */
        int feathers = 3;
        for (int f = 0; f < feathers; f++) {
            double fa = (double)f * 2.1 + qlcg_lp_rng_next(rand) * 0.4;
            double fpos[3] = { tip[0] + cos(fa) * cr * 0.6, tip[1], tip[2] + sin(fa) * cr * 0.6 };
            LpOpts of = qlcg_lp_opts();
            of.pitch = -0.38; of.cup = 0.35; of.rand = rand;
            qlcg_lp_petal(b, fpos, fa, cr * 0.85, cr * 0.45, fol, &of);
        }
    }

    /* 主轴中上部透气叶簇 */
    int central_puffs = 4;
    for (int c = 0; c < central_puffs; c++) {
        double ct = (double)c / (double)central_puffs;
        double cy = trunk + span * (0.35 + ct * 0.55);
        double cr = (base_radius * (0.65 - ct * 0.35) * (0.85 + qlcg_lp_rng_next(rand) * 0.3)) * growth.size;
        double a = (double)c * GOLDEN + (double)def->build_seed * 0.01;
        double cpos[3] = { cos(a) * cr * 0.25, cy, sin(a) * cr * 0.25 };
        LpOpts oc = qlcg_lp_opts();
        oc.jitter = 0.18; oc.rand = rand; oc.floor_on = 0;
        qlcg_lp_dome(b, cpos, cr, cr * 0.72, cr, 5, (c % 2 == 0) ? fol : dark, &oc);
    }

    /* 梢头轻柔窄尖顶 */
    double st[3] = { 0, trunk + span * 0.88, 0 };
    LpOpts os = qlcg_lp_opts();
    os.jitter = 0.12; os.rand = rand;
    qlcg_lp_cone(b, st, base_radius * 0.25, base_radius * 0.25, span * 0.14, 4, fol, &os);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "canopy")) {
        double sc[3] = { 0, trunk + span * 0.92, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }

    /* 开花期花粉飘散 */
    int pollen = qlcg_pa_bloom_count_for(def, 0, 14);
    if (pollen > 0) qlcg_pa_pollen_grains(b, def, pollen, base_radius, trunk, trunk + span, rand);
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
    o0.rand = rand; o0.jitter = 0.1; o0.cap = 0;
    qlcg_lp_prism(b, p0, stem_r * 1.3, stem_r * 1.3, 0, trunk * 0.5, 4, stem, &o0);
    double p1[3] = { 0, trunk * 0.46, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.rand = rand; o1.jitter = 0.12; o1.cap = 0;
    qlcg_lp_prism(b, p1, stem_r, stem_r, 0, trunk * 0.56, 4, stem, &o1);

    /* 柔韧松针子叶簇 */
    for (int i = 0; i < 2; i++) {
        double a = (double)i * PI + qlcg_lp_rng_next(rand) * 0.5;
        double pos[3] = { cos(a) * leaf_r * 0.3, trunk * 0.5, sin(a) * leaf_r * 0.3 };
        double plen = leaf_r * (0.95 + qlcg_lp_rng_next(rand) * 0.3);
        LpOpts o = qlcg_lp_opts();
        o.pitch = -0.06 + qlcg_lp_rng_next(rand) * 0.2; o.cup = 0.35; o.rand = rand;
        qlcg_lp_petal(b, pos, a, plen, leaf_r * 0.75, i ? fol : fol_dark, &o);
    }
    for (int i = 0; i < 4; i++) {
        double a = (double)i * GOLDEN + (double)def->build_seed * 0.0017 + qlcg_lp_rng_next(rand) * 0.4;
        double len = leaf_r * (0.7 + qlcg_lp_rng_next(rand) * 0.4);
        double pos[3] = { cos(a) * leaf_r * 0.22, trunk * (0.62 + (double)i * 0.06), sin(a) * leaf_r * 0.22 };
        LpOpts o = qlcg_lp_opts();
        o.pitch = 0.12 + qlcg_lp_rng_next(rand) * 0.3; o.cup = 0.4; o.rand = rand;
        qlcg_lp_petal(b, pos, a, len, len * 0.52, (i % 2) ? fol : fol_dark, &o);
    }
    double dc[3] = { 0, trunk + leaf_r * 0.3, 0 };
    LpOpts od = qlcg_lp_opts();
    od.jitter = 0.22; od.rand = rand;
    qlcg_lp_dome(b, dc, leaf_r * 0.5, leaf_r * 0.62, leaf_r * 0.5, 5, fol_dark, &od);

    /* 幼苗顶生针束 */
    int bud_blobs = qlcg_pa_js_round(qlcg_pa_pnum(def, "blobs", 2.0));
    if (bud_blobs > 2) bud_blobs = 2;
    int buds = 2 + (bud_blobs > 0 ? bud_blobs : 0);
    for (int i = 0; i < buds; i++) {
        double a = (double)i * GOLDEN + qlcg_lp_rng_next(rand);
        double len = leaf_r * (0.55 + qlcg_lp_rng_next(rand) * 0.35);
        double pos[3] = { 0, trunk + leaf_r * (0.42 + qlcg_lp_rng_next(rand) * 0.2), 0 };
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
    o0.jitter = jitter * 0.5; o0.rand = rand; o0.cap = 0;
    qlcg_lp_prism(b, p0, trunk_r * 1.3, trunk_r * 1.3, 0, trunk * 0.35, sides, wood, &o0);
    double p1[3] = { 0, trunk * 0.25, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.jitter = jitter; o1.rand = rand; o1.cap = 0;
    qlcg_lp_prism(b, p1, trunk_r * 0.95, trunk_r * 0.95, 0, trunk * 0.7, sides, stem, &o1);

    /* 稀疏枯硬斜枝 */
    for (int i = 0; i < limbs; i++) {
        double a = ((double)i / (double)limbs) * TAU + qlcg_lp_rng_next(rand) * 0.8;
        double up = 0.25 + qlcg_lp_rng_next(rand) * 0.4;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double y = trunk * (0.35 + qlcg_lp_rng_next(rand) * 0.5);
        double len = height * (0.2 + qlcg_lp_rng_next(rand) * 0.18);
        double start[3] = { 0, y, 0 };
        double rad = trunk_r * 0.32;
        double tip[3];
        qlcg_pa_limb_to(b, start, dir, len, rad, stem, tip);
        if (qlcg_lp_rng_next(rand) < 0.65) {
            double a2 = a + (qlcg_lp_rng_next(rand) - 0.5) * 1.2;
            double up2 = up * 0.65 - 0.1;
            double dir2[3] = { cos(a2) * cos(up2), sin(up2), sin(a2) * cos(up2) };
            double len2 = len * 0.42;
            qlcg_pa_limb_to(b, tip, dir2, len2, trunk_r * 0.2, wood, NULL);
        }
    }

    /* 少数残存枯死细片 */
    for (int i = 0; i < 3; i++) {
        double a = (double)i * GOLDEN + (double)def->build_seed * 0.0017;
        double rad = fmin(height * 0.14, trunk_r * 2.8);
        double len = fmin(height * 0.08, 0.45);
        double pos[3] = { cos(a) * rad, trunk * (0.6 + qlcg_lp_rng_next(rand) * 0.4), sin(a) * rad };
        double bearing = a + (qlcg_lp_rng_next(rand) - 0.5) * 0.8;
        LpRgb col = qlcg_pa_deepen(dry_leaf, qlcg_lp_rng_next(rand) * 0.3);
        LpOpts o = qlcg_lp_opts();
        o.pitch = -0.25; o.cup = 0.5; o.rand = rand;
        qlcg_lp_petal(b, pos, bearing, len, len * 0.5, col, &o);
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
