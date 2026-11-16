/* shu.jinheguan */

#include "shu_model.h"
#include "../../plant_forms.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const ShuShape shapes[] = {
    {1.0, 1.0, 1.0, 0},
    {1.14, 0.80, 1.16, -1},
    {0.87, 1.26, 0.85, 1},
};

static void build_fruit(LpBuilder *b, const PdDef *def, const double point[3],
                        double radius, LpRng *rng) {
    double size[3] = {radius, radius, radius};
    LpOpts opts = qlcg_lp_opts();
    opts.jitter = 0.15; opts.rand = rng; opts.mat = LP_MAT_FRUIT; opts.rings = 1;
    qlcg_lp_ico(b, point, size, 5, qlcg_pa_tone(def, "fruit", 1), &opts);
}

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
    return qlcg_pa_shu_season(def, season, 0.53, 4, 0);
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
    double height = qlcg_pa_pnum(def, "height", 8.0);
    double trunk = qlcg_pa_pnum(def, "trunk", height * 0.52);
    height *= shape->height;
    trunk *= shape->trunk;
    LpRgb bark = qlcg_pa_tone(def, "bark", 1.0);
    LpRgb bark_light = qlcg_pa_tone(def, "barkLight", 1.0);
    double trunk_r = qlcg_pa_pnum(def, "trunkRadius", fmax(0.18, height * 0.055));
    double canopy = qlcg_pa_pnum(def, "canopy", height * 0.5);
    canopy *= shape->crown;
    int sides = qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "sides", 5.0), 3.0, 10.0));
    double jitter = qlcg_pa_pnum(def, "jitter", 0.16);
    int limb_count = qlcg_pa_js_round(qlcg_pa_pnum(def, "limbCount", 4.0)) + shape->branches;
    if (limb_count < 2) limb_count = 2;
    double limb_length = qlcg_pa_pnum(def, "limbLength", trunk * 0.55);
    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    LpRgb fol_dark = qlcg_pa_tone(def, "foliageDark", 1.0);

    double p0[3] = { 0, 0, 0 };
    LpOpts o0 = qlcg_lp_opts();
    o0.jitter = fmin(0.10, jitter * 0.4); o0.rand = rand; o0.radius_ratio = 0.78;
    qlcg_lp_cone(b, p0, trunk_r * 1.35, trunk_r * 1.35, trunk * 0.22, sides, bark, &o0);

    double p1[3] = { 0, trunk * 0.16, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.jitter = fmin(0.10, jitter * 0.5); o1.rand = rand; o1.radius_ratio = 0.76;
    qlcg_lp_cone(b, p1, trunk_r * 0.90, trunk_r * 0.90, trunk * 0.84, sides, bark_light, &o1);

    int bloom_count = qlcg_pa_bloom_count_for(def, qlcg_pa_js_round(qlcg_pa_pnum(def, "bloomCount", 0.0)), 5);
    if (season == PD_SEASON_WINTER || growth.density <= 0.0) bloom_count = 0;
    int bloom_used = 0;
    double b_rad = pd_num(&def->params, "bloomRadius", NAN);
    double limb_starts[16][3];
    double limb_tips[16][3];
    double limb_r[16];
    int saved_limbs = 0;
    for (int i = 0; i < limb_count; i++) {
        double a = ((double)i / (double)limb_count) * TAU + qlcg_lp_rng_next(rand) * 0.5;
        double up = qlcg_pa_pnum(def, "branchAngle", 0.62);
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double y = trunk - trunk_r * 1.4 - qlcg_lp_rng_next(rand) * trunk * 0.12;
        double start[3] = { 0, y, 0 };
        double len = limb_length * (0.85 + qlcg_lp_rng_next(rand) * 0.35);
        double tip[3];
        qlcg_pa_limb_to(b, start, dir, len, trunk_r * 0.55, bark_light, tip);
        double r = (canopy * (0.42 + qlcg_lp_rng_next(rand) * 0.16)) * growth.size;
        if (saved_limbs < 16) {
            for (int k = 0; k < 3; k++) {
                limb_starts[saved_limbs][k] = start[k];
                limb_tips[saved_limbs][k] = tip[k];
            }
            limb_r[saved_limbs] = r;
            saved_limbs++;
        }
        if (growth.density > 0.0) {
            double dc[3] = { tip[0], tip[1] + r * 0.04, tip[2] };
            LpOpts od = qlcg_lp_opts();
            od.jitter = fmin(0.12, jitter * 0.7); od.squash = 0.70; od.rand = rand;
            qlcg_lp_dome(b, dc, r, r * 0.34, r, sides, (i % 2) ? fol : fol_dark, &od);
        }
        if (bloom_used < bloom_count) {
            double fr = (!isnan(b_rad) && isfinite(b_rad) && b_rad > 0.0)
                ? qlcg_pa_clampd(b_rad, 0.03, 0.6)
                : qlcg_pa_clampd(r * 0.22, 0.04, 0.4);
            double pstart[3] = {
                start[0] + (tip[0] - start[0]) * 0.88,
                start[1] + (tip[1] - start[1]) * 0.88,
                start[2] + (tip[2] - start[2]) * 0.88
            };
            double pbase[3] = { tip[0], tip[1] + r * 0.24, tip[2] };
            double sd[3] = { pbase[0] - pstart[0], pbase[1] - pstart[1], pbase[2] - pstart[2] };
            double slen = sqrt(sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2]);
            double srad = fmax(0.016, trunk_r * 0.10);
            double fc[3] = { tip[0], tip[1] + r * 0.32, tip[2] };
            qlcg_pa_limb_to(b, pstart, sd, slen, srad, bark_light, NULL);
            qlcg_pa_limb_to(b, (double[]){ tip[0], tip[1] + r * 0.22, tip[2] },
                            (double[]){ 0, 1, 0 }, r * 0.10 + fr * 0.35, srad, bark_light, NULL);
            qlcg_pa_flower_at(b, def, fc, fr, rand);
            bloom_used++;
        }
        if (qlcg_pa_has_snow_target(def, "canopy")) {
            double sc[3] = { tip[0], tip[1] + r * 0.3, tip[2] };
            qlcg_lp_socket(b, sc, LP_SOCKET_SNOW_CANOPY);
        }
    }
    double top_r = (canopy * 0.6) * growth.size;
    if (growth.density > 0.0) {
        double tc[3] = { 0, trunk + top_r * 0.04, 0 };
        LpOpts ot = qlcg_lp_opts();
        ot.jitter = fmin(0.12, jitter * 0.6); ot.squash = 0.85; ot.rand = rand;
        qlcg_lp_dome(b, tc, top_r, top_r * 0.32, top_r, sides, fol, &ot);
        if (bloom_used < bloom_count) {
            double fr = (!isnan(b_rad) && isfinite(b_rad) && b_rad > 0.0)
                ? qlcg_pa_clampd(b_rad, 0.03, 0.6)
                : qlcg_pa_clampd(top_r * 0.22, 0.04, 0.4);
            double fc[3] = { 0, trunk + top_r * 0.31, 0 };
            qlcg_pa_limb_to(b, (double[]){ 0, trunk - top_r * 0.04, 0 },
                            (double[]){ 0, 1, 0 }, top_r * 0.35 + fr * 0.35,
                            fmax(0.016, trunk_r * 0.10), bark_light, NULL);
            qlcg_pa_flower_at(b, def, fc, fr, rand);
        }
        int leaf_base = qlcg_pa_js_round(qlcg_pa_pnum(def, "leafPetals",
                                         (double)qlcg_pa_js_round((double)limb_count * 3.0)));
        int leaf_n = qlcg_pa_js_round((double)leaf_base * growth.density);
        for (int i = 0; i < leaf_n; i++) {
            double a = (double)i * GOLDEN + (double)def->build_seed * 0.0011 + qlcg_lp_rng_next(rand) * 0.5;
            double rad_scale = 0.35 + 0.12 * qlcg_lp_rng_next(rand);
            double pos[3];
            double rk;
            if (i % 4 == 0 || saved_limbs == 0) {
                rk = top_r;
                pos[0] = cos(a) * rk * rad_scale;
                pos[1] = trunk + rk * (0.06 + (double)(i % 3) * 0.025);
                pos[2] = sin(a) * rk * rad_scale;
            } else {
                int k = i % saved_limbs;
                rk = limb_r[k];
                pos[0] = limb_tips[k][0] + cos(a) * rk * rad_scale;
                pos[1] = limb_tips[k][1] + rk * (0.06 + (double)(i % 3) * 0.025);
                pos[2] = limb_tips[k][2] + sin(a) * rk * rad_scale;
            }
            double plen = fmax(0.32, rk * (0.80 + 0.16 * qlcg_lp_rng_next(rand)));
            LpRgb col = qlcg_pa_deepen((i % 2) ? fol : fol_dark, qlcg_lp_rng_next(rand) * 0.25);
            LpOpts ol = qlcg_lp_opts();
            ol.pitch = 0.08 + (double)(i % 3) * 0.05; ol.cup = 0.28; ol.rand = rand;
            qlcg_lp_petal(b, pos, a, plen, plen * 0.36, col, &ol);
        }
    }

    int fruit_n = qlcg_pa_fruit_count_for(def, qlcg_pa_js_round(qlcg_pa_pnum(def, "fruitCount", 0.0)), 3);
    if (fruit_n > 0 && saved_limbs > 0
            && (def->state == PD_STATE_FRUITING || def->state == PD_STATE_NORMAL)) {
        double efr = pd_num(&def->params, "fruitRadius", NAN);
        double fr = (!isnan(efr) && isfinite(efr) && efr > 0.0)
            ? qlcg_pa_clampd(efr, 0.02, 0.5)
            : qlcg_pa_clampd(canopy * 0.08, 0.035, 0.24);
        double drop = fmax(0.06, fr * 1.45);
        for (int i = 0; i < fruit_n; i++) {
            int k = i % saved_limbs;
            int slot = i / saved_limbs;
            double u = 0.58 + 0.14 * (double)(slot % 3) + 0.06 * qlcg_lp_rng_next(rand);
            double anchor[3] = {
                limb_starts[k][0] + (limb_tips[k][0] - limb_starts[k][0]) * u,
                limb_starts[k][1] + (limb_tips[k][1] - limb_starts[k][1]) * u,
                limb_starts[k][2] + (limb_tips[k][2] - limb_starts[k][2]) * u
            };
            double pos[3] = { anchor[0], anchor[1] - drop, anchor[2] };
            qlcg_lp_socket(b, pos, LP_SOCKET_FRUIT);
            if (def->state == PD_STATE_FRUITING) {
                qlcg_pa_limb_to(b, anchor, (double[]){ 0, -1, 0 }, drop,
                                fmax(0.014, trunk_r * 0.08), bark_light, NULL);
                build_fruit(b, def, pos, fr, rand);
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
    o0.rand = rand; o0.jitter = 0.1; o0.cap = 1.0;
    qlcg_lp_prism(b, p0, stem_r * 1.3, stem_r * 1.3, 0, trunk * 0.5, 4, stem, &o0);
    double p1[3] = { 0, trunk * 0.44, 0 };
    LpOpts o1 = qlcg_lp_opts();
    o1.rand = rand; o1.jitter = 0.1; o1.cap = 1.0;
    qlcg_lp_prism(b, p1, stem_r, stem_r, 0, trunk * 0.60, 4, stem, &o1);

    for (int i = 0; i < 2; i++) {
        double a = (double)i * PI + qlcg_lp_rng_next(rand) * 0.5;
        double pos[3] = { cos(a) * stem_r * 0.45, trunk * (0.54 + (double)i * 0.04), sin(a) * stem_r * 0.45 };

        double plen = leaf_r * (0.95 + qlcg_lp_rng_next(rand) * 0.3);
        LpOpts o = qlcg_lp_opts();
        o.pitch = 0.02 + (double)i * 0.10 + qlcg_lp_rng_next(rand) * 0.14; o.cup = 0.35; o.rand = rand;
        qlcg_lp_petal(b, pos, a, plen, leaf_r * 0.75, i ? fol : fol_dark, &o);
    }
    for (int i = 0; i < 4; i++) {
        double a = (double)i * GOLDEN + (double)def->build_seed * 0.0017 + qlcg_lp_rng_next(rand) * 0.4;
        double len = leaf_r * (0.7 + qlcg_lp_rng_next(rand) * 0.4);
        double pos[3] = { cos(a) * stem_r * 0.45, trunk * (0.66 + (double)i * 0.06), sin(a) * stem_r * 0.45 };
        LpOpts o = qlcg_lp_opts();
        o.pitch = 0.14 + (double)(i % 2) * 0.09 + qlcg_lp_rng_next(rand) * 0.22; o.cup = 0.4; o.rand = rand;
        qlcg_lp_petal(b, pos, a, len, len * 0.52, (i % 2) ? fol : fol_dark, &o);
    }
    double dc[3] = { 0, trunk + leaf_r * 0.22, 0 };
    LpOpts od = qlcg_lp_opts();
    od.jitter = 0.22; od.rand = rand;
    qlcg_lp_dome(b, dc, leaf_r * 0.5, leaf_r * 0.62, leaf_r * 0.5, 5, fol_dark, &od);

    int bud_blobs = qlcg_pa_js_round(qlcg_pa_pnum(def, "blobs", 2.0));
    if (bud_blobs > 2) bud_blobs = 2;
    int buds = 2 + (bud_blobs > 0 ? bud_blobs : 0);
    for (int i = 0; i < buds; i++) {
        double a = (double)i * GOLDEN + qlcg_lp_rng_next(rand);
        double len = leaf_r * (0.55 + qlcg_lp_rng_next(rand) * 0.35);
        double pos[3] = { 0, dc[1] + leaf_r * (0.14 + (double)i * 0.04), 0 };
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
    o0.jitter = jitter * 0.4; o0.rand = rand; o0.cap = 1.0;
    qlcg_lp_prism(b, p0, trunk_r * 1.4, trunk_r * 1.4, 0, trunk * 0.3, sides, wood, &o0);
    double p1_end[3] = { lean * trunk * 0.2, trunk * 0.84, 0 };
    double p1_start[3] = { 0, trunk * 0.24, 0 };
    double p1_d[3] = { p1_end[0] - p1_start[0], p1_end[1] - p1_start[1], 0.0 };
    double p1_len = sqrt(p1_d[0] * p1_d[0] + p1_d[1] * p1_d[1]);
    double p1_dir[3] = { (p1_len > 1e-6) ? p1_d[0] / p1_len : 0.0,
                         (p1_len > 1e-6) ? p1_d[1] / p1_len : 1.0, 0.0 };
    if (p1_len < 1e-6) p1_len = 1e-6;
    LpOpts o1 = qlcg_lp_opts();
    o1.sides = sides; o1.tip_ratio = 0.82; o1.cap = 1.0;
    qlcg_lp_limb(b, p1_start, p1_dir, p1_len, trunk_r * 1.05, stem, &o1);

    double attach[5][3];
    for (int i = 0; i < limbs; i++) {
        double a = ((double)i / (double)limbs) * TAU + qlcg_lp_rng_next(rand) * 0.8;
        double up = 0.28 + qlcg_lp_rng_next(rand) * 0.75;
        double dir[3] = { cos(a) * cos(up), sin(up), sin(a) * cos(up) };
        double y = trunk * (0.32 + qlcg_lp_rng_next(rand) * 0.48);
        double frac_y = (trunk > 1e-6) ? (y - trunk * 0.24) / (trunk * 0.60) : 0.0;
        double len = height * (0.2 + qlcg_lp_rng_next(rand) * 0.2);
        if (len > height * 0.35) len = height * 0.35;
        double start[3] = { lean * trunk * 0.2 * frac_y, y, 0 };
        double rad = trunk_r * (0.34 + qlcg_lp_rng_next(rand) * 0.24);
        double tip[3];
        qlcg_pa_limb_to(b, start, dir, len, rad, stem, tip);
        for (int k = 0; k < 3; k++) attach[i][k] = start[k] + (tip[k] - start[k]) * 0.85;
        if (qlcg_lp_rng_next(rand) < 0.65) {
            double mid_back[3];
            for (int k = 0; k < 3; k++) mid_back[k] = start[k] + (tip[k] - start[k]) * 0.86;
            double a2 = a + (qlcg_lp_rng_next(rand) - 0.5) * 1.4;
            double up2 = up * (0.6 + qlcg_lp_rng_next(rand) * 0.7);
            double dir2[3] = { cos(a2) * cos(up2), sin(up2), sin(a2) * cos(up2) };
            double len2 = len * (0.38 + qlcg_lp_rng_next(rand) * 0.3);
            double tip2[3];
            qlcg_pa_limb_to(b, mid_back, dir2, len2, rad * 0.62, wood, tip2);
            for (int k = 0; k < 3; k++) attach[i][k] = mid_back[k] + (tip2[k] - mid_back[k]) * 0.78;
        }
    }

    int clumps = qlcg_pa_js_round(qlcg_pa_pnum(def, "blobs", 3.0) * 0.5);
    if (clumps < 2) clumps = 2;
    if (clumps > 5) clumps = 5;
    for (int i = 0; i < clumps; i++) {
        double a = (double)i * GOLDEN + (double)def->build_seed * 0.0017;
        double len = fmin(height * 0.085 * (0.8 + qlcg_lp_rng_next(rand) * 0.5), 0.5);
        const double *ap = attach[i % limbs];
        double pos[3] = { ap[0], ap[1], ap[2] };
        double bearing = a + (qlcg_lp_rng_next(rand) - 0.5) * 0.8;
        LpRgb col = qlcg_pa_deepen(dry_leaf, qlcg_lp_rng_next(rand) * 0.3);
        LpOpts o = qlcg_lp_opts();
        o.pitch = -0.25 + (double)i * 0.11 + qlcg_lp_rng_next(rand) * 0.16; o.cup = 0.5; o.rand = rand;
        qlcg_lp_petal(b, pos, bearing, len, len * 0.5, col, &o);
    }
}

void qlcg_plant_shu_jinheguan(LpBuilder *b, const PdDef *def, int season, int form) {
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
