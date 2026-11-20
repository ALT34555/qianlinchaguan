/* hua.zhu */

#include "hua_model.h"
#include "../../plant_forms.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *culm, *leaf;
    double height, spread;
    int culms;
} GardenBambooShape;

static const GardenBambooShape shapes[] = {
    {"#508551", "#3F8149", 1.0, 1.0, 0},
    {"#88AE58", "#78A541", 1.10, .90, -1},
    {"#398477", "#2D7561", .93, 1.14, 1},
};

static const HuaSeedProfile bamboo_seed = {HUA_GRAINS, 8, .046, 2.65, "#C1A674"};

static double num_or(const PdDict *d, const char *key, double dflt) {
    double v = pd_num(d, key, NAN);
    return isnan(v) ? dflt : v;
}

static void build_simple(LpBuilder *b, const PdDef *def, int season) {
    (void)season;
    const PdDict *p = &def->params;
    double h = fmax(0.1, num_or(p, "height", num_or(p, "trunk", 1.0)));
    double r = fmax(0.05, num_or(p, "canopy", num_or(p, "spread", h * 0.3)));

    const GardenBambooShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRgb leaf = qlcg_lp_wind_color(qlcg_lp_hex(shape->leaf, 65, 130, 74), LP_WIND_LEAF);
    LpRgb culm = qlcg_lp_wind_color(qlcg_lp_hex(shape->culm, 78, 129, 76), LP_WIND_BARK);

    double o[3] = { 0, 0, 0 };

    qlcg_lp_prism(b, o, h * 0.025, h * 0.025, 0, h * 0.84, 4, culm, NULL);
    double crown[3] = {0, h * .64, 0};
    qlcg_lp_dome(b, crown, r, h * .29, r, 5, leaf, NULL);
}

typedef struct {
    double base[3], dir[3], len, angle;
} BambooTwig;

static void bamboo_leaf(LpBuilder *b, const double start[3], double bearing,
                        double len, double wid, double rise, double drop,
                        LpRgb col, LpRng *rand) {
    double p1 = atan2(rise, len * 0.5) + 0.14;
    double dx = cos(bearing), dz = sin(bearing);
    double l1 = len * 0.56;
    double tip1[3] = { start[0] + dx * l1, start[1] + sin(p1) * l1, start[2] + dz * l1 };
    LpOpts o1 = qlcg_lp_opts();
    o1.pitch = p1; o1.cup = 0.34; o1.mat = LP_MAT_SOLID; o1.rand = rand;
    qlcg_lp_petal(b, start, bearing, l1, wid, col, &o1);
    LpOpts o2 = qlcg_lp_opts();
    o2.pitch = atan2(rise - drop, len * 0.5) - 0.04;
    o2.cup = 0.3; o2.mat = LP_MAT_SOLID; o2.rand = rand;
    qlcg_lp_petal(b, tip1, bearing, len * 0.52, wid * 0.72, col, &o2);
}

static void build_adult(LpBuilder *b, const PdDef *def, int season) {
    static const double density[] = {.72, 1.0, .83, .57};
    const GardenBambooShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 6.0) * shape->height;
    int culms = qlcg_pa_js_round(qlcg_pa_pnum(def, "culms", 4.0)) + shape->culms;
    if (culms < 2) culms = 2;
    double culm_r = qlcg_pa_pnum(def, "trunkRadius", height * 0.022);
    if (culm_r < 0.04) culm_r = 0.04;
    double spread = qlcg_pa_pnum(def, "spread", height * 0.16) * shape->spread;
    double node_step = qlcg_pa_pnum(def, "nodeStep", height * 0.18);
    if (node_step < 0.35) node_step = 0.35;
    int blades = qlcg_pa_js_round(qlcg_pa_pnum(def, "blades", 12.0) * (season == PD_SEASON_NONE ? 1.0 : density[season]));
    if (blades < 3) blades = 3;
    double leaf_len = qlcg_pa_pnum(def, "frondLength", height * 0.16);
    LpRgb stem = qlcg_lp_wind_color(qlcg_lp_hex(shape->culm, 78, 129, 76), LP_WIND_BARK);
    LpRgb node = qlcg_pa_deepen(stem, 0.3);
    LpRgb leaf = qlcg_lp_wind_color(qlcg_lp_hex(shape->leaf, 65, 130, 74), LP_WIND_LEAF);
    LpRgb leaf_dark = qlcg_pa_deepen(leaf, season == PD_SEASON_AUTUMN ? .28 : .19);
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 5.0), 3.0, 6.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.1);

    double (*tops)[3] = (double (*)[3])malloc((size_t)culms * 3 * sizeof(double));
    int top_count = 0;
    int max_twigs = culms * 8;
    BambooTwig *twigs = (BambooTwig *)malloc((size_t)max_twigs * sizeof(BambooTwig));
    int twig_count = 0;
    for (int c = 0; c < culms; c++) {
        double a = ((double)c / (double)culms) * TAU + qlcg_lp_rng_next(rand) * 0.9;
        double rad = spread * (0.25 + qlcg_lp_rng_next(rand) * 0.75);
        double x0 = cos(a) * rad;
        double z0 = sin(a) * rad;
        double h = height * (0.62 + qlcg_lp_rng_next(rand) * 0.4);
        double lean = qlcg_pa_pnum(def, "lean", 0.08) * (qlcg_lp_rng_next(rand) - 0.5) * 2.0;
        int nodes = qlcg_pa_js_round(h / node_step);
        if (nodes < 3) nodes = 3;
        double node_h = h / (double)nodes;
        double r = culm_r * (0.85 + qlcg_lp_rng_next(rand) * 0.4);
        double cur[3] = { x0, 0.0, z0 };
        for (int i = 0; i < nodes; i++) {
            double t = (double)i / (double)nodes;
            double t1 = (double)(i + 1) / (double)nodes;
            double y1 = (double)(i + 1) * node_h;
            double x1 = x0 + lean * y1 * 0.35 * (0.4 + t1);
            double nxt[3] = { x1, y1, z0 };
            double rn0 = r * (1.0 - t * 0.2);
            double rn1 = r * (1.0 - t1 * 0.2);
            double r_base = i > 0 ? rn0 * 0.88 : rn0;
            double r_tip = rn1 * 1.02;
            double dx = nxt[0] - cur[0], dy = nxt[1] - cur[1], dz = nxt[2] - cur[2];
            double sl = sqrt(dx * dx + dy * dy + dz * dz);
            if (sl > 0.0) {
                double d[3] = { dx / sl, dy / sl, dz / sl };
                double ov = i > 0 ? sl * 0.04 : 0.0;
                double st[3] = { cur[0] - d[0] * ov, cur[1] - d[1] * ov, cur[2] - d[2] * ov };
                LpOpts o = qlcg_lp_opts();
                o.sides = sides; o.tip_ratio = r_tip / r_base; o.cap = 1;
                qlcg_lp_limb(b, st, d, sl + ov, r_base, stem, &o);
                if (i < nodes - 1) {
                    LpOpts on = qlcg_lp_opts();
                    on.squash = 0.6; on.rot_y = (double)i * 0.25;
                    on.jitter = jitter * 0.3; on.rand = rand;
                    qlcg_lp_dome(b, nxt, rn1 * 1.28, node_h * 0.07, rn1 * 1.28, sides, node, &on);
                }
                if (i >= nodes / 2 && i >= nodes - 4) {
                    double back = i == nodes - 1 ? fmin(0.03, r_tip * 0.5) : 0.0;
                    double twb[3] = { nxt[0] - d[0] * back, nxt[1] - d[1] * back, nxt[2] - d[2] * back };
                    int cnt = i >= nodes - 2 ? 2 : 1;
                    for (int k = 0; k < cnt; k++) {
                        double ba = a + (double)c * 0.7 + (((i + k) % 2) ? 0.9 : -0.9)
                                    + (qlcg_lp_rng_next(rand) - 0.5) * 0.8;
                        double up = 0.38 + qlcg_lp_rng_next(rand) * 0.34;
                        double bd[3] = { cos(ba) * cos(up), sin(up), sin(ba) * cos(up) };
                        double blen = leaf_len * (0.45 + qlcg_lp_rng_next(rand) * 0.35);
                        LpOpts ot = qlcg_lp_opts();
                        ot.sides = 3; ot.tip_ratio = 0.4; ot.cap = 1;
                        qlcg_lp_limb(b, twb, bd, blen, fmax(0.010, r_tip * 0.28), node, &ot);
                        if (twigs && twig_count < max_twigs) {
                            BambooTwig *tw = &twigs[twig_count++];
                            tw->base[0] = twb[0]; tw->base[1] = twb[1]; tw->base[2] = twb[2];
                            tw->dir[0] = bd[0]; tw->dir[1] = bd[1]; tw->dir[2] = bd[2];
                            tw->len = blen; tw->angle = ba;
                        }
                    }
                }
            }
            cur[0] = nxt[0]; cur[1] = nxt[1]; cur[2] = nxt[2];
        }
        if (tops && top_count < culms) {
            tops[top_count][0] = cur[0];
            tops[top_count][1] = cur[1];
            tops[top_count][2] = cur[2];
            top_count++;
        }
    }
    int per_tw = twig_count > 0 ? (blades / twig_count) + 1 : 1;
    for (int i = 0; i < blades; i++) {
        if (twig_count <= 0) break;
        const BambooTwig *tw = &twigs[i % twig_count];
        int ring_idx = i / twig_count;
        double u = 0.35 + 0.58 * (((double)ring_idx + 0.5) / (double)per_tw);
        double anc[3] = { tw->base[0] + tw->dir[0] * tw->len * u,
                          tw->base[1] + tw->dir[1] * tw->len * u,
                          tw->base[2] + tw->dir[2] * tw->len * u };
        double la = tw->angle + (double)ring_idx * GOLDEN * 0.5 + (qlcg_lp_rng_next(rand) - 0.5) * 0.7;
        double len = leaf_len * (0.45 + qlcg_lp_rng_next(rand) * 0.35);
        double rise = len * (0.18 + 0.12 * qlcg_lp_rng_next(rand));
        LpRgb col = (i % 2) ? leaf : leaf_dark;
        bamboo_leaf(b, anc, la, len, leaf_len * 0.15, rise,
                    len * (0.5 + qlcg_lp_rng_next(rand) * 0.4), col, rand);
    }
    for (int i = 0; i < 2; i++) {
        double a = qlcg_lp_rng_next(rand) * TAU;
        double rad = spread * (0.3 + qlcg_lp_rng_next(rand) * 0.5);
        double sc[3] = { cos(a) * rad, 0, sin(a) * rad };
        LpOpts o = qlcg_lp_opts();
        o.jitter = 0.2; o.rand = rand; o.cap = 1;
        qlcg_lp_cone(b, sc, culm_r * 0.9, culm_r * 0.9, height * 0.07, 4, stem, &o);
    }
    if (season == PD_SEASON_WINTER && qlcg_pa_has_snow_target(def, "canopy"))
        for (int i = 0; i < top_count; i++) qlcg_lp_socket(b, tops[i], LP_SOCKET_SNOW_CANOPY);
    int seeds = qlcg_pa_hua_seed_count(def, season);
    for (int i = 0; i < top_count && i < seeds; i++) {
        if (def->state == PD_STATE_FRUITING) {
            double sb[3] = { tops[i][0], tops[i][1] - 0.02, tops[i][2] };
            double up[3] = { 0.0, 1.0, 0.0 };
            LpOpts os = qlcg_lp_opts();
            os.sides = 3; os.tip_ratio = 0.45; os.cap = 1;
            qlcg_lp_limb(b, sb, up, 0.046 * shape->spread * 2.65 * 0.50 + 0.02,
                         fmax(0.008, culm_r * 0.25), stem, &os);
        }
        qlcg_pa_hua_seed(b, tops[i], &bamboo_seed, shape->spread, def->state == PD_STATE_FRUITING);
    }
    free(twigs);
    free(tops);
}

static void build_seedling(LpBuilder *b, const PdDef *def) {
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

    double stem_r = fmax(0.012, height * 0.05);
    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.rand = rand; o0.jitter = 0.1; o0.cap = 1;
    qlcg_lp_prism(b, p0, stem_r * 1.3, stem_r * 1.3, 0, trunk * 0.5, 4, stem, &o0);
    double p1[3] = { 0, trunk * 0.45, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.rand = rand; o1.jitter = 0.12; o1.cap = 1; o1.rot_y = 0.78;
    qlcg_lp_prism(b, p1, stem_r, stem_r, 0, trunk * 0.56, 4, stem, &o1);

    for (int i = 0; i < 2; i++) {
        double a = (double)i * PI + qlcg_lp_rng_next(rand) * 0.5;
        double pos[3] = { cos(a) * stem_r * 0.25, trunk * (0.44 + (double)i * 0.05), sin(a) * stem_r * 0.25 };

        double plen = leaf_r * (0.95 + qlcg_lp_rng_next(rand) * 0.3);
        LpOpts o = qlcg_lp_opts();
        o.pitch = -0.06 + qlcg_lp_rng_next(rand) * 0.2; o.cup = 0.35; o.rand = rand;
        qlcg_lp_petal(b, pos, a, plen, leaf_r * 0.75, i ? fol : fol_dark, &o);
    }
    for (int i = 0; i < 4; i++) {
        double a = (double)i * GOLDEN + (double)def->build_seed * 0.0017 + qlcg_lp_rng_next(rand) * 0.4;
        double len = leaf_r * (0.7 + qlcg_lp_rng_next(rand) * 0.4);
        double pos[3] = { cos(a) * stem_r * 0.25, trunk * (0.62 + (double)i * 0.08), sin(a) * stem_r * 0.25 };
        LpOpts o = qlcg_lp_opts();
        o.pitch = 0.12 + qlcg_lp_rng_next(rand) * 0.3; o.cup = 0.4; o.rand = rand;
        qlcg_lp_petal(b, pos, a, len, len * 0.52, (i % 2) ? fol : fol_dark, &o);
    }
    double dc[3] = { 0, trunk + leaf_r * 0.3, 0 };
    LpOpts od = qlcg_lp_opts();
    od.jitter = 0.22; od.rand = rand;
    qlcg_lp_dome(b, dc, leaf_r * 0.5, leaf_r * 0.62, leaf_r * 0.5, 5, fol_dark, &od);

    int bud_blobs = qlcg_pa_js_round(qlcg_pa_pnum(def, "blobs", 2.0));
    if (bud_blobs > 2) bud_blobs = 2;
    int buds = 2 + (bud_blobs > 0 ? bud_blobs : 0);
    for (int i = 0; i < buds; i++) {
        double a = (double)i * GOLDEN + qlcg_lp_rng_next(rand);
        double len = leaf_r * (0.55 + qlcg_lp_rng_next(rand) * 0.35);
        double pos[3] = { 0, trunk + leaf_r * (0.34 + qlcg_lp_rng_next(rand) * 0.16), 0 };
        LpOpts o = qlcg_lp_opts();
        o.pitch = 0.45 + qlcg_lp_rng_next(rand) * 0.3; o.cup = 0.42; o.rand = rand;
        qlcg_lp_petal(b, pos, a, len, len * 0.55, (i % 2) ? fol : fol_dark, &o);
    }
    if (qlcg_pa_wants_snow_on(def, "canopy")) {
        double sc[3] = { 0, trunk + leaf_r * 0.5, 0 };
        qlcg_pa_snow_cap(b, def, sc, leaf_r * 0.8, 0.7);
    }
}

static int arch_is(const char *arch, const char *name) {
    return arch && strcmp(arch, name) == 0;
}

static void build_withered(LpBuilder *b, const PdDef *def) {
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    const char *base_arch = def->base_archetype;
    if (!base_arch) base_arch = pd_str(&def->params, "baseArchetype", "");
    double height = qlcg_pa_pnum(def, "height", 4.0);
    LpRgb wood = qlcg_pa_tone(def, "witheredDark", 1.0);
    LpRgb stem = qlcg_pa_tone(def, "withered", 1.0);
    LpRgb dry_leaf = qlcg_pa_tone(def, "witheredStem", 1.0);
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 5.0), 3.0, 8.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.2);

    int low_nong_hua = def->id && height < 1.3 &&
        (strncmp(def->id, "nong.", 5) == 0 || strncmp(def->id, "hua.", 4) == 0);
    int withered_low =
        arch_is(base_arch, "grassClump") || arch_is(base_arch, "waterweed") ||
        arch_is(base_arch, "floatingPad") || arch_is(base_arch, "succulent") ||
        arch_is(base_arch, "orchid") ||
        (height < 1.3 && !arch_is(base_arch, "shrub") &&
         !arch_is(base_arch, "bramble") && !arch_is(base_arch, "bamboo") &&
         !arch_is(base_arch, "reed")) ||
        low_nong_hua;
    int withered_cane = (arch_is(base_arch, "reed") || arch_is(base_arch, "bamboo")) && !withered_low;
    int withered_woody = (arch_is(base_arch, "shrub") || arch_is(base_arch, "bramble")) && !withered_low;

    if (withered_low) {
        double spread = qlcg_pa_pnum(def, "spread", fmax(0.18, height * 0.45));
        double ridge_h = fmin(0.14, fmax(0.025, height * 0.25));
        double ridge_r = fmax(0.06, spread * 0.5);
        double dc[3] = { 0, 0, 0 };
        LpOpts od = qlcg_lp_opts();
        od.floor_on = 0; od.jitter = jitter * 0.3; od.rand = rand;
        qlcg_lp_dome(b, dc, ridge_r, ridge_h * 0.6, ridge_r * 0.9, (sides > 4 ? sides : 4), wood, &od);

        double base_n = qlcg_pa_pnum(def, "blades", qlcg_pa_pnum(def, "blobs", 5.0)) * 0.6;
        int stub_count = qlcg_pa_js_round(base_n);
        if (stub_count < 3) stub_count = 3;
        if (stub_count > 6) stub_count = 6;
        for (int i = 0; i < stub_count; i++) {
            double a = ((double)i / (double)stub_count) * TAU + (qlcg_lp_rng_next(rand) - 0.5) * 0.5;
            double stub_len = fmax(0.05, fmin(spread * 0.45, height * 0.4));
            double stub_w = fmax(0.02, stub_len * 0.28);
            double bx = cos(a) * ridge_r * 0.35;
            double bz = sin(a) * ridge_r * 0.35;
            LpRgb col = (i % 2 == 0) ? stem : dry_leaf;
            double pos[3] = { bx, ridge_h * 0.45, bz };
            LpOpts o = qlcg_lp_opts();
            o.pitch = -0.04 + qlcg_lp_rng_next(rand) * 0.12; o.cup = 0.25; o.rand = rand;
            qlcg_lp_petal(b, pos, a, stub_len, stub_w, col, &o);
        }
        return;
    }

    if (withered_cane) {
        int culms = qlcg_pa_js_round(qlcg_pa_pnum(def, "culms", 4.0));
        if (culms < 2) culms = 2;
        if (culms > 5) culms = 5;
        double spread = qlcg_pa_pnum(def, "spread", height * 0.18);
        double culm_r = qlcg_pa_pnum(def, "trunkRadius", height * 0.025);
        culm_r = fmax(0.02, fmin(0.08, culm_r));
        for (int c = 0; c < culms; c++) {
            double a = ((double)c / (double)culms) * TAU + qlcg_lp_rng_next(rand) * 0.8;
            double rad = spread * (0.2 + qlcg_lp_rng_next(rand) * 0.8);
            double cx = cos(a) * rad;
            double cz = sin(a) * rad;
            double h = height * (0.28 + qlcg_lp_rng_next(rand) * 0.18);
            double lean = (qlcg_lp_rng_next(rand) - 0.5) * 0.12;
            double pc[3] = { cx, 0, cz };
            double dx = lean * h, dy = h;
            double sl = sqrt(dx * dx + dy * dy);
            double dir[3] = { dx / sl, dy / sl, 0.0 };
            LpOpts op = qlcg_lp_opts();
            op.sides = 4; op.tip_ratio = 0.82; op.cap = 1;
            qlcg_lp_limb(b, pc, dir, sl, culm_r, wood, &op);
            double leaf_len = h * 0.45;
            double pos[3] = { cx + lean * h * 0.85, h * 0.85, cz };
            double bearing = a + qlcg_lp_rng_next(rand) * 0.5;
            LpOpts o = qlcg_lp_opts();
            o.pitch = -0.4 + qlcg_lp_rng_next(rand) * 0.2; o.cup = 0.3; o.rand = rand;
            qlcg_lp_petal(b, pos, bearing, leaf_len, leaf_len * 0.2, dry_leaf, &o);
        }
        return;
    }

    if (withered_woody) {
        int stems = qlcg_pa_js_round(qlcg_pa_pnum(def, "stems", 4.0));
        if (stems < 3) stems = 3;
        if (stems > 6) stems = 6;
        double spread = qlcg_pa_pnum(def, "spread", height * 0.4);
        double branch_r = fmax(0.025, fmin(0.06, height * 0.03));
        double tips[6][3];
        for (int i = 0; i < stems; i++) {
            double a = ((double)i / (double)stems) * TAU + qlcg_lp_rng_next(rand) * 0.6;
            double up = 0.25 + qlcg_lp_rng_next(rand) * 0.45;
            double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
            double start[3] = { cos(a) * spread * 0.15, 0.02, sin(a) * spread * 0.15 };
            double len = height * (0.28 + qlcg_lp_rng_next(rand) * 0.18);
            LpOpts ol = qlcg_lp_opts();
            ol.sides = 3; ol.tip_ratio = 0.42; ol.cap = 1;
            qlcg_lp_limb(b, start, dir, len, branch_r, stem, &ol);
            tips[i][0] = start[0] + dir[0] * len * 0.7;
            tips[i][1] = start[1] + dir[1] * len * 0.7;
            tips[i][2] = start[2] + dir[2] * len * 0.7;
        }
        for (int i = 0; i < 3; i++) {
            double a = qlcg_lp_rng_next(rand) * TAU;
            double len = height * 0.12;
            LpOpts o = qlcg_lp_opts();
            o.pitch = 0.1; o.cup = 0.3; o.rand = rand;
            qlcg_lp_petal(b, tips[i % stems], a, len, len * 0.45, dry_leaf, &o);
        }
        return;
    }

    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.42) * 0.42;
    if (trunk > height * 0.52) trunk = height * 0.52;
    double trunk_r = fmax(0.06, qlcg_pa_pnum(def, "trunkRadius", height * 0.04));
    if (trunk_r > height * 0.1) trunk_r = height * 0.1;
    int limbs = qlcg_pa_js_round(qlcg_pa_pnum(def, "limbCount", 4.0));
    if (limbs < 2) limbs = 2;
    if (limbs > 5) limbs = 5;
    double lean = qlcg_pa_pnum(def, "lean", 0.06);
    double off = lean * trunk * 0.2;
    if (fabs(off) > trunk_r * 0.35) off = (off < 0.0 ? -1.0 : 1.0) * trunk_r * 0.35;

    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.jitter = jitter * 0.5; o0.rand = rand; o0.cap = 1;
    qlcg_lp_prism(b, p0, trunk_r * 1.4, trunk_r * 1.4, 0, trunk * 0.3, sides, wood, &o0);
    double p1[3] = { off, trunk * 0.22, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.jitter = jitter; o1.rand = rand; o1.cap = 1; o1.rot_y = 0.55;
    qlcg_lp_prism(b, p1, trunk_r * 1.05, trunk_r * 1.05, 0, trunk * 0.62, sides, stem, &o1);

    double anchors[10][3];
    double angles[10];
    int n_anc = 0;
    for (int i = 0; i < limbs; i++) {
        double a = ((double)i / (double)limbs) * TAU + qlcg_lp_rng_next(rand) * 0.8;
        double up = 0.28 + qlcg_lp_rng_next(rand) * 0.75;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double y = trunk * (0.3 + qlcg_lp_rng_next(rand) * 0.5);
        double len = height * (0.2 + qlcg_lp_rng_next(rand) * 0.2);
        if (len > height * 0.35) len = height * 0.35;
        double start[3] = { off, y, 0 };
        double rad = trunk_r * (0.34 + qlcg_lp_rng_next(rand) * 0.24);
        LpOpts ol = qlcg_lp_opts();
        ol.sides = 3; ol.tip_ratio = 0.42; ol.cap = 1;
        qlcg_lp_limb(b, start, dir, len, rad, stem, &ol);
        if (n_anc < 10) {
            anchors[n_anc][0] = start[0] + dir[0] * len * 0.65;
            anchors[n_anc][1] = start[1] + dir[1] * len * 0.65;
            anchors[n_anc][2] = start[2] + dir[2] * len * 0.65;
            angles[n_anc++] = a;
        }
        if (qlcg_lp_rng_next(rand) < 0.65) {
            double a2 = a + (qlcg_lp_rng_next(rand) - 0.5) * 1.4;
            double up2 = up * (0.6 + qlcg_lp_rng_next(rand) * 0.7);
            double dir2[3] = { cos(a2) * cos(up2), sin(up2), sin(a2) * cos(up2) };
            double len2 = len * (0.35 + qlcg_lp_rng_next(rand) * 0.3);
            double fk[3] = { start[0] + dir[0] * len * 0.85,
                             start[1] + dir[1] * len * 0.85,
                             start[2] + dir[2] * len * 0.85 };
            qlcg_lp_limb(b, fk, dir2, len2, trunk_r * 0.22, wood, &ol);
            if (n_anc < 10) {
                anchors[n_anc][0] = fk[0] + dir2[0] * len2 * 0.6;
                anchors[n_anc][1] = fk[1] + dir2[1] * len2 * 0.6;
                anchors[n_anc][2] = fk[2] + dir2[2] * len2 * 0.6;
                angles[n_anc++] = a2;
            }
        }
    }

    int clumps = qlcg_pa_js_round(qlcg_pa_pnum(def, "blobs", 3.0) * 0.5);
    if (clumps < 2) clumps = 2;
    if (clumps > 5) clumps = 5;
    for (int i = 0; i < clumps; i++) {
        double len = fmin(height * 0.085 * (0.8 + qlcg_lp_rng_next(rand) * 0.5), 0.5);
        double bearing = angles[i % n_anc] + (qlcg_lp_rng_next(rand) - 0.5) * 0.8;
        LpRgb col = qlcg_pa_deepen(dry_leaf, qlcg_lp_rng_next(rand) * 0.3);
        LpOpts o = qlcg_lp_opts();
        o.pitch = -0.25 + qlcg_lp_rng_next(rand) * 0.4; o.cup = 0.5; o.rand = rand;
        qlcg_lp_petal(b, anchors[i % n_anc], bearing, len, len * 0.5, col, &o);
    }
}

void qlcg_plant_hua_zhu(LpBuilder *b, const PdDef *def, int season, int form) {
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
