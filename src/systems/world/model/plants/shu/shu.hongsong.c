/* shu.hongsong */

#include "shu_model.h"
#include "../../plant_forms.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const ShuShape shapes[] = {
    {1.0, 1.0, 1.0, 0},
    {1.16, 0.81, 1.17, -1},
    {0.88, 1.28, 0.85, 1},
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
    return qlcg_pa_shu_season(def, season, 0.89, 0, 3);
}

/* 简模开阔平顶松伞 */
static void build_simple(LpBuilder *b, const PdDef *def, int season) {
    const PdDict *p = &def->params;
    double h = fmax(0.1, num_or(p, "height", 18.0));
    double r = fmax(0.05, num_or(p, "canopy", h * 0.28)) * season_growth(def, season).size;
    LpRgb leaf = qlcg_lp_wind_color(pick_color(&def->palette, "foliage", "coniferDark", "#2b4a2d"), LP_WIND_LEAF);
    LpRgb bark = qlcg_lp_wind_color(pick_color(&def->palette, "bark", "barkWarm", "#7d5c3d"), LP_WIND_BARK);

    /* 粗干直立 */
    double c1[3] = { 0, 0, 0 };
    LpOpts oc1 = qlcg_lp_opts(); oc1.radius_ratio = 0.45;
    qlcg_lp_cone(b, c1, h * 0.046, h * 0.046, h * 0.60, 5, bark, &oc1);

    /* 平顶扁平伞冠 */
    double c2[3] = { 0, h * 0.54, 0 };
    LpOpts od = qlcg_lp_opts(); od.floor_on = 1.0; od.squash = 0.35;
    qlcg_lp_dome(b, c2, r, h * 0.25, r, 6, leaf, &od);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "canopy") || qlcg_pa_has_snow_target(def, "top")) {
        double cs[3] = { 0, h * 0.72, 0 };
        qlcg_lp_socket(b, cs, LP_SOCKET_SNOW_CANOPY);
    }
}

/* 成年态苍劲云盘伞状松冠 */
static void build_adult(LpBuilder *b, const PdDef *def, int season) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    ShuSeason growth = season_growth(def, season);
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 18.0);
    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.38);
    height *= shape->height;
    trunk *= shape->trunk;
    LpRgb bark = qlcg_pa_tone(def, "bark", 1.0);
    LpRgb bark_light = qlcg_pa_tone(def, "barkLight", 1.0);
    double trunk_r = qlcg_pa_pnum(def, "trunkRadius", fmax(0.25, height * 0.042));
    double canopy = qlcg_pa_pnum(def, "canopy", height * 0.28);
    double base_radius = qlcg_pa_pnum(def, "layerRadius", canopy);
    base_radius *= shape->crown;
    double span = height - trunk;
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 6.0), 5.0, 8.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.14);

    /* 粗大锥化苍劲主干 */
    double top_y = trunk + span * 0.78;
    double p0[3] = { 0, 0, 0 };
    LpOpts ot0 = qlcg_lp_opts();
    ot0.jitter = jitter * 0.4; ot0.rand = rand; ot0.radius_ratio = 0.74;
    qlcg_lp_cone(b, p0, trunk_r * 1.35, trunk_r * 1.35, trunk * 0.42, sides, bark, &ot0);
    double p1[3] = { 0, trunk * 0.34, 0 };
    LpOpts ot1 = qlcg_lp_opts();
    ot1.jitter = jitter * 0.4; ot1.rand = rand; ot1.radius_ratio = 0.70;
    qlcg_lp_cone(b, p1, trunk_r * 0.96, trunk_r * 0.96, trunk * 0.72, sides, bark, &ot1);

    /* 延伸至顶云内锥干 */
    double p2[3] = { 0, trunk * 0.96, 0 };
    LpOpts ot2 = qlcg_lp_opts();
    ot2.jitter = jitter * 0.25; ot2.rand = rand; ot2.radius_ratio = 0.28;
    qlcg_lp_cone(b, p2, trunk_r * 0.65, trunk_r * 0.65, top_y + span * 0.14 - p2[1], sides, bark, &ot2);

    /* 虬曲斜伸大骨干枝 */
    int boughs = 4 + shape->branches;
    double tips[5][3];
    for (int k = 0; k < boughs; k++) {
        double a = ((double)k / (double)boughs) * TAU + qlcg_lp_rng_next(rand) * 0.5;
        double up = 0.32 + qlcg_lp_rng_next(rand) * 0.35;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double dlen = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        double by = trunk + span * (0.15 + (double)k * 0.15);
        double start[3] = { 0, by, 0 };
        double blen = base_radius * (0.8 + qlcg_lp_rng_next(rand) * 0.3);
        LpOpts ol = qlcg_lp_opts(); ol.sides = 4; ol.tip_ratio = 0.44; ol.cap = 1.0;
        qlcg_lp_limb(b, start, dir, blen, trunk_r * 0.42, bark_light, &ol);
        tips[k][0] = start[0] + dir[0] / dlen * blen;
        tips[k][1] = start[1] + dir[1] / dlen * blen;
        tips[k][2] = start[2] + dir[2] / dlen * blen;
    }

    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1.0);

    /* 枝端嵌合松针云盘 */
    double bough_r[5] = { 0 };
    for (int k = 0; k < boughs; k++) {
        double *tip = tips[k];
        double cr = (base_radius * 0.48 * (0.85 + qlcg_lp_rng_next(rand) * 0.3)) * growth.size;
        bough_r[k] = cr;
        double dc[3] = { tip[0], tip[1] - cr * 0.06, tip[2] };
        LpOpts od = qlcg_lp_opts();
        od.jitter = 0.16; od.rand = rand; od.floor_on = 1.0; od.squash = 0.36;
        qlcg_lp_dome(b, dc, cr, cr * 0.42, cr, 6, (k % 2 == 0) ? fol : dark, &od);

        /* 雪片挂点 */
        if (qlcg_pa_has_snow_target(def, "canopy")) {
            double sc[3] = { tip[0], tip[1] + cr * 0.22, tip[2] };
            qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
        }
    }

    /* 树冠顶层开阔松云 */
    double top_r = base_radius * 0.72 * growth.size;
    double tc[3] = { 0, top_y, 0 };
    LpOpts otop = qlcg_lp_opts();
    otop.jitter = 0.15; otop.rand = rand; otop.floor_on = 1.0; otop.squash = 0.35;
    qlcg_lp_dome(b, tc, top_r, span * 0.25, top_r, 7, fol, &otop);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "top")) {
        double sc[3] = { 0, top_y + span * 0.16, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }

    /* 结实期松塔贴挂云盘 */
    if (def->fruit) {
        int fc = qlcg_pa_fruit_count_for(def, 6, 8);
        LpRgb cone_col = qlcg_pa_deepen(bark, 0.25);
        for (int k = 0; k < fc; k++) {
            int bi = k % boughs;
            double *tip = tips[bi];
            double cr = bough_r[bi];
            double a = (double)k * GOLDEN + qlcg_lp_rng_next(rand);
            double dlen = sqrt(0.88 * 0.88 + 0.25 * 0.25);
            double dir[3] = { cos(a) * 0.88 / dlen, -0.25 / dlen, sin(a) * 0.88 / dlen };
            double flen = cr * 0.72;
            double fp[3] = {
                tip[0] + dir[0] * flen,
                tip[1] + dir[1] * flen,
                tip[2] + dir[2] * flen
            };
            qlcg_lp_socket(b, fp, LP_SOCKET_FRUIT);
            if (def->state == PD_STATE_FRUITING) {
                LpOpts ol = qlcg_lp_opts();
                ol.sides = 3; ol.tip_ratio = 0.65; ol.cap = 1.0;
                qlcg_lp_limb(b, tip, dir, flen, 0.026, cone_col, &ol);
                double sz[3] = { 0.14, 0.24, 0.14 };
                LpOpts of = qlcg_lp_opts();
                of.mat = LP_MAT_FRUIT; of.rand = rand;
                of.rot_y = a + (double)k * 0.6;
                qlcg_lp_box(b, fp, sz, cone_col, &of);
            }
        }
    }

    /* 开花期云盘雄球花 */
    int pollen = qlcg_pa_bloom_count_for(def, 0, 16);
    if (pollen > 0) {
        LpRgb pcol = qlcg_pa_tone(def, "pollen", 1.0);
        LpRgb twig_c = qlcg_pa_deepen(fol, 0.2);
        for (int k = 0; k < pollen; k++) {
            int bi = k % (boughs + 1);
            double sp[3] = {
                (bi < boughs) ? tips[bi][0] : 0.0,
                (bi < boughs) ? tips[bi][1] : (top_y + span * 0.04),
                (bi < boughs) ? tips[bi][2] : 0.0
            };
            double cr = (bi < boughs) ? bough_r[bi] : top_r;
            double a = (double)k * GOLDEN + 0.35;
            double dlen = sqrt(0.82 * 0.82 + 0.45 * 0.45);
            double dir[3] = { cos(a) * 0.82 / dlen, 0.45 / dlen, sin(a) * 0.82 / dlen };
            double plen = cr * 0.68;
            double gp[3] = {
                sp[0] + dir[0] * plen,
                sp[1] + dir[1] * plen,
                sp[2] + dir[2] * plen
            };
            LpOpts ol = qlcg_lp_opts();
            ol.sides = 3; ol.tip_ratio = 0.65; ol.cap = 1.0;
            qlcg_lp_limb(b, sp, dir, plen, 0.022, twig_c, &ol);
            double sz[3] = { 0.09, 0.11, 0.09 };
            LpOpts op = qlcg_lp_opts();
            op.mat = LP_MAT_BLOOM; op.rand = rand;
            qlcg_lp_ico(b, gp, sz, 3, pcol, &op);
        }
    }
}

/* 幼苗期红松雏苗 */
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

    /* 粗短幼茎 */
    double stem_r = fmax(0.014, height * 0.055);
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.jitter = 0.1; o0.radius_ratio = 0.35;
    qlcg_lp_cone(b, p0, stem_r * 1.25, stem_r * 1.25, trunk + leaf_r * 0.22, 4, stem, &o0);

    /* 展生初叶束 */
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

    /* 顶端聚生松针簇 */
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

/* 枯萎态苍劲曲枝枯木 */
static void build_withered(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 16.0);
    height *= shape->height;
    LpRgb wood = qlcg_pa_tone(def, "witheredDark", 1.0);
    LpRgb stem = qlcg_pa_tone(def, "withered", 1.0);
    LpRgb dry_leaf = qlcg_pa_tone(def, "witheredStem", 1.0);
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 5.0), 3.0, 8.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.2);

    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.38) * 0.55;
    double trunk_r = fmax(0.1, qlcg_pa_pnum(def, "trunkRadius", height * 0.04));
    int limbs = 4;

    /* 粗大枯立主干 */
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.jitter = jitter * 0.4; o0.rand = rand; o0.radius_ratio = 0.72;
    qlcg_lp_cone(b, p0, trunk_r * 1.4, trunk_r * 1.4, trunk * 0.36, sides, wood, &o0);
    double p1[3] = { 0, trunk * 0.28, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.jitter = jitter * 0.5; o1.rand = rand; o1.radius_ratio = 0.38;
    qlcg_lp_cone(b, p1, trunk_r * 0.96, trunk_r * 0.96, trunk * 0.68, sides, stem, &o1);

    /* 枯萎横展虬枝 */
    double tips[4][3];
    for (int i = 0; i < limbs; i++) {
        double a = ((double)i / (double)limbs) * TAU + qlcg_lp_rng_next(rand) * 0.8;
        double up = 0.18 + qlcg_lp_rng_next(rand) * 0.35;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double dlen = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        double y = trunk * (0.35 + qlcg_lp_rng_next(rand) * 0.5);
        double len = height * (0.22 + qlcg_lp_rng_next(rand) * 0.2);
        double start[3] = { 0, y, 0 };
        double rad = trunk_r * 0.38;
        LpOpts ol = qlcg_lp_opts(); ol.sides = 4; ol.tip_ratio = 0.42; ol.cap = 1.0;
        qlcg_lp_limb(b, start, dir, len, rad, stem, &ol);
        tips[i][0] = start[0] + dir[0] / dlen * len;
        tips[i][1] = start[1] + dir[1] / dlen * len;
        tips[i][2] = start[2] + dir[2] / dlen * len;
        if (qlcg_lp_rng_next(rand) < 0.65) {
            double a2 = a + (qlcg_lp_rng_next(rand) - 0.5) * 1.2;
            double up2 = up * 0.65 - 0.1;
            double dir2[3] = { cos(a2) * cos(up2), sin(up2), sin(a2) * cos(up2) };
            double len2 = len * 0.45;
            double s2[3] = {
                tips[i][0] - dir[0] / dlen * rad * 0.6,
                tips[i][1] - dir[1] / dlen * rad * 0.6,
                tips[i][2] - dir[2] / dlen * rad * 0.6
            };
            LpOpts ol2 = qlcg_lp_opts(); ol2.sides = 3; ol2.tip_ratio = 0.40; ol2.cap = 1.0;
            qlcg_lp_limb(b, s2, dir2, len2, trunk_r * 0.24, wood, &ol2);
        }
    }

    /* 枝头零星枯针叶 */
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

/* 红松物种构建主入口 */
void qlcg_plant_shu_hongsong(LpBuilder *b, const PdDef *def, int season, int form) {
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
