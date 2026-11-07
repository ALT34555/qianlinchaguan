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
    qlcg_lp_prism(b, c1, h * 0.035, h * 0.035, 0, h * 0.12, 5, bark, NULL);

    /* 紧凑圆卵冠身 */
    double c2[3] = { 0, h * 0.1, 0 };
    LpOpts od = qlcg_lp_opts(); od.floor_on = 0;
    qlcg_lp_dome(b, c2, r, h * 0.88, r, 6, leaf, &od);

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

    /* 低矮粗实短基干 */
    double p0[3] = { 0, 0, 0 };
    LpOpts ot = qlcg_lp_opts();
    ot.jitter = jitter * 0.3; ot.rand = rand; ot.cap = 0;
    qlcg_lp_prism(b, p0, trunk_r * 1.25, trunk_r * 1.25, 0, trunk * 0.5, sides, bark, &ot);
    double p1[3] = { 0, trunk * 0.45, 0 };
    qlcg_lp_prism(b, p1, trunk_r, trunk_r, 0, span * 0.65, sides, bark, &ot);

    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1.0);

    /* 内部紧凑主卵柱身 */
    double cd[3] = { 0, trunk, 0 };
    LpOpts od = qlcg_lp_opts();
    od.jitter = jitter; od.rand = rand; od.floor_on = 0; od.rings = 2;
    double crown_r = base_radius * .86 * growth.size;
    double crown_h = span * (.73 + .23 * growth.density);
    qlcg_lp_dome(b, cd, crown_r, crown_h, crown_r, sides, dark, &od);

    /* 交错直立鳞叶扇 */
    int tiers = 5 + shape->branches;
    int fans = sides + 1;
    LpOpts op = qlcg_lp_opts();
    op.pitch = 1.02 - growth.droop * .33;
    op.cup = .26;
    for (int tier = 0; tier < tiers; tier++) {
        for (int k = 0; k < fans; k++) {
            double a = tier * GOLDEN + k * TAU / fans;
            double t = .11 + .15 * tier + .035 * qlcg_lp_rng_next(rand);
            double edge = crown_r * sqrt(1.0 - t * t);
            double length = crown_r * (.58 - .18 * t);
            double dx = cos(a), dz = sin(a);
            double pos[3] = {dx * edge * .59, trunk + crown_h * t, dz * edge * .59};
            qlcg_lp_petal(b, pos, a, length, length * .29, fol, &op);
            for (int j = 1; j <= 2; j++) {
                double u = j * .26;
                double node[3] = {pos[0] + dx * length * u, pos[1] + sin(op.pitch) * length * u,
                                  pos[2] + dz * length * u};
                for (int side = -1; side <= 1; side += 2)
                    qlcg_lp_petal(b, node, a + side * .63, length * (.64 - .37 * u),
                                  length * .18, j == 1 ? dark : fol, &op);
            }
        }
    }

    /* 顶端聚收小圆顶 */
    double td[3] = { 0, trunk + span * 0.82, 0 };
    LpOpts otop = qlcg_lp_opts();
    otop.jitter = 0.06; otop.rand = rand; otop.floor_on = 0;
    qlcg_lp_dome(b, td, base_radius * 0.45, span * 0.18, base_radius * 0.45, sides, fol, &otop);

    /* 雪片挂点 */
    if (qlcg_pa_has_snow_target(def, "canopy")) {
        double sc[3] = { 0, trunk + span * 0.88, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }

    /* 结实期柏木小果球 */
    if (def->fruit) {
        int fc = qlcg_pa_fruit_count_for(def, 6, 8);
        LpRgb cone_col = qlcg_pa_tone(def, "barkDark", 1.0);
        for (int k = 0; k < fc; k++) {
            double a = (double)k * GOLDEN + qlcg_lp_rng_next(rand);
            double tr = base_radius * 0.88;
            double fy = trunk + span * (0.3 + qlcg_lp_rng_next(rand) * 0.45);
            double fp[3] = { cos(a) * tr, fy, sin(a) * tr };
            qlcg_lp_socket(b, fp, LP_SOCKET_FRUIT);
            if (def->state == PD_STATE_FRUITING) {
                double sz[3] = { 0.08, 0.08, 0.08 };
                LpOpts of = qlcg_lp_opts();
                of.mat = LP_MAT_FRUIT; of.rand = rand;
                qlcg_lp_box(b, fp, sz, cone_col, &of);
            }
        }
    }

    /* 开花期花粉飘散 */
    int pollen = qlcg_pa_bloom_count_for(def, 0, 14);
    if (pollen > 0) qlcg_pa_pollen_grains(b, def, pollen, base_radius, trunk, trunk + span, rand);
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
    o0.rand = rand; o0.cap = 0;
    qlcg_lp_prism(b, p0, stem_r, stem_r, 0, trunk, 4, stem, &o0);

    /* 贴茎直立小鳞片 */
    for (int i = 0; i < 4; i++) {
        double a = (double)i * (TAU / 4.0);
        double pos[3] = { cos(a) * leaf_r * 0.25, trunk * 0.4, sin(a) * leaf_r * 0.25 };
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
    o0.rand = rand; o0.cap = 0;
    qlcg_lp_prism(b, p0, trunk_r * 1.2, trunk_r * 1.2, 0, trunk, sides, wood, &o0);

    /* 枯萎直立簇杈 */
    for (int i = 0; i < 3; i++) {
        double a = (double)i * (TAU / 3.0);
        double dir[3] = { cos(a) * 0.25, 0.95, sin(a) * 0.25 };
        double start[3] = { cos(a) * trunk_r * 0.4, trunk * 0.8, sin(a) * trunk_r * 0.4 };
        qlcg_pa_limb_to(b, start, dir, height * 0.35, trunk_r * 0.35, stem, NULL);
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
