/* shu.mumahuang */

#include "shu_model.h"
#include "../../plant_forms.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const ShuShape shapes[] = {
    {1.0, 1.0, 1.0, 0},
    {1.17, 0.79, 1.15, -1},
    {0.90, 1.25, 0.85, 1},
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
    return qlcg_pa_shu_season(def, season, 0.78, 3, 0);
}

/* 简模细丝悬垂冠 */
static void build_simple(LpBuilder *b, const PdDef *def, int season) {
    const PdDict *p = &def->params;
    double h = fmax(0.1, num_or(p, "height", 14.0));
    double r = fmax(0.05, num_or(p, "canopy", h * 0.22)) * season_growth(def, season).size;
    LpRgb leaf = qlcg_lp_wind_color(pick_color(&def->palette, "foliage", "conifer", "#3a5f39"), LP_WIND_LEAF);
    LpRgb bark = qlcg_lp_wind_color(pick_color(&def->palette, "bark", "barkLight", "#725338"), LP_WIND_BARK);

    /* 挺拔直干 */
    double c1[3] = { 0, 0, 0 };
    qlcg_lp_prism(b, c1, h * 0.035, h * 0.035, 0, h * 0.4, 5, bark, NULL);

    /* 狭长悬垂卵冠 */
    double c2[3] = { 0, h * 0.38, 0 };
    LpOpts od = qlcg_lp_opts(); od.floor_on = 0;
    qlcg_lp_dome(b, c2, r, h * 0.58, r, 6, leaf, &od);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "canopy") || qlcg_pa_has_snow_target(def, "top")) {
        double cs[3] = { 0, h * 0.82, 0 };
        qlcg_lp_socket(b, cs, LP_SOCKET_SNOW_CANOPY);
    }
}

/* 成年态如烟丝垂柔美木麻黄 */
static void build_adult(LpBuilder *b, const PdDef *def, int season) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    ShuSeason growth = season_growth(def, season);
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 14.0);
    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.35);
    height *= shape->height;
    trunk *= shape->trunk;
    LpRgb bark = qlcg_pa_tone(def, "bark", 1.0);
    LpRgb bark_light = qlcg_pa_tone(def, "barkLight", 1.0);
    double trunk_r = qlcg_pa_pnum(def, "trunkRadius", fmax(0.18, height * 0.032));
    double canopy = qlcg_pa_pnum(def, "canopy", height * 0.22);
    double base_radius = qlcg_pa_pnum(def, "layerRadius", canopy);
    base_radius *= shape->crown;
    double span = height - trunk;
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 5.0), 4.0, 7.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.12);

    /* 修长挺拔直立主干 */
    double p0[3] = { 0, 0, 0 };
    LpOpts ot0 = qlcg_lp_opts();
    ot0.jitter = jitter * 0.3; ot0.rand = rand; ot0.cap = 0;
    qlcg_lp_prism(b, p0, trunk_r * 1.3, trunk_r * 1.3, 0, trunk * 0.45, sides, bark, &ot0);
    double p1[3] = { 0, trunk * 0.42, 0 };
    LpOpts ot1 = qlcg_lp_opts();
    ot1.jitter = jitter * 0.4; ot1.rand = rand; ot1.cap = 0;
    qlcg_lp_prism(b, p1, trunk_r * 0.9, trunk_r * 0.9, 0, trunk * 0.58, sides, bark, &ot1);

    /* 内部中轴干通顶 */
    double p2[3] = { 0, trunk, 0 };
    LpOpts ot2 = qlcg_lp_opts();
    ot2.jitter = jitter * 0.2; ot2.rand = rand; ot2.cap = 0;
    qlcg_lp_prism(b, p2, trunk_r * 0.65, trunk_r * 0.65, 0, span * 0.85, sides, bark, &ot2);

    /* 斜出主分枝 */
    int limbs = 5 + shape->branches;
    double tips[6][3];
    for (int k = 0; k < limbs; k++) {
        double a = ((double)k / (double)limbs) * TAU + qlcg_lp_rng_next(rand) * 0.5;
        double up = 0.42 + qlcg_lp_rng_next(rand) * 0.3;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double ly = trunk + span * (0.15 + (double)k * 0.14);
        double start[3] = { 0, ly, 0 };
        double llen = base_radius * (0.8 + qlcg_lp_rng_next(rand) * 0.3);
        qlcg_pa_limb_to(b, start, dir, llen, trunk_r * 0.35, bark_light, tips[k]);
    }

    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1.0);

    /* 悬垂如烟丝状细枝羽叶束 */
    for (int k = 0; k < limbs; k++) {
        double *tip = tips[k];
        double cr = (base_radius * 0.45) * growth.size;
        int ribbons = 4;
        for (int r = 0; r < ribbons; r++) {
            double ra = (double)r * 1.57 + qlcg_lp_rng_next(rand) * 0.4;
            double rpos[3] = { tip[0] + cos(ra) * cr * 0.5, tip[1], tip[2] + sin(ra) * cr * 0.5 };
            LpOpts op = qlcg_lp_opts();
            op.pitch = -0.65; op.cup = 0.22; op.rand = rand;
            qlcg_lp_petal(b, rpos, ra, cr * 1.25, cr * 0.28, (r % 2) ? fol : dark, &op);
        }
    }

    /* 树冠中上部轻透叶簇 */
    int puffs = 3;
    for (int c = 0; c < puffs; c++) {
        double cy = trunk + span * (0.45 + (double)c * 0.18);
        double cr = (base_radius * (0.65 - (double)c * 0.15)) * growth.size;
        double cpos[3] = { 0, cy, 0 };
        LpOpts oc = qlcg_lp_opts();
        oc.jitter = 0.18; oc.rand = rand; oc.floor_on = 0;
        qlcg_lp_dome(b, cpos, cr, cr * 0.65, cr, 5, fol, &oc);
    }

    /* 梢顶细柔尖束 */
    double st[3] = { 0, trunk + span * 0.85, 0 };
    LpOpts os = qlcg_lp_opts();
    os.jitter = 0.1; os.rand = rand;
    qlcg_lp_cone(b, st, base_radius * 0.2, base_radius * 0.2, span * 0.16, 4, fol, &os);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "canopy")) {
        double sc[3] = { 0, trunk + span * 0.9, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }

    /* 开花期花粉飘散 */
    int pollen = qlcg_pa_bloom_count_for(def, 0, 14);
    if (pollen > 0) qlcg_pa_pollen_grains(b, def, pollen, base_radius, trunk, trunk + span, rand);
}

/* 幼苗期木麻黄细苗 */
static void build_seedling(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double species_h = qlcg_pa_pnum(def, "height", 0.5);
    double height = fmin(species_h, fmax(0.08, species_h * 0.42));
    double trunk = height * 0.5;
    LpRgb stem = qlcg_pa_tone(def, "barkLight", 1.0);
    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    double leaf_r = height * 0.28;
    height *= shape->height;
    trunk *= shape->trunk;
    leaf_r *= shape->crown;

    /* 直幼细茎 */
    double stem_r = fmax(0.012, height * 0.05);
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.cap = 0;
    qlcg_lp_prism(b, p0, stem_r, stem_r, 0, trunk, 4, stem, &o0);

    /* 幼苗下垂丝片 */
    for (int i = 0; i < 4; i++) {
        double a = (double)i * (TAU / 4.0);
        double pos[3] = { cos(a) * leaf_r * 0.2, trunk * 0.6, sin(a) * leaf_r * 0.2 };
        LpOpts op = qlcg_lp_opts();
        op.pitch = -0.45; op.cup = 0.2; op.rand = rand;
        qlcg_lp_petal(b, pos, a, leaf_r * 0.9, leaf_r * 0.25, fol, &op);
    }
}

/* 枯萎态干硬枯干 */
static void build_withered(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 14.0);
    height *= shape->height;
    LpRgb wood = qlcg_pa_tone(def, "witheredDark", 1.0);
    LpRgb stem = qlcg_pa_tone(def, "withered", 1.0);

    double trunk = height * 0.35;
    double trunk_r = fmax(0.08, height * 0.032);

    /* 枯立主干 */
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.cap = 0;
    qlcg_lp_prism(b, p0, trunk_r * 1.3, trunk_r * 1.3, 0, trunk, 5, wood, &o0);
    double p1[3] = { 0, trunk * 0.3, 0 };
    qlcg_lp_prism(b, p1, trunk_r * 0.85, trunk_r * 0.85, 0, trunk * 0.7, 4, stem, &o0);
}

/* 木麻黄物种构建主入口 */
void qlcg_plant_shu_mumahuang(LpBuilder *b, const PdDef *def, int season, int form) {
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
