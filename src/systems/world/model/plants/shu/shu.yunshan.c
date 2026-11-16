/* shu.yunshan */

#include "shu_model.h"
#include "../../plant_forms.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const ShuShape shapes[] = {
    {1.0, 1.0, 1.0, 0},
    {1.16, 0.78, 1.10, 1},
    {0.87, 1.25, 0.88, -1},
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
    return qlcg_pa_shu_season(def, season, 0.92, 0, 4);
}

/* 简模三段下垂宝塔锥 */
static void build_simple(LpBuilder *b, const PdDef *def, int season) {
    const PdDict *p = &def->params;
    double h = fmax(0.1, num_or(p, "height", 15.0));
    double r = fmax(0.05, num_or(p, "canopy", h * 0.24)) * season_growth(def, season).size;
    LpRgb leaf = qlcg_lp_wind_color(pick_color(&def->palette, "foliage", "conifer", "#3a5f39"), LP_WIND_LEAF);
    LpRgb dark = qlcg_lp_wind_color(pick_color(&def->palette, "foliageDark", "coniferDark", "#2b4a2d"), LP_WIND_LEAF);
    LpRgb bark = qlcg_lp_wind_color(pick_color(&def->palette, "bark", "barkLight", "#725338"), LP_WIND_BARK);

    /* 树干基段 */
    double c1[3] = { 0, 0, 0 };
    LpOpts oc1 = qlcg_lp_opts(); oc1.radius_ratio = 0.25;
    qlcg_lp_cone(b, c1, h * 0.036, h * 0.036, h * 0.72, 5, bark, &oc1);

    /* 三段阶梯宝塔锥层 */
    for (int i = 0; i < 3; i++) {
        double y = h * (0.18 + (double)i * 0.22);
        double cr = r * (1.0 - (double)i * 0.25);
        double c2[3] = { 0, y, 0 };
        LpOpts oc = qlcg_lp_opts(); oc.cap = 1; oc.radius_ratio = (i == 2) ? 0.0 : 0.22;
        qlcg_lp_cone(b, c2, cr, cr, h * 0.30, 6, (i % 2) ? leaf : dark, &oc);
    }

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "canopy") || qlcg_pa_has_snow_target(def, "top")) {
        double c3[3] = { 0, h * 0.72, 0 };
        qlcg_lp_socket(b, c3, LP_SOCKET_SNOW_CANOPY);
    }
}

/* 成年态宝塔下垂轮生枝冠 */
static void build_adult(LpBuilder *b, const PdDef *def, int season) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    ShuSeason growth = season_growth(def, season);
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 15.0);
    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.18);
    height *= shape->height;
    trunk *= shape->trunk;
    LpRgb bark = qlcg_pa_tone(def, "bark", 1.0);
    double trunk_r = qlcg_pa_pnum(def, "trunkRadius", fmax(0.18, height * 0.032));
    int layers = qlcg_pa_js_round(qlcg_pa_pnum(def, "layers", 7.0)) + shape->branches;
    if (layers < 4) layers = 4;
    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1.0);
    double canopy = qlcg_pa_pnum(def, "canopy", height * 0.24);
    double base_radius = qlcg_pa_pnum(def, "layerRadius", canopy);
    base_radius *= shape->crown;
    double span = height - trunk;
    double step = span / (double)(layers + 1);
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 6.0), 5.0, 8.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.12);

    /* 锥化主干及顶梢 */
    double last_y = trunk + step * (double)layers * 0.88;
    double last_r = (base_radius * pow(0.24, 1.08) + 0.12) * growth.size;
    double last_lh = (step * 1.32) * (0.75 + 0.25 * growth.density);
    double last_base = last_y - last_lh * 0.16;
    double last_ch = last_lh * 0.86;
    double top_y = last_base + last_ch * 0.85;
    double st_h = fmax(step * 0.9, trunk + span * 1.05 - top_y);

    double p0[3] = { 0, 0, 0 };
    LpOpts ot0 = qlcg_lp_opts();
    ot0.jitter = jitter * 0.3; ot0.rand = rand; ot0.radius_ratio = 0.74;
    qlcg_lp_cone(b, p0, trunk_r * 1.35, trunk_r * 1.35, trunk * 0.42, sides, bark, &ot0);

    double mid_top = trunk + step * 0.85;
    double p1[3] = { 0, trunk * 0.35, 0 };
    LpOpts ot1 = qlcg_lp_opts();
    ot1.jitter = jitter * 0.25; ot1.rand = rand; ot1.radius_ratio = 0.54;
    qlcg_lp_cone(b, p1, trunk_r * 0.92, trunk_r * 0.92, mid_top - p1[1], sides, bark, &ot1);

    double p2_y = trunk + step * 0.65;
    double p2_r = fmin(trunk_r * 0.48, base_radius * 0.24);
    double p2_top_r = fmax(0.012, fmin(p2_r * 0.18, last_r * 0.05));
    double p2[3] = { 0, p2_y, 0 };
    LpOpts ot2 = qlcg_lp_opts();
    ot2.jitter = jitter * 0.15; ot2.rand = rand; ot2.radius_ratio = p2_top_r / p2_r;
    qlcg_lp_cone(b, p2, p2_r, p2_r, top_y + st_h * 0.25 - p2_y, sides, bark, &ot2);

    /* 逐层生成轮生宝塔层 */
    double lr_arr[16] = { 0 };
    double ly_arr[16] = { 0 };
    double lh_arr[16] = { 0 };
    for (int i = 1; i <= layers; i++) {
        double t = (double)i / (double)layers;
        double y = trunk + step * (double)i * 0.88;
        double r = (base_radius * pow(1.0 - t * 0.76, 1.08) + 0.12) * growth.size;
        double lh = (step * 1.32) * (.75 + .25 * growth.density);
        double rot = (double)i * 0.52 + (double)def->build_seed * 0.01;
        LpRgb c = qlcg_pa_deepen((i % 2 == 0) ? fol : dark, (1.0 - t) * 0.35);

        /* 叠合冠层主锥 */
        double y_base = y - lh * 0.16;
        double cone_h = lh * 0.86;
        double pc[3] = { 0, y_base, 0 };
        LpOpts oc = qlcg_lp_opts();
        oc.rot_y = rot; oc.jitter = jitter; oc.rand = rand;
        oc.radius_ratio = 0.22;
        qlcg_lp_cone(b, pc, r, r, cone_h, sides, c, &oc);
        if (i <= 16) { lr_arr[i - 1] = r; ly_arr[i - 1] = y_base; lh_arr[i - 1] = cone_h; }

        /* 边缘嵌入式下垂羽片 */
        if (i <= layers - 1) {
            int skirts = sides;
            for (int k = 0; k < skirts; k++) {
                double a = rot + ((double)k / (double)skirts) * TAU;
                double pos[3] = { cos(a) * r * 0.48, y_base + cone_h * 0.28, sin(a) * r * 0.48 };
                LpOpts op = qlcg_lp_opts();
                op.pitch = -0.05; op.cup = 0.28; op.rand = rand;
                qlcg_lp_petal(b, pos, a, r * 0.68, r * 0.22, c, &op);
            }
        }

        /* 雪片挂点 */
        if (qlcg_pa_has_snow_target(def, "canopy") && i >= layers - 4) {
            double sc[3] = { 0, y + lh * 0.22, 0 };
            qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
        }
    }

    /* 尖耸挺直宝塔尖梢 */
    double st[3] = { 0, top_y, 0 };
    LpOpts osp = qlcg_lp_opts();
    osp.jitter = 0.05; osp.rand = rand; osp.cap = 1.0; osp.radius_ratio = 0.0;
    qlcg_lp_cone(b, st, last_r * 0.42, last_r * 0.42, st_h, 5, fol, &osp);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "top")) {
        double sc[3] = { 0, top_y + st_h * 0.45, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }

    /* 结实期下垂球果 */
    if (def->fruit) {
        int fc = qlcg_pa_fruit_count_for(def, 6, 8);
        LpRgb cone_col = qlcg_pa_deepen(qlcg_pa_tone(def, "barkWarm", 1.0), 0.25);
        int cap_n = layers < 16 ? layers : 16;
        for (int k = 0; k < fc; k++) {
            int li = k % (cap_n > 1 ? cap_n - 1 : 1);
            double frac = 0.20 + qlcg_lp_rng_next(rand) * 0.28;
            double r_slope = lr_arr[li] * (1.0 - frac * 0.78);
            double a = (double)k * GOLDEN + qlcg_lp_rng_next(rand);
            double fy = ly_arr[li] + lh_arr[li] * frac;
            double ftw_s[3] = { cos(a) * r_slope * 0.36, fy, sin(a) * r_slope * 0.36 };
            double ftw_dir[3] = { cos(a), -0.05, sin(a) };
            double ftw_len = r_slope * 0.76;
            double fp[3] = {
                ftw_s[0] + ftw_dir[0] * ftw_len,
                ftw_s[1] + ftw_dir[1] * ftw_len,
                ftw_s[2] + ftw_dir[2] * ftw_len
            };
            qlcg_lp_socket(b, fp, LP_SOCKET_FRUIT);
            if (def->state == PD_STATE_FRUITING) {
                LpOpts ol = qlcg_lp_opts();
                ol.sides = 3; ol.tip_ratio = 0.65; ol.cap = 1.0;
                qlcg_lp_limb(b, ftw_s, ftw_dir, ftw_len, 0.024, cone_col, &ol);
                double sz[3] = { 0.12, 0.22, 0.12 };
                LpOpts of = qlcg_lp_opts();
                of.mat = LP_MAT_FRUIT; of.rand = rand;
                of.rot_y = a + (double)k * 0.6;
                qlcg_lp_box(b, fp, sz, cone_col, &of);
            }
        }
    }

    /* 开花期冠面雄球花 */
    int pollen = qlcg_pa_bloom_count_for(def, 0, 16);
    if (pollen > 0) {
        LpRgb pcol = qlcg_pa_tone(def, "pollen", 1.0);
        LpRgb twig_c = qlcg_pa_deepen(fol, 0.2);
        int cap_n = layers < 16 ? layers : 16;
        for (int k = 0; k < pollen; k++) {
            int li = k % (cap_n > 1 ? cap_n - 1 : 1);
            double frac = 0.25 + qlcg_lp_rng_next(rand) * 0.35;
            double r_slope = lr_arr[li] * (1.0 - frac * 0.78);
            double a = (double)k * GOLDEN + 0.37;
            double py = ly_arr[li] + lh_arr[li] * frac;
            double ptw_s[3] = { cos(a) * r_slope * 0.36, py, sin(a) * r_slope * 0.36 };
            double ptw_dir[3] = { cos(a), 0.12, sin(a) };
            double ptw_len = r_slope * 0.76;
            double gp[3] = {
                ptw_s[0] + ptw_dir[0] * ptw_len,
                ptw_s[1] + ptw_dir[1] * ptw_len,
                ptw_s[2] + ptw_dir[2] * ptw_len
            };
            LpOpts ol = qlcg_lp_opts();
            ol.sides = 3; ol.tip_ratio = 0.65; ol.cap = 1.0;
            qlcg_lp_limb(b, ptw_s, ptw_dir, ptw_len, 0.020, twig_c, &ol);
            double sz[3] = { 0.08, 0.10, 0.08 };
            LpOpts op = qlcg_lp_opts();
            op.mat = LP_MAT_BLOOM; op.rand = rand;
            qlcg_lp_ico(b, gp, sz, 3, pcol, &op);
        }
    }
}

/* 幼苗期双生针叶雏形 */
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

    /* 幼苗细茎 */
    double stem_r = fmax(0.012, height * 0.05);
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.jitter = 0.1; o0.radius_ratio = 0.34;
    qlcg_lp_cone(b, p0, stem_r * 1.25, stem_r * 1.25, trunk + leaf_r * 0.22, 4, stem, &o0);

    /* 初生子叶羽片 */
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

    /* 顶芽微聚 */
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

/* 枯萎态残存枯骨与散枝 */
static void build_withered(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 14.0);
    height *= shape->height;
    LpRgb wood = qlcg_pa_tone(def, "witheredDark", 1.0);
    LpRgb stem = qlcg_pa_tone(def, "withered", 1.0);
    LpRgb dry_leaf = qlcg_pa_tone(def, "witheredStem", 1.0);
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 5.0), 3.0, 8.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.2);

    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.35) * 0.5;
    double trunk_r = fmax(0.08, qlcg_pa_pnum(def, "trunkRadius", height * 0.035));
    int limbs = 4;

    /* 枯立主干基部 */
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.jitter = jitter * 0.4; o0.rand = rand; o0.radius_ratio = 0.72;
    qlcg_lp_cone(b, p0, trunk_r * 1.4, trunk_r * 1.4, trunk * 0.34, sides, wood, &o0);
    double p1[3] = { 0, trunk * 0.26, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.jitter = jitter * 0.5; o1.rand = rand; o1.radius_ratio = 0.38;
    qlcg_lp_cone(b, p1, trunk_r * 0.96, trunk_r * 0.96, trunk * 0.68, sides, stem, &o1);

    /* 枯瘦分叉横枝 */
    double tips[4][3];
    for (int i = 0; i < limbs; i++) {
        double a = ((double)i / (double)limbs) * TAU + qlcg_lp_rng_next(rand) * 0.8;
        double up = 0.15 + qlcg_lp_rng_next(rand) * 0.45;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double dlen = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        double y = trunk * (0.34 + qlcg_lp_rng_next(rand) * 0.45);
        double len = height * (0.2 + qlcg_lp_rng_next(rand) * 0.2);
        double start[3] = { 0, y, 0 };
        double rad = trunk_r * 0.35;
        LpOpts ol = qlcg_lp_opts(); ol.sides = 4; ol.tip_ratio = 0.42; ol.cap = 1.0;
        qlcg_lp_limb(b, start, dir, len, rad, stem, &ol);
        double tip[3] = {
            start[0] + dir[0] / dlen * len,
            start[1] + dir[1] / dlen * len,
            start[2] + dir[2] / dlen * len
        };
        tips[i][0] = tip[0]; tips[i][1] = tip[1]; tips[i][2] = tip[2];
        if (qlcg_lp_rng_next(rand) < 0.65) {
            double a2 = a + (qlcg_lp_rng_next(rand) - 0.5) * 1.2;
            double up2 = up * 0.6 - 0.1;
            double dir2[3] = { cos(a2) * cos(up2), sin(up2), sin(a2) * cos(up2) };
            double len2 = len * 0.4;
            double s2[3] = {
                tip[0] - dir[0] / dlen * rad * 0.6,
                tip[1] - dir[1] / dlen * rad * 0.6,
                tip[2] - dir[2] / dlen * rad * 0.6
            };
            LpOpts ol2 = qlcg_lp_opts(); ol2.sides = 3; ol2.tip_ratio = 0.40; ol2.cap = 1.0;
            qlcg_lp_limb(b, s2, dir2, len2, trunk_r * 0.2, wood, &ol2);
        }
    }

    /* 零星残存枯针叶簇 */
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

/* 云杉物种构建主入口 */
void qlcg_plant_shu_yunshan(LpBuilder *b, const PdDef *def, int season, int form) {
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
