/* shu.cebai */

#include "shu_model.h"
#include "../../plant_forms.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const ShuShape shapes[] = {
    {1.0, 1.0, 1.0, 0},
    {1.17, 0.77, 1.10, 1},
    {0.85, 1.22, 0.87, -1},
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
    return qlcg_pa_shu_season(def, season, 0.93, 0, 2);
}

/* 简模圆卵火焰冠 */
static void build_simple(LpBuilder *b, const PdDef *def, int season) {
    const PdDict *p = &def->params;
    double h = fmax(0.1, num_or(p, "height", 8.0));
    double r = fmax(0.05, num_or(p, "canopy", h * 0.22)) * season_growth(def, season).size;
    LpRgb leaf = qlcg_lp_wind_color(pick_color(&def->palette, "foliage", "conifer", "#3a5f39"), LP_WIND_LEAF);
    LpRgb bark = qlcg_lp_wind_color(pick_color(&def->palette, "bark", "barkLight", "#725338"), LP_WIND_BARK);

    /* 极短隐蔽主干 */
    double c1[3] = { 0, 0, 0 };
    LpOpts oc1 = qlcg_lp_opts(); oc1.radius_ratio = 0.38;
    qlcg_lp_cone(b, c1, h * 0.042, h * 0.042, h * 0.45, 5, bark, &oc1);

    /* 紧凑圆卵冠身 */
    double c2[3] = { 0, h * 0.26, 0 };
    LpOpts od = qlcg_lp_opts(); od.floor_on = 1.0; od.squash = 0.28;
    qlcg_lp_dome(b, c2, r, h * 0.72, r, 6, leaf, &od);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "canopy") || qlcg_pa_has_snow_target(def, "top")) {
        double cs[3] = { 0, h * 0.82, 0 };
        qlcg_lp_socket(b, cs, LP_SOCKET_SNOW_CANOPY);
    }
}

/* 成年态紧致直立卵柱侧柏冠 */
static void build_adult(LpBuilder *b, const PdDef *def, int season) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    ShuSeason growth = season_growth(def, season);
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 8.0);
    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.12);
    height *= shape->height;
    trunk *= shape->trunk;
    LpRgb bark = qlcg_pa_tone(def, "bark", 1.0);
    double trunk_r = qlcg_pa_pnum(def, "trunkRadius", fmax(0.14, height * 0.03));
    double canopy = qlcg_pa_pnum(def, "canopy", height * 0.22);
    double base_radius = qlcg_pa_pnum(def, "layerRadius", canopy);
    base_radius *= shape->crown;
    double span = height - trunk;
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 6.0), 5.0, 8.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.08);

    /* 锥化直立短基干 */
    double p0[3] = { 0, 0, 0 };
    LpOpts ot0 = qlcg_lp_opts();
    ot0.jitter = jitter * 0.3; ot0.rand = rand; ot0.radius_ratio = 0.74;
    qlcg_lp_cone(b, p0, trunk_r * 1.25, trunk_r * 1.25, trunk * 0.52, sides, bark, &ot0);
    double p1[3] = { 0, trunk * 0.44, 0 };
    LpOpts ot1 = qlcg_lp_opts();
    ot1.jitter = jitter * 0.2; ot1.rand = rand; ot1.radius_ratio = 0.14;
    qlcg_lp_cone(b, p1, trunk_r * 0.90, trunk_r * 0.90, trunk * 0.56 + span * 0.90, sides, bark, &ot1);

    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1.0);

    /* 内部紧凑主卵柱身 */
    double cd[3] = { 0, trunk, 0 };
    LpOpts od = qlcg_lp_opts();
    od.jitter = jitter; od.rand = rand; od.floor_on = 1.0; od.squash = 0.25; od.rings = 2;
    double crown_r = base_radius * .86 * growth.size;
    double crown_h = span * (.73 + .23 * growth.density);
    qlcg_lp_dome(b, cd, crown_r, crown_h, crown_r, sides, dark, &od);

    /* 交错直立鳞叶扇 */
    int tiers = 5 + shape->branches;
    int fans = sides + 1;
    LpOpts op = qlcg_lp_opts();
    op.pitch = 0.62 - growth.droop * 0.20;
    op.cup = .24;
    for (int tier = 0; tier < tiers; tier++) {
        for (int k = 0; k < fans; k++) {
            double a = tier * GOLDEN + k * TAU / fans;
            double t = .11 + .15 * tier + .035 * qlcg_lp_rng_next(rand);
            double edge = crown_r * sqrt(1.0 - t * t);
            double length = crown_r * (.72 - .14 * t);
            double wid = length * .29;
            double dx = cos(a), dz = sin(a);
            double sy_p = sin(op.pitch);
            double pos[3] = {dx * edge * .52, trunk + crown_h * t, dz * edge * .52};
            qlcg_lp_petal(b, pos, a, length, wid, fol, &op);
            for (int j = 1; j <= 2; j++) {
                double u = (j == 1) ? 0.40 : 0.72;
                double node[3] = {
                    pos[0] + dx * length * u,
                    pos[1] + length * sy_p * u,
                    pos[2] + dz * length * u
                };
                for (int side = -1; side <= 1; side += 2) {
                    LpOpts op_sub = op;
                    op_sub.pitch = op.pitch - 0.18 - (double)j * 0.14;
                    op_sub.cup = (side > 0) ? 0.36 : 0.30;
                    qlcg_lp_petal(b, node, a + side * (0.76 - 0.18 * (double)j),
                                  length * (.64 - .37 * (j * 0.26)),
                                  length * .18, j == 1 ? dark : fol, &op_sub);
                }
            }
        }
    }

    /* 顶端聚收小圆顶 */
    double td[3] = { 0, trunk + span * 0.78, 0 };
    LpOpts otop = qlcg_lp_opts();
    otop.jitter = 0.06; otop.rand = rand; otop.floor_on = 1.0; otop.squash = 0.35;
    qlcg_lp_dome(b, td, base_radius * 0.45, span * 0.22, base_radius * 0.45, sides, fol, &otop);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "canopy")) {
        double sc[3] = { 0, trunk + span * 0.88, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }

    /* 结实期柏木小果球 */
    if (def->fruit) {
        int fc = qlcg_pa_fruit_count_for(def, 6, 8);
        LpRgb cone_col = qlcg_pa_deepen(qlcg_pa_tone(def, "barkWarm", 1.0), 0.25);
        for (int k = 0; k < fc; k++) {
            double a = (double)k * GOLDEN + qlcg_lp_rng_next(rand);
            double ft = 0.28 + qlcg_lp_rng_next(rand) * 0.42;
            double tr = crown_r * sqrt(1.0 - ft * ft);
            double fy = trunk + crown_h * ft;
            double ftw_s[3] = { cos(a) * tr * 0.42, fy, sin(a) * tr * 0.42 };
            double ftw_dir[3] = { cos(a), 0.04, sin(a) };
            double ftw_len = tr * 0.68;
            double fp[3] = {
                ftw_s[0] + ftw_dir[0] * ftw_len,
                ftw_s[1] + ftw_dir[1] * ftw_len,
                ftw_s[2] + ftw_dir[2] * ftw_len
            };
            qlcg_lp_socket(b, fp, LP_SOCKET_FRUIT);
            if (def->state == PD_STATE_FRUITING) {
                LpOpts ol = qlcg_lp_opts();
                ol.sides = 3; ol.tip_ratio = 0.65; ol.cap = 1.0;
                qlcg_lp_limb(b, ftw_s, ftw_dir, ftw_len, 0.020, cone_col, &ol);
                double sz[3] = { 0.08, 0.08, 0.08 };
                LpOpts of = qlcg_lp_opts();
                of.mat = LP_MAT_FRUIT; of.rand = rand;
                of.rot_y = a + (double)k * 0.6;
                qlcg_lp_box(b, fp, sz, cone_col, &of);
            }
        }
    }

    /* 开花期冠面雄球花 */
    int pollen = qlcg_pa_bloom_count_for(def, 0, 14);
    if (pollen > 0) {
        LpRgb pcol = qlcg_pa_tone(def, "pollen", 1.0);
        LpRgb twig_c = qlcg_pa_deepen(fol, 0.2);
        for (int k = 0; k < pollen; k++) {
            double a = (double)k * GOLDEN + 0.29;
            double ft = 0.20 + qlcg_lp_rng_next(rand) * 0.55;
            double tr = crown_r * sqrt(1.0 - ft * ft);
            double py = trunk + crown_h * ft;
            double ptw_s[3] = { cos(a) * tr * 0.42, py, sin(a) * tr * 0.42 };
            double ptw_dir[3] = { cos(a), 0.12, sin(a) };
            double ptw_len = tr * 0.68;
            double gp[3] = {
                ptw_s[0] + ptw_dir[0] * ptw_len,
                ptw_s[1] + ptw_dir[1] * ptw_len,
                ptw_s[2] + ptw_dir[2] * ptw_len
            };
            LpOpts ol = qlcg_lp_opts();
            ol.sides = 3; ol.tip_ratio = 0.65; ol.cap = 1.0;
            qlcg_lp_limb(b, ptw_s, ptw_dir, ptw_len, 0.018, twig_c, &ol);
            double sz[3] = { 0.07, 0.08, 0.07 };
            LpOpts op_b = qlcg_lp_opts();
            op_b.mat = LP_MAT_BLOOM; op_b.rand = rand;
            qlcg_lp_ico(b, gp, sz, 3, pcol, &op_b);
        }
    }
}

/* 幼苗期侧柏直立小苗 */
static void build_seedling(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double species_h = qlcg_pa_pnum(def, "height", 0.5);
    double height = fmin(species_h, fmax(0.08, species_h * 0.42));
    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.35);
    LpRgb stem = qlcg_pa_tone(def, "barkLight", 1.0);
    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    LpRgb fol_dark = qlcg_pa_tone(def, "foliageDark", 1.0);
    double leaf_r = qlcg_pa_pnum(def, "canopy", height * 0.25);
    height *= shape->height;
    trunk *= shape->trunk;
    leaf_r *= shape->crown;

    /* 短幼直茎 */
    double stem_r = fmax(0.012, height * 0.05);
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.radius_ratio = 0.48;
    qlcg_lp_cone(b, p0, stem_r * 1.15, stem_r * 1.15, trunk * 1.08, 4, stem, &o0);

    /* 贴茎直立小鳞片 */
    for (int i = 0; i < 4; i++) {
        double a = (double)i * (TAU / 4.0);
        double pos[3] = { cos(a) * stem_r * 0.42, trunk * 0.45, sin(a) * stem_r * 0.42 };
        LpOpts op = qlcg_lp_opts();
        op.pitch = 0.65; op.cup = 0.3; op.rand = rand;
        qlcg_lp_petal(b, pos, a, leaf_r * 0.9, leaf_r * 0.45, (i % 2) ? fol : fol_dark, &op);
    }
}

/* 枯萎态紧实枯立小桩 */
static void build_withered(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 8.0);
    height *= shape->height;
    LpRgb wood = qlcg_pa_tone(def, "witheredDark", 1.0);
    LpRgb stem = qlcg_pa_tone(def, "withered", 1.0);
    int sides = 5;

    double trunk = height * 0.25;
    double trunk_r = fmax(0.08, height * 0.035);

    /* 粗短枯桩 */
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.radius_ratio = 0.48;
    qlcg_lp_cone(b, p0, trunk_r * 1.2, trunk_r * 1.2, trunk * 1.05, sides, wood, &o0);

    /* 枯萎直立簇杈 */
    for (int i = 0; i < 3; i++) {
        double a = (double)i * (TAU / 3.0);
        double dir[3] = { cos(a) * 0.25, 0.95, sin(a) * 0.25 };
        double start[3] = { cos(a) * trunk_r * 0.35, trunk * 0.78, sin(a) * trunk_r * 0.35 };
        LpOpts ol = qlcg_lp_opts(); ol.sides = 3; ol.tip_ratio = 0.4; ol.cap = 1.0;
        qlcg_lp_limb(b, start, dir, height * 0.35, trunk_r * 0.35, stem, &ol);
    }
}

/* 侧柏物种构建主入口 */
void qlcg_plant_shu_cebai(LpBuilder *b, const PdDef *def, int season, int form) {
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
