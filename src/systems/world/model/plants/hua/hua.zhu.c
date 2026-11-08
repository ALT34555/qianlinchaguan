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
    int blades = qlcg_pa_js_round(qlcg_pa_pnum(def, "blades", 12.0) * density[season]);
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
        for (int i = 0; i < nodes; i++) {
            double t = (double)i / (double)nodes;
            double y = (double)i * node_h;
            double nx = x0 + lean * y * 0.35 * (0.4 + t);
            double rr = r * (1.0 - t * 0.2);
            double pc[3] = { nx, y, z0 };
            LpOpts o = qlcg_lp_opts();
            o.jitter = jitter * 0.4; o.rand = rand; o.cap = 0;
            qlcg_lp_prism(b, pc, rr, rr, 0, node_h * 0.86, sides, stem, &o);
            double nc[3] = { nx, y + node_h * 0.84, z0 };
            LpOpts on = qlcg_lp_opts();
            on.radius_ratio = 0.8; on.jitter = 0.05; on.rand = rand;
            qlcg_lp_cone(b, nc, rr * 1.36, rr * 1.36, node_h * 0.24, sides, node, &on);
        }
        if (tops && top_count < culms) {
            tops[top_count][0] = x0 + lean * h * 0.35 * 1.4;
            tops[top_count][1] = h;
            tops[top_count][2] = z0;
            top_count++;
        }
    }
    for (int i = 0; i < blades; i++) {
        if (top_count <= 0) break;
        int pick = (int)floor(qlcg_lp_rng_next(rand) * (double)top_count);
        if (pick >= top_count) pick = top_count - 1;
        if (pick < 0) pick = 0;
        double a = qlcg_lp_rng_next(rand) * TAU;
        double len = leaf_len * (0.45 + qlcg_lp_rng_next(rand) * 0.35);
        LpRgb col = (i % 2) ? leaf : leaf_dark;
        double drop = leaf_len * (0.05 + (double)(i % 3) * 0.5) + qlcg_lp_rng_next(rand) * leaf_len * 0.2;
        double start[3] = { tops[pick][0], tops[pick][1] - drop, tops[pick][2] };
        qlcg_pa_bent_leaf(b, start, a, len, leaf_len * 0.15, len * 0.24,
                  len * (0.5 + qlcg_lp_rng_next(rand) * 0.4), col, rand, LP_MAT_SOLID);
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
    for (int i = 0; i < top_count && i < seeds; i++)
        qlcg_pa_hua_seed(b, tops[i], &bamboo_seed, shape->spread, def->state == PD_STATE_FRUITING);
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
    o0.rand = rand; o0.jitter = 0.1; o0.cap = 0;
    qlcg_lp_prism(b, p0, stem_r * 1.3, stem_r * 1.3, 0, trunk * 0.5, 4, stem, &o0);
    double p1[3] = { 0, trunk * 0.46, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.rand = rand; o1.jitter = 0.12; o1.cap = 0;
    qlcg_lp_prism(b, p1, stem_r, stem_r, 0, trunk * 0.56, 4, stem, &o1);

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
            LpOpts op = qlcg_lp_opts();
            op.rand = rand; op.jitter = 0.1; op.cap = 0;
            qlcg_lp_prism(b, pc, culm_r, culm_r, 0, h, 4, wood, &op);
            double leaf_len = h * 0.45;
            double pos[3] = { cx + lean * h, h * 0.85, cz };
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
        for (int i = 0; i < stems; i++) {
            double a = ((double)i / (double)stems) * TAU + qlcg_lp_rng_next(rand) * 0.6;
            double up = 0.25 + qlcg_lp_rng_next(rand) * 0.45;
            double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
            double start[3] = { cos(a) * spread * 0.15, 0.02, sin(a) * spread * 0.15 };
            double len = height * (0.28 + qlcg_lp_rng_next(rand) * 0.18);
            qlcg_pa_limb_to(b, start, dir, len, branch_r, stem, NULL);
        }
        for (int i = 0; i < 3; i++) {
            double a = qlcg_lp_rng_next(rand) * TAU;
            double rad = spread * 0.3;
            double len = height * 0.12;
            double pos[3] = { cos(a) * rad, 0.04, sin(a) * rad };
            LpOpts o = qlcg_lp_opts();
            o.pitch = 0.1; o.cup = 0.3; o.rand = rand;
            qlcg_lp_petal(b, pos, a, len, len * 0.45, dry_leaf, &o);
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

    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.jitter = jitter * 0.5; o0.rand = rand; o0.cap = 0;
    qlcg_lp_prism(b, p0, trunk_r * 1.4, trunk_r * 1.4, 0, trunk * 0.3, sides, wood, &o0);
    double p1[3] = { lean * trunk * 0.2, trunk * 0.22, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.jitter = jitter; o1.rand = rand; o1.cap = 0;
    qlcg_lp_prism(b, p1, trunk_r * 1.05, trunk_r * 1.05, 0, trunk * 0.62, sides, stem, &o1);

    for (int i = 0; i < limbs; i++) {
        double a = ((double)i / (double)limbs) * TAU + qlcg_lp_rng_next(rand) * 0.8;
        double up = 0.28 + qlcg_lp_rng_next(rand) * 0.75;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double y = trunk * (0.3 + qlcg_lp_rng_next(rand) * 0.5);
        double len = height * (0.2 + qlcg_lp_rng_next(rand) * 0.2);
        if (len > height * 0.35) len = height * 0.35;
        double start[3] = { lean * trunk * 0.2, y, 0 };
        double rad = trunk_r * (0.34 + qlcg_lp_rng_next(rand) * 0.24);
        double tip[3];
        qlcg_pa_limb_to(b, start, dir, len, rad, stem, tip);
        if (qlcg_lp_rng_next(rand) < 0.65) {
            double a2 = a + (qlcg_lp_rng_next(rand) - 0.5) * 1.4;
            double up2 = up * (0.6 + qlcg_lp_rng_next(rand) * 0.7);
            double dir2[3] = { cos(a2) * cos(up2), sin(up2), sin(a2) * cos(up2) };
            double len2 = len * (0.35 + qlcg_lp_rng_next(rand) * 0.3);
            qlcg_pa_limb_to(b, tip, dir2, len2, trunk_r * 0.22, wood, NULL);
        }
    }

    int clumps = qlcg_pa_js_round(qlcg_pa_pnum(def, "blobs", 3.0) * 0.5);
    if (clumps < 2) clumps = 2;
    if (clumps > 5) clumps = 5;
    for (int i = 0; i < clumps; i++) {
        double a = (double)i * GOLDEN + (double)def->build_seed * 0.0017;
        double rad = fmin(height * (0.12 + qlcg_lp_rng_next(rand) * 0.22), trunk_r * 3.0);
        double len = fmin(height * 0.085 * (0.8 + qlcg_lp_rng_next(rand) * 0.5), 0.5);
        double pos[3] = { cos(a) * rad, trunk * (0.6 + qlcg_lp_rng_next(rand) * 0.6), sin(a) * rad };
        double bearing = a + (qlcg_lp_rng_next(rand) - 0.5) * 0.8;
        LpRgb col = qlcg_pa_deepen(dry_leaf, qlcg_lp_rng_next(rand) * 0.3);
        LpOpts o = qlcg_lp_opts();
        o.pitch = -0.25 + qlcg_lp_rng_next(rand) * 0.4; o.cup = 0.5; o.rand = rand;
        qlcg_lp_petal(b, pos, bearing, len, len * 0.5, col, &o);
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
