/* shu.shuishan */

#include "shu_model.h"
#include "../../plant_forms.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const ShuShape shapes[] = {
    {1.0, 1.0, 1.0, 0},
    {1.18, 0.77, 1.09, 1},
    {0.88, 1.25, 0.89, -1},
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
    return qlcg_pa_shu_season(def, season, 0, 1, 3);
}

/* 简模秀美羽状尖塔 */
static void build_simple(LpBuilder *b, const PdDef *def, int season) {
    const PdDict *p = &def->params;
    double h = fmax(0.1, num_or(p, "height", 18.0));
    double r = fmax(0.05, num_or(p, "canopy", h * 0.2)) * season_growth(def, season).size;
    LpRgb leaf = qlcg_lp_wind_color(pick_color(&def->palette, "foliage", "conifer", "#3a5f39"), LP_WIND_LEAF);
    LpRgb bark = qlcg_lp_wind_color(pick_color(&def->palette, "bark", "barkLight", "#725338"), LP_WIND_BARK);

    /* 板根基段与高干 */
    double c1[3] = { 0, 0, 0 };
    LpOpts oc1 = qlcg_lp_opts(); oc1.radius_ratio = 0.55;
    qlcg_lp_cone(b, c1, h * 0.048, h * 0.048, h * 0.32, 6, bark, &oc1);

    /* 细长羽状尖塔 */
    double c2[3] = { 0, h * 0.2, 0 };
    LpOpts oc = qlcg_lp_opts(); oc.cap = 1; oc.radius_ratio = 0.0;
    qlcg_lp_cone(b, c2, r, r, h * 0.8, 6, leaf, &oc);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "canopy") || qlcg_pa_has_snow_target(def, "top")) {
        double cs[3] = { 0, h * 0.78, 0 };
        qlcg_lp_socket(b, cs, LP_SOCKET_SNOW_CANOPY);
    }
}

/* 成年态板根凹槽细长羽状水杉塔 */
static void build_adult(LpBuilder *b, const PdDef *def, int season) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    ShuSeason growth = season_growth(def, season);
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 18.0);
    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.22);
    height *= shape->height;
    trunk *= shape->trunk;
    LpRgb bark = qlcg_pa_tone(def, "bark", 1.0);
    LpRgb bark_light = qlcg_pa_tone(def, "barkWarm", 1.0);
    double trunk_r = qlcg_pa_pnum(def, "trunkRadius", fmax(0.18, height * 0.032));
    int layers = qlcg_pa_js_round(qlcg_pa_pnum(def, "layers", 7.0)) + shape->branches;
    if (layers < 5) layers = 5;
    double canopy = qlcg_pa_pnum(def, "canopy", height * 0.2);
    double base_radius = qlcg_pa_pnum(def, "layerRadius", canopy);
    base_radius *= shape->crown;
    double span = height - trunk;
    double step = span / (double)(layers + 1);
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 6.0), 5.0, 8.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.1);
    int is_winter = (season == PD_SEASON_WINTER);

    /* 基部板根凹槽锥干 */
    double last_y = trunk + step * (double)layers * 0.9;
    double last_lh = (step * 1.35) * (0.75 + 0.25 * growth.density);
    double top_y = last_y + last_lh * 0.84;
    double spire_h = fmax(span * 0.10, trunk + span * 1.07 - top_y);

    double p0[3] = { 0, 0, 0 };
    LpOpts ot0 = qlcg_lp_opts();
    ot0.jitter = jitter * 0.3; ot0.rand = rand; ot0.radius_ratio = 0.62;
    qlcg_lp_cone(b, p0, trunk_r * 1.8, trunk_r * 1.8, trunk * 0.36, sides, bark, &ot0);

    double p1[3] = { 0, trunk * 0.28, 0 };
    LpOpts ot1 = qlcg_lp_opts();
    ot1.jitter = jitter * 0.25; ot1.rand = rand; ot1.radius_ratio = 0.68;
    qlcg_lp_cone(b, p1, trunk_r * 1.05, trunk_r * 1.05, trunk * 0.77, sides, bark, &ot1);

    double p2[3] = { 0, trunk * 0.96, 0 };
    LpOpts ot2 = qlcg_lp_opts();
    ot2.jitter = jitter * 0.16; ot2.rand = rand;
    double stem_radius = trunk_r * 0.68;
    double stem_height = is_winter ? (span * 0.90) : (top_y + spire_h * 0.24 - p2[1]);
    double stem_tip_ratio = is_winter ? 0.22 : 0.08;
    ot2.radius_ratio = stem_tip_ratio;
    qlcg_lp_cone(b, p2, stem_radius, stem_radius, stem_height, sides, bark, &ot2);

    /* 冬季落叶期裸枝塔身 */
    if (is_winter) {
        int bare_limbs = 6;
        for (int k = 0; k < bare_limbs; k++) {
            double a = ((double)k / (double)bare_limbs) * TAU;
            double ly = trunk + span * (0.2 + (double)k * 0.12);
            double dir[3] = { cos(a) * 0.85, 0.45, sin(a) * 0.85 };
            double dlen = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            double start[3] = { 0, ly, 0 };
            double blen = base_radius * (0.8 - (double)k * 0.1);
            LpOpts ol = qlcg_lp_opts(); ol.sides = 3; ol.tip_ratio = 0.42; ol.cap = 1.0;
            qlcg_lp_limb(b, start, dir, blen, trunk_r * 0.3, bark_light, &ol);
            if (qlcg_pa_has_snow_target(def, "canopy")) {
                double sc[3] = {
                    start[0] + dir[0] / dlen * blen,
                    start[1] + dir[1] / dlen * blen + 0.05,
                    start[2] + dir[2] / dlen * blen
                };
                qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
            }
        }
        return;
    }

    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1.0);
    double rim_factor = (1.0 - jitter) * cos(PI / sides);

    /* 逐层羽状细密尖塔枝层 */
    double lr_arr[16] = { 0 };
    double ly_arr[16] = { 0 };
    double lh_arr[16] = { 0 };
    double rr_arr[16] = { 0 };
    double last_top_r = base_radius * 0.12;
    for (int i = 1; i <= layers; i++) {
        double t = (double)i / (double)layers;
        double y = trunk + step * (double)i * 0.9;
        double r = (base_radius * pow(1.0 - t * 0.82, 1.15) + 0.08) * growth.size;
        double lh = (step * 1.35) * (.75 + .25 * growth.density);
        double base_t = fmax(0.0, fmin(1.0, (y - p2[1]) / stem_height));
        double top_t = fmax(0.0, fmin(1.0, (y + lh - p2[1]) / stem_height));
        double stem_outer = stem_radius * (1.0 + ot2.jitter);
        double base_cover = (stem_outer * (1.0 - (1.0 - stem_tip_ratio) * base_t) + .06) / rim_factor;
        double top_cover = (stem_outer * (1.0 - (1.0 - stem_tip_ratio) * top_t) + .06) / rim_factor;
        r = fmax(r, base_cover);
        double r_ratio = fmax(i == layers ? 0.16 : 0.18, top_cover / r);
        if (r_ratio > 0.85) r_ratio = 0.85;
        double rot = (double)i * 0.55;
        LpRgb c = (i % 2 == 0) ? fol : dark;

        /* 羽状对生层台 */
        double pc[3] = { 0, y, 0 };
        LpOpts oc = qlcg_lp_opts();
        oc.rot_y = rot; oc.jitter = jitter; oc.rand = rand;
        oc.radius_ratio = r_ratio;
        qlcg_lp_cone(b, pc, r, r, lh, sides, c, &oc);
        if (i <= 16) {
            lr_arr[i - 1] = r; ly_arr[i - 1] = y; lh_arr[i - 1] = lh; rr_arr[i - 1] = r_ratio;
        }
        if (i == layers) last_top_r = r * r_ratio;

        /* 边缘嵌入式羽状细瓣 */
        if (i <= layers - 2) {
            for (int k = 0; k < 4; k++) {
                double a = rot + (double)k * (TAU / 4.0);
                double pos[3] = { cos(a) * r * 0.48, y + lh * 0.26, sin(a) * r * 0.48 };
                LpOpts op = qlcg_lp_opts();
                op.pitch = -0.05; op.cup = 0.25; op.rand = rand;
                qlcg_lp_petal(b, pos, a, r * 0.66, r * 0.18, c, &op);
            }
        }

        /* 雪片挂点 */
        if (qlcg_pa_has_snow_target(def, "canopy") && i >= layers - 4) {
            double sc[3] = { 0, y + lh * 0.32, 0 };
            qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
        }
    }

    /* 细高针尖塔梢 */
    double st_r = fmax(base_radius * 0.11, last_top_r * 1.25);
    double st[3] = { 0, top_y, 0 };
    LpOpts os = qlcg_lp_opts();
    os.jitter = 0.05; os.rand = rand; os.cap = 1.0; os.radius_ratio = 0.0;
    qlcg_lp_cone(b, st, st_r, st_r, spire_h, 5, fol, &os);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "top")) {
        double sc[3] = { 0, top_y + spire_h * 0.45, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }

    /* 结实期下垂小果 */
    if (def->fruit) {
        int fc = qlcg_pa_fruit_count_for(def, 6, 8);
        LpRgb cone_col = qlcg_pa_deepen(qlcg_pa_tone(def, "barkWarm", 1.0), 0.25);
        int cap_n = layers < 16 ? layers : 16;
        for (int k = 0; k < fc; k++) {
            int li = k % (cap_n > 1 ? cap_n - 1 : 1);
            double frac = 0.22 + qlcg_lp_rng_next(rand) * 0.35;
            double r_slope = lr_arr[li] * (1.0 - frac * (1.0 - rr_arr[li]));
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
                qlcg_lp_limb(b, ftw_s, ftw_dir, ftw_len, 0.022, cone_col, &ol);
                double sz[3] = { 0.1, 0.16, 0.1 };
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
            double r_slope = lr_arr[li] * (1.0 - frac * (1.0 - rr_arr[li]));
            double a = (double)k * GOLDEN + 0.31;
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

/* 幼苗期水杉直立细苗 */
static void build_seedling(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double species_h = qlcg_pa_pnum(def, "height", 0.5);
    double height = fmin(species_h, fmax(0.08, species_h * 0.42));
    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.52);
    LpRgb stem = qlcg_pa_tone(def, "barkLight", 1.0);
    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    double leaf_r = qlcg_pa_pnum(def, "canopy", height * 0.28);
    height *= shape->height;
    trunk *= shape->trunk;
    leaf_r *= shape->crown;

    /* 细直幼茎 */
    double stem_r = fmax(0.012, height * 0.05);
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.radius_ratio = 0.45;
    qlcg_lp_cone(b, p0, stem_r * 1.2, stem_r * 1.2, trunk * 1.06, 4, stem, &o0);

    /* 对生羽片子叶 */
    for (int i = 0; i < 4; i++) {
        double a = (double)i * (TAU / 4.0);
        double pos[3] = { cos(a) * stem_r * 0.40, trunk * (0.5 + (double)i * 0.1), sin(a) * stem_r * 0.40 };
        LpOpts op = qlcg_lp_opts();
        op.pitch = 0.1; op.cup = 0.25; op.rand = rand;
        qlcg_lp_petal(b, pos, a, leaf_r * 0.8, leaf_r * 0.35, fol, &op);
    }
}

/* 枯萎态细瘦直立残干 */
static void build_withered(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 16.0);
    height *= shape->height;
    LpRgb wood = qlcg_pa_tone(def, "witheredDark", 1.0);
    LpRgb stem = qlcg_pa_tone(def, "withered", 1.0);

    double trunk = height * 0.35;
    double trunk_r = fmax(0.08, height * 0.032);

    /* 板根枯立锥干 */
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.radius_ratio = 0.65;
    qlcg_lp_cone(b, p0, trunk_r * 1.5, trunk_r * 1.5, trunk * 0.42, 6, wood, &o0);
    double p1[3] = { 0, trunk * 0.34, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.rand = rand; o1.radius_ratio = 0.34;
    qlcg_lp_cone(b, p1, trunk_r * 0.94, trunk_r * 0.94, trunk * 0.72, 5, stem, &o1);

    /* 对生残枯侧枝 */
    for (int i = 0; i < 4; i++) {
        double a = ((double)i / 4.0) * TAU + qlcg_lp_rng_next(rand) * 0.6;
        double up = 0.18 + qlcg_lp_rng_next(rand) * 0.30;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double dlen = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        double y = trunk * (0.45 + qlcg_lp_rng_next(rand) * 0.45);
        double len = height * (0.14 + qlcg_lp_rng_next(rand) * 0.12);
        double start[3] = { 0, y, 0 };
        double rad = trunk_r * 0.30;
        LpOpts ol = qlcg_lp_opts(); ol.sides = 3; ol.tip_ratio = 0.4; ol.cap = 1.0;
        qlcg_lp_limb(b, start, dir, len, rad, stem, &ol);
        if (qlcg_lp_rng_next(rand) < 0.6) {
            double s2[3] = {
                start[0] + dir[0] / dlen * (len - rad * 0.6),
                start[1] + dir[1] / dlen * (len - rad * 0.6),
                start[2] + dir[2] / dlen * (len - rad * 0.6)
            };
            double a2 = a + (qlcg_lp_rng_next(rand) - 0.5) * 1.0;
            double dir2[3] = { cos(a2) * 0.9, 0.3, sin(a2) * 0.9 };
            LpOpts ol2 = qlcg_lp_opts(); ol2.sides = 3; ol2.tip_ratio = 0.4; ol2.cap = 1.0;
            qlcg_lp_limb(b, s2, dir2, len * 0.4, trunk_r * 0.16, wood, &ol2);
        }
    }
}

/* 水杉物种构建主入口 */
void qlcg_plant_shu_shuishan(LpBuilder *b, const PdDef *def, int season, int form) {
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
