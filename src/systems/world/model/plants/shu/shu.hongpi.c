/* shu.hongpi */

#include "shu_model.h"
#include "../../plant_forms.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const ShuShape shapes[] = {
    {1.0, 1.0, 1.0, 0},
    {1.18, 0.77, 1.10, 1},
    {0.88, 1.27, 0.89, -1},
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
    return qlcg_pa_shu_season(def, season, 0.91, 0, 3);
}

/* 简模狭细红皮云杉塔 */
static void build_simple(LpBuilder *b, const PdDef *def, int season) {
    const PdDict *p = &def->params;
    double h = fmax(0.1, num_or(p, "height", 16.0));
    double r = fmax(0.05, num_or(p, "canopy", h * 0.2)) * season_growth(def, season).size;
    LpRgb leaf = qlcg_lp_wind_color(pick_color(&def->palette, "foliage", "conifer", "#3a5f39"), LP_WIND_LEAF);
    LpRgb bark = qlcg_lp_wind_color(pick_color(&def->palette, "bark", "barkDark", "#4a3628"), LP_WIND_BARK);

    /* 红褐深色基干 */
    double c1[3] = { 0, 0, 0 };
    LpOpts oc1 = qlcg_lp_opts(); oc1.radius_ratio = 0.25;
    qlcg_lp_cone(b, c1, h * 0.036, h * 0.036, h * 0.72, 5, bark, &oc1);

    /* 狭长三段层锥 */
    for (int i = 0; i < 3; i++) {
        double y = h * (0.18 + (double)i * 0.22);
        double cr = r * (1.0 - (double)i * 0.28);
        double c2[3] = { 0, y, 0 };
        LpOpts oc = qlcg_lp_opts(); oc.cap = 1; oc.radius_ratio = (i == 2) ? 0.0 : 0.20;
        qlcg_lp_cone(b, c2, cr, cr, h * 0.30, 6, leaf, &oc);
    }

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "canopy") || qlcg_pa_has_snow_target(def, "top")) {
        double cs[3] = { 0, h * 0.74, 0 };
        qlcg_lp_socket(b, cs, LP_SOCKET_SNOW_CANOPY);
    }
}

/* 成年态红皮狭尖塔宝塔云杉冠 */
static void build_adult(LpBuilder *b, const PdDef *def, int season) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    ShuSeason growth = season_growth(def, season);
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 16.0);
    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.18);
    height *= shape->height;
    trunk *= shape->trunk;
    LpRgb bark = qlcg_pa_tone(def, "barkWarm", .82);
    double trunk_r = qlcg_pa_pnum(def, "trunkRadius", fmax(0.18, height * 0.032));
    int layers = qlcg_pa_js_round(qlcg_pa_pnum(def, "layers", 8.0)) + shape->branches;
    if (layers < 5) layers = 5;
    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1.0);
    double canopy = qlcg_pa_pnum(def, "canopy", height * 0.2);
    double base_radius = qlcg_pa_pnum(def, "layerRadius", canopy);
    base_radius *= shape->crown;
    double span = height - trunk;
    double step = span / (double)(layers + 1);
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 6.0), 5.0, 8.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.1);

    /* 锥化直干与顶梢 */
    double last_y = trunk + step * (double)layers * 0.90;
    double last_r = (base_radius * pow(0.22, 1.1) + 0.1) * growth.size;
    double last_lh = (step * 1.28) * (0.75 + 0.25 * growth.density);
    double last_base = last_y - last_lh * 0.14;
    double last_ch = last_lh * 0.88;
    double top_y = last_base + last_ch * 0.85;
    double st_h = fmax(step * 0.9, trunk + span * 1.06 - top_y);

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

    /* 紧凑窄狭宝塔冠层 */
    double lr_arr[16] = { 0 };
    double ly_arr[16] = { 0 };
    double lh_arr[16] = { 0 };
    for (int i = 1; i <= layers; i++) {
        double t = (double)i / (double)layers;
        double y = trunk + step * (double)i * 0.90;
        double r = (base_radius * pow(1.0 - t * 0.78, 1.1) + 0.1) * growth.size;
        double lh = (step * 1.28) * (.75 + .25 * growth.density);
        double rot = (double)i * 0.58;
        LpRgb c = (i % 2 == 0) ? fol : dark;

        /* 叠合窄塔锥面 */
        double y_base = y - lh * 0.14;
        double cone_h = lh * 0.88;
        double pc[3] = { 0, y_base, 0 };
        LpOpts oc = qlcg_lp_opts();
        oc.rot_y = rot; oc.jitter = jitter; oc.rand = rand;
        oc.radius_ratio = 0.20;
        qlcg_lp_cone(b, pc, r, r, cone_h, sides, c, &oc);
        if (i <= 16) { lr_arr[i - 1] = r; ly_arr[i - 1] = y_base; lh_arr[i - 1] = cone_h; }

        /* 雪片挂点 */
        if (qlcg_pa_has_snow_target(def, "canopy") && i >= layers - 4) {
            double sc[3] = { 0, y + lh * 0.25, 0 };
            qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
        }
    }

    /* 尖锐刺天塔梢 */
    double st[3] = { 0, top_y, 0 };
    LpOpts osp = qlcg_lp_opts();
    osp.jitter = 0.05; osp.rand = rand; osp.cap = 1.0; osp.radius_ratio = 0.0;
    qlcg_lp_cone(b, st, last_r * 0.40, last_r * 0.40, st_h, 5, fol, &osp);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "top")) {
        double sc[3] = { 0, top_y + st_h * 0.45, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }

    /* 结实期下垂球果 */
    if (def->fruit) {
        int fc = qlcg_pa_fruit_count_for(def, 6, 8);
        LpRgb cone_col = qlcg_pa_tone(def, "barkWarm", .82);
        int cap_n = layers < 16 ? layers : 16;
        for (int k = 0; k < fc; k++) {
            int li = k % (cap_n > 1 ? cap_n - 1 : 1);
            double frac = 0.20 + qlcg_lp_rng_next(rand) * 0.30;
            double r_slope = lr_arr[li] * (1.0 - frac * 0.80);
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
                double sz[3] = { 0.1, 0.2, 0.1 };
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
            double r_slope = lr_arr[li] * (1.0 - frac * 0.80);
            double a = (double)k * GOLDEN + 0.41;
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

/* 幼苗期红皮云杉幼苗 */
static void build_seedling(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double species_h = qlcg_pa_pnum(def, "height", 0.5);
    double height = fmin(species_h, fmax(0.08, species_h * 0.42));
    double trunk = height * 0.5;
    LpRgb stem = qlcg_pa_tone(def, "barkLight", .88);
    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    double leaf_r = height * 0.3;
    height *= shape->height;
    trunk *= shape->trunk;
    leaf_r *= shape->crown;

    /* 细直暗褐幼茎 */
    double stem_r = fmax(0.012, height * 0.05);
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.radius_ratio = 0.55;
    qlcg_lp_cone(b, p0, stem_r * 1.15, stem_r * 1.15, trunk * 1.05, 4, stem, &o0);

    /* 侧展子叶小瓣 */
    for (int i = 0; i < 4; i++) {
        double a = (double)i * (TAU / 4.0);
        double pos[3] = { cos(a) * stem_r * 0.45, trunk * 0.62, sin(a) * stem_r * 0.45 };
        LpOpts op = qlcg_lp_opts();
        op.pitch = 0.15; op.cup = 0.35; op.rand = rand;
        qlcg_lp_petal(b, pos, a, leaf_r * 0.75, leaf_r * 0.4, fol, &op);
    }
}

/* 枯萎态细瘦枯塔 */
static void build_withered(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 16.0);
    height *= shape->height;
    LpRgb wood = qlcg_pa_tone(def, "witheredDark", 1.0);
    LpRgb stem = qlcg_pa_tone(def, "withered", 1.0);
    LpRgb needle = qlcg_pa_tone(def, "witheredNeedle", 1.0);

    double trunk = height * 0.36;
    double trunk_r = fmax(0.08, height * 0.032);
    int limbs = 4;

    /* 枯立锥化干段 */
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.radius_ratio = 0.72;
    qlcg_lp_cone(b, p0, trunk_r * 1.35, trunk_r * 1.35, trunk * 0.38, 5, wood, &o0);
    double p1[3] = { 0, trunk * 0.30, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.rand = rand; o1.radius_ratio = 0.32;
    qlcg_lp_cone(b, p1, trunk_r * 0.92, trunk_r * 0.92, trunk * 0.82, 5, stem, &o1);

    /* 枯枝分叉 */
    double tips[4][3];
    for (int i = 0; i < limbs; i++) {
        double a = ((double)i / (double)limbs) * TAU + qlcg_lp_rng_next(rand) * 0.7;
        double up = 0.12 + qlcg_lp_rng_next(rand) * 0.38;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double dlen = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        double y = trunk * (0.42 + qlcg_lp_rng_next(rand) * 0.55);
        double len = height * (0.16 + qlcg_lp_rng_next(rand) * 0.14);
        double start[3] = { 0, y, 0 };
        double rad = trunk_r * 0.32;
        LpOpts ol = qlcg_lp_opts(); ol.sides = 4; ol.tip_ratio = 0.42; ol.cap = 1.0;
        qlcg_lp_limb(b, start, dir, len, rad, stem, &ol);
        tips[i][0] = start[0] + dir[0] / dlen * len;
        tips[i][1] = start[1] + dir[1] / dlen * len;
        tips[i][2] = start[2] + dir[2] / dlen * len;
        if (qlcg_lp_rng_next(rand) < 0.65) {
            double a2 = a + (qlcg_lp_rng_next(rand) - 0.5) * 1.1;
            double up2 = up * 0.5 - 0.1;
            double dir2[3] = { cos(a2) * cos(up2), sin(up2), sin(a2) * cos(up2) };
            double s2[3] = {
                tips[i][0] - dir[0] / dlen * rad * 0.6,
                tips[i][1] - dir[1] / dlen * rad * 0.6,
                tips[i][2] - dir[2] / dlen * rad * 0.6
            };
            LpOpts ol2 = qlcg_lp_opts(); ol2.sides = 3; ol2.tip_ratio = 0.4; ol2.cap = 1.0;
            qlcg_lp_limb(b, s2, dir2, len * 0.4, trunk_r * 0.18, wood, &ol2);
        }
    }
    for (int i = 0; i < 2; i++) {
        double a = (double)i * GOLDEN + qlcg_lp_rng_next(rand);
        double len = fmin(height * 0.06, 0.38);
        LpOpts op = qlcg_lp_opts(); op.pitch = -0.25; op.cup = 0.45; op.rand = rand;
        qlcg_lp_petal(b, tips[i], a, len, len * 0.48, needle, &op);
    }
}

/* 红皮云杉物种构建主入口 */
void qlcg_plant_shu_hongpi(LpBuilder *b, const PdDef *def, int season, int form) {
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
