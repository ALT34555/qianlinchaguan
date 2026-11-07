/* shu.binglang */

#include "shu_model.h"
#include "../../plant_forms.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const ShuShape shapes[] = {
    {1.0, 1.0, 1.0, 0},
    {1.17, 0.79, 1.17, -1},
    {0.89, 1.25, 0.89, 1},
};

static LpRgb pick_color(const PdDict *pal, const char *k1, const char *k2, const char *fallback) {
    const char *v = k1 ? pd_str(pal, k1, NULL) : NULL;
    if (!v && k2) v = pd_str(pal, k2, NULL);
    if (!v) v = fallback;
    return qlcg_lp_hex(v, 255, 0, 255);
}

static double num_or(const PdDict *d, const char *key, double dflt) {
    double v = pd_num(d, key, NAN);
    return isnan(v) ? dflt : v;
}

static ShuSeason season_growth(const PdDef *def, int season) {
    return qlcg_pa_shu_season(def, season, 0.6, 4, 1);
}

static void build_simple(LpBuilder *b, const PdDef *def, int season) {
    const PdDict *p = &def->params;
    double h = fmax(0.1, num_or(p, "height", num_or(p, "trunk", 1.0)));
    double r = fmax(0.05, num_or(p, "canopy", num_or(p, "spread", h * 0.3))) * season_growth(def, season).size;

    LpRgb leaf = qlcg_lp_wind_color(pick_color(&def->palette, "foliage", "waterweed", "#50843b"), LP_WIND_LEAF);
    LpRgb bark = qlcg_lp_wind_color(pick_color(&def->palette, "bark", "barkLight", "#725338"), LP_WIND_BARK);

    double c1[3] = { 0, 0, 0 };
    qlcg_lp_prism(b, c1, h * 0.04, h * 0.04, 0, h * 0.5, 6, bark, NULL);
    double c2[3] = { 0, h * 0.28, 0 };
    LpOpts oc = qlcg_lp_opts(); oc.cap = 1;
    qlcg_lp_cone(b, c2, r, r, h * 0.72, 6, leaf, &oc);
    if (qlcg_pa_has_snow_target(def, "canopy") || qlcg_pa_has_snow_target(def, "top")) {
        double c3[3] = { 0, h * 0.62, 0 };
        qlcg_lp_socket(b, c3, LP_SOCKET_SNOW_CANOPY);
    }
}

static void build_adult(LpBuilder *b, const PdDef *def, int season) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    ShuSeason growth = season_growth(def, season);
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    double height = qlcg_pa_pnum(def, "height", 11.0);
    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.86);
    height *= shape->height;
    trunk *= shape->trunk;
    LpRgb bark = qlcg_pa_tone(def, "bark", 1.0);
    LpRgb bark_light = qlcg_pa_tone(def, "barkLight", 1.0);
    double trunk_r = qlcg_pa_pnum(def, "trunkRadius", fmax(0.16, height * 0.026));
    double lean = qlcg_pa_pnum(def, "lean", 0.16);
    int fronds = qlcg_pa_js_round(qlcg_pa_pnum(def, "fronds", 9.0)) + shape->branches;
    if (fronds < 4) fronds = 4;
    int frond_slots = fronds;
    fronds = qlcg_pa_js_round(fronds * growth.density);
    if (fronds < 2) fronds = 2;
    double frond_length = qlcg_pa_pnum(def, "frondLength", height * 0.42) * 0.88 * growth.size;
    frond_length *= shape->crown;
    double droop = qlcg_pa_clampd(qlcg_pa_pnum(def, "droop", 0.55) + growth.droop + (-0.13) + (shape->crown - 1.0) * 0.45, 0.0, 1.0);
    int segments = qlcg_pa_js_round(qlcg_pa_pnum(def, "segments", 7.0));
    if (segments > 5) segments = 5;
    if (segments < 1) segments = 1;
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 5.0), 3.0, 10.0));
    LpRgb leaf = qlcg_pa_tone(def, "palmFrond", 1.0);
    LpRgb leaf_dark = qlcg_pa_tone(def, "foliageDark", 1.0);

    int nodes = qlcg_pa_js_round(trunk / 1.1);
    if (nodes < 3) nodes = 3;
    double x = 0.0;
    double node_h = trunk / (double)nodes;
    for (int i = 0; i < nodes; i++) {
        double y = (double)i * node_h;
        double t = (double)i / (double)(nodes - 1 > 0 ? nodes - 1 : 1);
        double nr = trunk_r * (1.05 - 0.28 * t);
        double pc[3] = { x, y, 0 };
        LpOpts o = qlcg_lp_opts();
        o.jitter = 0.06; o.rand = rand; o.cap = 0;
        qlcg_lp_prism(b, pc, nr * 1.08, nr * 1.08, 0, node_h * 1.02, sides,
                      (i % 2) ? bark : bark_light, &o);
        x += lean * node_h * (0.35 + t * 0.9);
    }
    double crown[3] = { x, trunk, 0 };
    LpOpts oc = qlcg_lp_opts();
    oc.rand = rand;
    qlcg_lp_prism(b, crown, trunk_r * 0.72, trunk_r * 0.72, -0.02, trunk_r * 1.9, sides, leaf_dark, &oc);

    for (int f = 0; f < fronds; f++) {
        double a = ((double)f / (double)frond_slots) * TAU + qlcg_lp_rng_next(rand) * 0.25;
        double dx = cos(a), dz = sin(a);
        double seg_len = frond_length / (double)segments;
        double start_y = crown[1] + trunk_r * 1.6 + qlcg_lp_rng_next(rand) * 0.18;
        double px = crown[0], py = start_y, pz = crown[2];
        double r0 = fmax(0.06, trunk_r * 0.34);
        for (int i = 0; i < segments; i++) {
            double t = (double)(i + 1) / (double)segments;
            double lift = sin(((double)i / (double)segments) * PI * 0.55) * 0.55;
            double sag = -droop * pow(t, 1.7) * 2.1;
            double ny = py + (lift + sag) * seg_len * 0.62;
            double nx = px + dx * seg_len * 0.98;
            double nz = pz + dz * seg_len * 0.98;
            double w = r0 * (1.0 - t * 0.55) * 1.9 * growth.size;
            LpRgb col = qlcg_pa_deepen((i % 2) ? leaf : leaf_dark, 0.25 * t);
            double side_x = -dz, side_z = dx;
            double mid[3] = { (px + nx) / 2.0, (py + ny) / 2.0, (pz + nz) / 2.0 };
            double tip[3] = { nx, ny, nz };
            double q1[3] = { px + side_x * w, py, pz + side_z * w };
            double q2[3] = { px - side_x * w, py, pz - side_z * w };
            double q3[3] = { mid[0] - side_x * w * 0.8, mid[1] + w * 0.35, mid[2] - side_z * w * 0.8 };
            double q4[3] = { mid[0] + side_x * w * 0.8, mid[1] + w * 0.35, mid[2] + side_z * w * 0.8 };
            qlcg_lp_quad(b, q1, q2, q3, q4, col, LP_MAT_SOLID);
            double s1[3] = { mid[0] + side_x * w * 0.8, mid[1] + w * 0.35, mid[2] + side_z * w * 0.8 };
            double s2[3] = { mid[0] - side_x * w * 0.8, mid[1] + w * 0.35, mid[2] - side_z * w * 0.8 };
            double s3[3] = { tip[0] - side_x * w * 0.34, tip[1], tip[2] - side_z * w * 0.34 };
            double s4[3] = { tip[0] + side_x * w * 0.34, tip[1], tip[2] + side_z * w * 0.34 };
            qlcg_lp_quad(b, s1, s2, s3, s4, col, LP_MAT_SOLID);
            LpOpts leaflet = qlcg_lp_opts();
            leaflet.pitch = -.14 - droop * .32;
            leaflet.cup = .22;
            for (int side = -1; side <= 1; side += 2) {
                qlcg_lp_petal(b, mid, a + side * 1.03,
                              seg_len * 1.07 * (1.10 - .50 * t), seg_len * .19, col, &leaflet);
            }
            px = nx; py = ny; pz = nz;
        }
        for (int s = 0; s < 2; s++) {
            double sgn = (s == 0) ? 1.0 : -1.0;
            double pc[3] = { px, py, pz };
            LpOpts o = qlcg_lp_opts();
            o.pitch = -0.3 - droop * 0.35; o.cup = 0.3; o.rand = rand;
            qlcg_lp_petal(b, pc, a + sgn * 0.34, seg_len * 0.8, seg_len * 0.24, qlcg_pa_deepen(leaf, 0.25), &o);
        }
        if (qlcg_pa_has_snow_target(def, "canopy") && (f % 2 == 0)) {
            double sc[3] = { crown[0] + dx * frond_length * 0.25,
                             crown[1] + trunk_r * 2.1,
                             crown[2] + dz * frond_length * 0.25 };
            qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
        }
    }

    int bloom_n = qlcg_pa_bloom_count_for(def, qlcg_pa_js_round(qlcg_pa_pnum(def, "bloomCount", 0.0)), 5);
    if (bloom_n > 0) {
        double bc[3] = { crown[0], crown[1] + trunk_r * 1.2, crown[2] };
        qlcg_pa_bloom_cluster(b, def, bc, frond_length * 0.4, bloom_n, rand, frond_length * 0.06, trunk_r * 1.6);
    }
    int fruit_n = qlcg_pa_fruit_count_for(def, qlcg_pa_js_round(qlcg_pa_pnum(def, "fruitCount", 0.0)), 3);
    if (fruit_n > 0) {
        LpRgb col = qlcg_pa_tone(def, "fruit", 1.0);
        for (int i = 0; i < fruit_n; i++) {
            double a = ((double)i / (double)fruit_n) * TAU + qlcg_lp_rng_next(rand);
            double pos[3] = { crown[0] + cos(a) * trunk_r * 1.5,
                              crown[1] + trunk_r * 0.4,
                              crown[2] + sin(a) * trunk_r * 1.5 };
            qlcg_lp_socket(b, pos, LP_SOCKET_FRUIT);
            if (def->state == PD_STATE_FRUITING) {
                double rad3[3] = { trunk_r * 0.55, trunk_r * 0.55, trunk_r * 0.55 };
                LpOpts o = qlcg_lp_opts();
                o.jitter = 0.12; o.rand = rand; o.mat = LP_MAT_FRUIT; o.rings = 1;
                qlcg_lp_ico(b, pos, rad3, 5, col, &o);
            }
        }
    }
    if (qlcg_pa_has_snow_target(def, "ground")) {
        double origin[3] = {0, 0, 0};
        qlcg_lp_socket(b, origin, LP_SOCKET_SNOW_GROUND);
    }
}

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
    if (qlcg_pa_has_snow_target(def, "canopy")) {
        double sc[3] = { 0, trunk + leaf_r * 0.5, 0 };
        qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
    }
}

static int arch_is(const char *arch, const char *name) {
    return arch && strcmp(arch, name) == 0;
}

static void build_withered(LpBuilder *b, const PdDef *def) {
    const ShuShape *shape = &shapes[qlcg_pa_shape_variant(def)];
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    LpRng *rand = &rng;
    const char *base_arch = def->base_archetype;
    if (!base_arch) base_arch = pd_str(&def->params, "baseArchetype", "");
    double height = qlcg_pa_pnum(def, "height", 4.0);
    height *= shape->height;
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

void qlcg_plant_shu_binglang(LpBuilder *b, const PdDef *def, int season, int form) {
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
