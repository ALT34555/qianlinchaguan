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
    qlcg_lp_prism(b, c1, h * 0.035, h * 0.035, 0, h * 0.22, 5, bark, NULL);

    /* 三段阶梯宝塔锥层 */
    for (int i = 0; i < 3; i++) {
        double y = h * (0.18 + (double)i * 0.24);
        double cr = r * (1.0 - (double)i * 0.26);
        double c2[3] = { 0, y, 0 };
        LpOpts oc = qlcg_lp_opts(); oc.cap = 1; oc.radius_ratio = (i == 2) ? 0.05 : 0.22;
        qlcg_lp_cone(b, c2, cr, cr, h * 0.32, 6, (i % 2) ? leaf : dark, &oc);
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

    /* 笔直通顶树干与膨大根颈 */
    double p0[3] = { 0, 0, 0 };
    LpOpts ot = qlcg_lp_opts();
    ot.jitter = jitter * 0.3; ot.rand = rand; ot.cap = 0;
    qlcg_lp_prism(b, p0, trunk_r * 1.35, trunk_r * 1.35, 0, trunk * 0.4, sides, bark, &ot);
    double p1[3] = { 0, trunk * 0.36, 0 };
    qlcg_lp_prism(b, p1, trunk_r, trunk_r, 0, span * 0.8, sides, bark, &ot);

    /* 逐层生成轮生下垂宝塔层 */
    for (int i = 1; i <= layers; i++) {
        double t = (double)i / (double)layers;
        double y = trunk + step * (double)i * 0.88;
        double r = (base_radius * pow(1.0 - t * 0.76, 1.08) + 0.12) * growth.size;
        double lh = (step * 1.32) * (.75 + .25 * growth.density);
        double rot = (double)i * 0.52 + (double)def->build_seed * 0.01;
        LpRgb c = qlcg_pa_deepen((i % 2 == 0) ? fol : dark, (1.0 - t) * 0.35);

        /* 上表面平展微倾伞台 */
        double pc[3] = { 0, y, 0 };
        LpOpts oc = qlcg_lp_opts();
        oc.rot_y = rot; oc.jitter = jitter; oc.rand = rand;
        oc.radius_ratio = (i == layers) ? 0.05 : 0.18;
        qlcg_lp_cone(b, pc, r, r, lh * 0.62, sides, c, &oc);

        /* 下层倒锥收敛下垂裙边 */
        double ps[3] = { 0, y - lh * 0.28, 0 };
        LpOpts os = qlcg_lp_opts();
        os.rot_y = rot; os.jitter = jitter * 0.5; os.rand = rand;
        os.radius_ratio = 1.0;
        qlcg_lp_cone(b, ps, r * 0.62, r * 0.62, lh * 0.28, sides, dark, &os);

        /* 边缘下垂微翘小枝羽片 */
        if (i <= layers - 1) {
            int skirts = sides;
            for (int k = 0; k < skirts; k++) {
                double a = rot + ((double)k / (double)skirts) * TAU;
                double pos[3] = { cos(a) * r * 0.94, y + lh * 0.04, sin(a) * r * 0.94 };
                LpOpts op = qlcg_lp_opts();
                op.pitch = -0.32; op.cup = 0.28; op.rand = rand;
                qlcg_lp_petal(b, pos, a, r * 0.38, r * 0.22, c, &op);
            }
        }

        /* 雪片挂点 */
        if (qlcg_pa_has_snow_target(def, "canopy") && i >= layers - 4) {
            double sc[3] = { 0, y + lh * 0.22, 0 };
            qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
        }
    }

    /* 尖耸挺直宝塔尖梢 */
    double top_y = trunk + span * 0.92;
    double st[3] = { 0, top_y, 0 };
    LpOpts osp = qlcg_lp_opts();
    osp.jitter = 0.05; osp.rand = rand;
    qlcg_lp_cone(b, st, base_radius * 0.1, base_radius * 0.1, span * 0.14, 4, fol, &osp);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "top")) {
        double sc[3] = { 0, top_y + span * 0.06, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }

    /* 结实期下垂球果 */
    if (def->fruit) {
        int fc = qlcg_pa_fruit_count_for(def, 6, 8);
        LpRgb cone_col = qlcg_pa_tone(def, "barkDark", 1.0);
        for (int k = 0; k < fc; k++) {
            double a = (double)k * GOLDEN + qlcg_lp_rng_next(rand);
            double tr = base_radius * 0.55 * (0.8 + qlcg_lp_rng_next(rand) * 0.3);
            double fy = trunk + span * (0.35 + qlcg_lp_rng_next(rand) * 0.35);
            double fp[3] = { cos(a) * tr, fy, sin(a) * tr };
            qlcg_lp_socket(b, fp, LP_SOCKET_FRUIT);
            if (def->state == PD_STATE_FRUITING) {
                double sz[3] = { 0.12, 0.22, 0.12 };
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
    o0.rand = rand; o0.jitter = 0.1; o0.cap = 0;
    qlcg_lp_prism(b, p0, stem_r * 1.3, stem_r * 1.3, 0, trunk * 0.5, 4, stem, &o0);
    double p1[3] = { 0, trunk * 0.46, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.rand = rand; o1.jitter = 0.12; o1.cap = 0;
    qlcg_lp_prism(b, p1, stem_r, stem_r, 0, trunk * 0.56, 4, stem, &o1);

    /* 初生子叶羽片 */
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

    /* 顶芽微聚 */
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
    o0.jitter = jitter * 0.5; o0.rand = rand; o0.cap = 0;
    qlcg_lp_prism(b, p0, trunk_r * 1.4, trunk_r * 1.4, 0, trunk * 0.3, sides, wood, &o0);
    double p1[3] = { 0, trunk * 0.22, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.jitter = jitter; o1.rand = rand; o1.cap = 0;
    qlcg_lp_prism(b, p1, trunk_r * 1.05, trunk_r * 1.05, 0, trunk * 0.62, sides, stem, &o1);

    /* 枯瘦分叉横枝 */
    for (int i = 0; i < limbs; i++) {
        double a = ((double)i / (double)limbs) * TAU + qlcg_lp_rng_next(rand) * 0.8;
        double up = 0.15 + qlcg_lp_rng_next(rand) * 0.45;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double y = trunk * (0.3 + qlcg_lp_rng_next(rand) * 0.5);
        double len = height * (0.2 + qlcg_lp_rng_next(rand) * 0.2);
        double start[3] = { 0, y, 0 };
        double rad = trunk_r * 0.35;
        double tip[3];
        qlcg_pa_limb_to(b, start, dir, len, rad, stem, tip);
        if (qlcg_lp_rng_next(rand) < 0.65) {
            double a2 = a + (qlcg_lp_rng_next(rand) - 0.5) * 1.2;
            double up2 = up * 0.6 - 0.1;
            double dir2[3] = { cos(a2) * cos(up2), sin(up2), sin(a2) * cos(up2) };
            double len2 = len * 0.4;
            qlcg_pa_limb_to(b, tip, dir2, len2, trunk_r * 0.2, wood, NULL);
        }
    }

    /* 零星残存枯针叶簇 */
    for (int i = 0; i < 3; i++) {
        double a = (double)i * GOLDEN + (double)def->build_seed * 0.0017;
        double rad = fmin(height * 0.15, trunk_r * 3.0);
        double len = fmin(height * 0.08, 0.45);
        double pos[3] = { cos(a) * rad, trunk * (0.6 + qlcg_lp_rng_next(rand) * 0.4), sin(a) * rad };
        double bearing = a + (qlcg_lp_rng_next(rand) - 0.5) * 0.8;
        LpRgb col = qlcg_pa_deepen(dry_leaf, qlcg_lp_rng_next(rand) * 0.3);
        LpOpts o = qlcg_lp_opts();
        o.pitch = -0.25; o.cup = 0.5; o.rand = rand;
        qlcg_lp_petal(b, pos, bearing, len, len * 0.5, col, &o);
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
