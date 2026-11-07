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
    qlcg_lp_prism(b, c1, h * 0.035, h * 0.035, 0, h * 0.22, 5, bark, NULL);

    /* 狭长三段层锥 */
    for (int i = 0; i < 3; i++) {
        double y = h * (0.18 + (double)i * 0.24);
        double cr = r * (1.0 - (double)i * 0.28);
        double c2[3] = { 0, y, 0 };
        LpOpts oc = qlcg_lp_opts(); oc.cap = 1; oc.radius_ratio = (i == 2) ? 0.04 : 0.18;
        qlcg_lp_cone(b, c2, cr, cr, h * 0.32, 6, leaf, &oc);
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

    /* 红褐挺立直干与基段扩圈 */
    double p0[3] = { 0, 0, 0 };
    LpOpts ot0 = qlcg_lp_opts();
    ot0.jitter = jitter * 0.3; ot0.rand = rand; ot0.cap = 0;
    qlcg_lp_prism(b, p0, trunk_r * 1.35, trunk_r * 1.35, 0, trunk * 0.4, sides, bark, &ot0);
    double p1[3] = { 0, trunk * 0.36, 0 };
    qlcg_lp_prism(b, p1, trunk_r, trunk_r, 0, span * 0.85, sides, bark, &ot0);

    /* 紧凑窄狭下垂宝塔层 */
    for (int i = 1; i <= layers; i++) {
        double t = (double)i / (double)layers;
        double y = trunk + step * (double)i * 0.9;
        double r = (base_radius * pow(1.0 - t * 0.78, 1.1) + 0.1) * growth.size;
        double lh = (step * 1.28) * (.75 + .25 * growth.density);
        double rot = (double)i * 0.58;
        LpRgb c = (i % 2 == 0) ? fol : dark;

        /* 上表面平缓锥面 */
        double pc[3] = { 0, y, 0 };
        LpOpts oc = qlcg_lp_opts();
        oc.rot_y = rot; oc.jitter = jitter; oc.rand = rand;
        oc.radius_ratio = (i == layers) ? 0.04 : 0.16;
        qlcg_lp_cone(b, pc, r, r, lh * 0.65, sides, c, &oc);

        /* 下檐微垂折边 */
        double ps[3] = { 0, y - lh * 0.25, 0 };
        LpOpts os = qlcg_lp_opts();
        os.rot_y = rot; os.jitter = jitter * 0.5; os.rand = rand;
        os.radius_ratio = 1.0;
        qlcg_lp_cone(b, ps, r * 0.65, r * 0.65, lh * 0.25, sides, dark, &os);

        /* 雪片挂点 */
        if (qlcg_pa_has_snow_target(def, "canopy") && i >= layers - 4) {
            double sc[3] = { 0, y + lh * 0.25, 0 };
            qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
        }
    }

    /* 尖锐刺天塔梢 */
    double top_y = trunk + span * 0.93;
    double st[3] = { 0, top_y, 0 };
    LpOpts osp = qlcg_lp_opts();
    osp.jitter = 0.05; osp.rand = rand;
    qlcg_lp_cone(b, st, base_radius * 0.08, base_radius * 0.08, span * 0.16, 4, fol, &osp);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "top")) {
        double sc[3] = { 0, top_y + span * 0.06, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }

    /* 结实期下垂球果 */
    if (def->fruit) {
        int fc = qlcg_pa_fruit_count_for(def, 6, 8);
        LpRgb cone_col = qlcg_pa_tone(def, "barkWarm", .82);
        for (int k = 0; k < fc; k++) {
            double a = (double)k * GOLDEN + qlcg_lp_rng_next(rand);
            double tr = base_radius * 0.5 * (0.8 + qlcg_lp_rng_next(rand) * 0.3);
            double fy = trunk + span * (0.35 + qlcg_lp_rng_next(rand) * 0.35);
            double fp[3] = { cos(a) * tr, fy, sin(a) * tr };
            qlcg_lp_socket(b, fp, LP_SOCKET_FRUIT);
            if (def->state == PD_STATE_FRUITING) {
                double sz[3] = { 0.1, 0.2, 0.1 };
                LpOpts of = qlcg_lp_opts();
                of.mat = LP_MAT_FRUIT; of.rand = rand;
                qlcg_lp_box(b, fp, sz, cone_col, &of);
            }
        }
    }

    /* 开花期花粉飘散 */
    int pollen = qlcg_pa_bloom_count_for(def, 0, 16);
    if (pollen > 0) qlcg_pa_pollen_grains(b, def, pollen, base_radius, trunk, top_y, rand);
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
    LpRgb stem = qlcg_pa_tone(def, "bark", .82);
    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    double leaf_r = height * 0.3;
    height *= shape->height;
    trunk *= shape->trunk;
    leaf_r *= shape->crown;

    /* 细直暗褐幼茎 */
    double stem_r = fmax(0.012, height * 0.05);
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.cap = 0;
    qlcg_lp_prism(b, p0, stem_r, stem_r, 0, trunk, 4, stem, &o0);

    /* 侧展子叶小瓣 */
    for (int i = 0; i < 4; i++) {
        double a = (double)i * (TAU / 4.0);
        double pos[3] = { cos(a) * leaf_r * 0.22, trunk * 0.5, sin(a) * leaf_r * 0.22 };
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

    double trunk = height * 0.35;
    double trunk_r = fmax(0.08, height * 0.032);

    /* 枯立干段 */
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.cap = 0;
    qlcg_lp_prism(b, p0, trunk_r * 1.3, trunk_r * 1.3, 0, trunk, 5, wood, &o0);
    double p1[3] = { 0, trunk * 0.3, 0 };
    qlcg_lp_prism(b, p1, trunk_r * 0.85, trunk_r * 0.85, 0, trunk * 0.7, 4, stem, &o0);
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
