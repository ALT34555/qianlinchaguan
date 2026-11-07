/* 植物共用几何构件 */
#include "plant_arch_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* --------- 参数与颜色 --------- */

double qlcg_pa_pnum(const PdDef *def, const char *key, double dflt) {
    double v = pd_num(&def->params, key, NAN);
    return isnan(v) ? dflt : v;
}

int qlcg_pa_js_round(double v) { return (int)floor(v + 0.5); }

double qlcg_pa_clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static double clamp_color(double v) { return qlcg_pa_clampd(v, 0.0, 255.0); }

static const char *const WITHEBLE_KEYS[] = {
    "foliage", "foliageDark", "foliageDeep", "canopy", "conifer", "coniferDark", "coniferDeep",
    "larch", "jungle", "jungleDark", "jungleDeep", "palmFrond", "bamboo", "bambooCulm",
    "tea", "mulberry", "cropGreen", "riceGreen", "cropGold", "sorghum",
};

static int str_in(const char *const *list, int n, const char *v) {
    if (!v) return 0;
    for (int i = 0; i < n; i++) if (list[i] && strcmp(list[i], v) == 0) return 1;
    return 0;
}

static const char *wither_key(const char *key) {
    if (!key) return "withered";
    if (strcmp(key, "foliageDark") == 0 || strcmp(key, "foliageDeep") == 0 ||
        strcmp(key, "coniferDark") == 0 || strcmp(key, "coniferDeep") == 0 ||
        strcmp(key, "jungleDark") == 0 || strcmp(key, "jungleDeep") == 0) return "witheredDark";
    if (strcmp(key, "conifer") == 0 || strcmp(key, "larch") == 0) return "witheredNeedle";
    if (strcmp(key, "cropGold") == 0 || strcmp(key, "sorghum") == 0) return "withered";
    return "withered";
}

LpRgb qlcg_pa_tone(const PdDef *def, const char *key, double scale) {
    const char *base = NULL;
    if (key && strcmp(key, "bloom") == 0) {
        base = def->has_bloom_color ? def->bloom_color : pd_str(&def->palette, "bloom", NULL);
        if (!base) base = "#a8456b";
    } else if (key && def->state == PD_STATE_WITHERED &&
               str_in(WITHEBLE_KEYS, (int)(sizeof(WITHEBLE_KEYS) / sizeof(WITHEBLE_KEYS[0])), key)) {
        base = pd_str(&def->palette, wither_key(key), NULL);
        if (!base) base = pd_str(&def->palette, key, NULL);
    } else {
        base = key ? pd_str(&def->palette, key, NULL) : NULL;
    }
    LpRgb c = qlcg_lp_hex(base ? base : "#ff00ff", 255, 0, 255);
    c.r = clamp_color(c.r * scale);
    c.g = clamp_color(c.g * scale);
    c.b = clamp_color(c.b * scale);
    return qlcg_lp_wind_color(c, qlcg_lp_palette_wind(key));
}

LpRgb qlcg_pa_deepen(LpRgb c, double depth) {
    if (depth <= 0.01) return c;
    double k = 1.0 + (0.52 - 1.0) * qlcg_pa_clampd(depth, 0.0, 1.0);
    LpRgb o = c;
    o.r = clamp_color(c.r * k);
    o.g = clamp_color(c.g * k);
    o.b = clamp_color(c.b * k);
    return o;
}

/* --------- 雪 --------- */

int qlcg_pa_wants_snow_on(const PdDef *def, const char *target) {
    return def->snow && qlcg_pa_has_snow_target(def, target);
}

int qlcg_pa_has_snow_target(const PdDef *def, const char *target) {
    if (!(qlcg_pa_pnum(def, "snow", 0.0) > 0.01)) return 0;
    const char *list = pd_str(&def->params, "snowOn", "canopy");
    if (!list) return 0;
    size_t n = strlen(target);
    const char *p = list;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len == n && strncmp(p, target, n) == 0) return 1;
        if (!comma) break;
        p = comma + 1;
    }
    return 0;
}

LpRgb qlcg_pa_snow_color(const PdDef *def) {
    return qlcg_pa_tone(def, "snow", 1.0);
}

void qlcg_pa_snow_cap(LpBuilder *b, const PdDef *def, const double center[3], double r, double y_scale) {
    double thickness = qlcg_pa_pnum(def, "snow", 0.18);
    LpRgb col = qlcg_pa_snow_color(def);
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed ^ 0x51ed270bu);
    double c[3] = { center[0], center[1] + r * 0.42 + thickness * 0.35, center[2] };
    LpOpts o = qlcg_lp_opts();
    o.floor_on = 0; o.jitter = 0.1; o.rand = &rng; o.mat = LP_MAT_SNOW;
    qlcg_lp_dome(b, c, r * 0.86, r * 0.5 * y_scale, r * 0.86, 5, col, &o);
}

/* --------- 数量推导 --------- */

int qlcg_pa_bloom_count_for(const PdDef *def, int requested, int fallback) {
    if (!def->bloom) return 0;
    return requested > fallback ? requested : fallback;
}

int qlcg_pa_fruit_count_for(const PdDef *def, int requested, int fallback) {
    if (!def->fruit) return 0;
    int base = requested > 0 ? requested : fallback;
    if (base <= 0) return 0;
    double scale = qlcg_pa_pnum(def, "fruitScale", 1.0);
    if (!isfinite(scale) || scale <= 0.0) scale = 1.0;
    int v = qlcg_pa_js_round((double)base * scale);
    return v < 1 ? 1 : v;
}

static int petal_count_of(const PdDef *def) {
    return qlcg_pa_js_round(qlcg_pa_clampd(qlcg_pa_pnum(def, "petals", 5.0), 2.0, 16.0));
}

static double petal_width_ratio(int petals) {
    return qlcg_pa_clampd(1.0 - (double)petals * 0.055, 0.24, 0.6);
}

static double flower_radius_for(const PdDef *def, double fallback) {
    double explicit_r = pd_num(&def->params, "bloomRadius", NAN);
    if (!isnan(explicit_r) && isfinite(explicit_r) && explicit_r > 0.0)
        return qlcg_pa_clampd(explicit_r, 0.03, 0.6);
    return qlcg_pa_clampd(fallback, 0.04, 0.4);
}

/* --------- 花叶果积木 --------- */

void qlcg_pa_flower_at(LpBuilder *b, const PdDef *def, const double center[3], double radius, LpRng *rand) {
    int petals = petal_count_of(def);
    LpRgb col = qlcg_pa_tone(def, "bloom", 1.0);
    LpRgb core = qlcg_pa_tone(def, "pollen", 1.0);
    double w = radius * 2.5 * petal_width_ratio(petals);
    double phase = qlcg_lp_rng_next(rand) * TAU;
    for (int i = 0; i < petals; i++) {
        double a = phase + ((double)i / (double)petals) * TAU;
        double len = radius * (0.8 + qlcg_lp_rng_next(rand) * 0.25);
        double wid = w * (0.85 + qlcg_lp_rng_next(rand) * 0.3);
        double dep = (i % 2 == 0) ? 0.04 + qlcg_lp_rng_next(rand) * 0.1
                                  : 0.26 + qlcg_lp_rng_next(rand) * 0.16;
        double pitch = 0.3 + qlcg_lp_rng_next(rand) * 0.3;
        double pos[3] = { center[0], center[1] + radius * 0.14, center[2] };
        LpOpts o = qlcg_lp_opts();
        o.pitch = pitch; o.cup = 0.32; o.mat = LP_MAT_BLOOM; o.rand = rand;
        qlcg_lp_petal(b, pos, a, len, wid, qlcg_pa_deepen(col, dep), &o);
    }
    double c2[3] = { center[0], center[1] + radius * 0.3, center[2] };
    LpOpts od = qlcg_lp_opts();
    od.jitter = 0.2; od.rand = rand; od.mat = LP_MAT_BLOOM; od.floor_on = 0;
    qlcg_lp_dome(b, c2, radius * 0.42, radius * 0.5, radius * 0.42, 4, core, &od);
}

void qlcg_pa_bloom_cluster(LpBuilder *b, const PdDef *def, const double center[3],
                          double r, int count, LpRng *rand, double size, double y_span) {
    double fr = flower_radius_for(def, size);
    for (int i = 0; i < count; i++) {
        double t = ((double)i + 0.5) / (double)count;
        double a = (double)i * GOLDEN + (double)def->build_seed * 0.0009 + qlcg_lp_rng_next(rand) * 0.5;
        double rad = r * (0.82 + 0.3 * qlcg_lp_rng_next(rand)) * (0.92 + 0.16 * sqrt(t));
        double y = center[1] + (qlcg_lp_rng_next(rand) * 2.0 - 1.0) * y_span;
        double pos[3] = { center[0] + cos(a) * rad, y, center[2] + sin(a) * rad };
        qlcg_pa_flower_at(b, def, pos, fr, rand);
    }
}

void qlcg_pa_leaf_spray(LpBuilder *b, const PdDef *def, const double center[3],
                       double r, int count, LpRng *rand,
                       double width, double pitch_in, double spread_y, int dry, double len_scale) {
    if (count <= 0) return;
    LpRgb fol = qlcg_pa_tone(def, "foliage", 1.0);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1.0);
    LpRgb dryc = qlcg_pa_tone(def, "witheredStem", 1.0);
    for (int i = 0; i < count; i++) {
        double t = ((double)i + 0.5) / (double)count;
        double a = (double)i * GOLDEN + (double)def->build_seed * 0.0011 + qlcg_lp_rng_next(rand) * 0.6;
        double rad = r * (0.5 + 0.5 * sqrt(t)) * (0.85 + qlcg_lp_rng_next(rand) * 0.3);
        double y = center[1] + r * spread_y * (1.0 - t) + (qlcg_lp_rng_next(rand) - 0.5) * r * 0.55;
        double len = r * len_scale * (0.8 + qlcg_lp_rng_next(rand) * 0.45);
        double bearing = a + (qlcg_lp_rng_next(rand) - 0.5) * 0.5;
        double pitch = isnan(pitch_in) ? (0.1 + qlcg_lp_rng_next(rand) * 0.3) : pitch_in;
        double pos[3] = { center[0] + cos(a) * rad, y, center[2] + sin(a) * rad };
        LpOpts o = qlcg_lp_opts();
        o.pitch = pitch; o.cup = 0.4; o.rand = rand;
        qlcg_lp_petal(b, pos, bearing, len, len * width, dry ? dryc : ((i % 2) ? fol : dark), &o);
    }
}

void qlcg_pa_bent_leaf(LpBuilder *b, const double start[3], double bearing, double len, double wid,
                      double rise, double drop, LpRgb col, LpRng *rand, int mat) {
    double dx = cos(bearing), dz = sin(bearing);
    double mid[3] = { start[0] + dx * len * 0.5, start[1] + rise, start[2] + dz * len * 0.5 };
    LpOpts o1 = qlcg_lp_opts();
    o1.pitch = atan2(rise, len * 0.5) + 0.14; o1.cup = 0.34; o1.mat = mat; o1.rand = rand;
    qlcg_lp_petal(b, start, bearing, len * 0.56, wid, col, &o1);
    LpOpts o2 = qlcg_lp_opts();
    o2.pitch = atan2(rise - drop, len * 0.5) - 0.04; o2.cup = 0.3; o2.mat = mat; o2.rand = rand;
    qlcg_lp_petal(b, mid, bearing, len * 0.56, wid * 0.72, col, &o2);
}

void qlcg_pa_fruit_layout(LpBuilder *b, const PdDef *def, const double center[3],
                          double r, int count, LpRng *rand, double size,
                          int sockets, PaFruitBuilder draw) {
    double explicit_r = pd_num(&def->params, "fruitRadius", NAN);
    double fr = (!isnan(explicit_r) && explicit_r > 0.0) ? qlcg_pa_clampd(explicit_r, 0.02, 0.5)
                                                         : qlcg_pa_clampd(size, 0.035, 0.24);
    for (int i = 0; i < count; i++) {
        double t = ((double)i + 0.5) / (double)count;
        double a = (double)i * GOLDEN + (double)def->build_seed * 0.0013 + qlcg_lp_rng_next(rand) * 0.5;
        double rad = r * (0.8 + 0.35 * qlcg_lp_rng_next(rand)) * (0.9 + 0.2 * sqrt(t));
        double y = center[1] - (0.15 + qlcg_lp_rng_next(rand) * 0.6) * r * 0.5;
        double pos[3] = { center[0] + cos(a) * rad, y, center[2] + sin(a) * rad };
        if (sockets) qlcg_lp_socket(b, pos, LP_SOCKET_FRUIT);
        if (draw) draw(b, def, pos, fr, rand);
    }
}

static void build_cluster_fruit(LpBuilder *b, const PdDef *def, const double pos[3],
                                double radius, LpRng *rand) {
    double size[3] = { radius, radius, radius };
    LpOpts o = qlcg_lp_opts();
    o.jitter = 0.15; o.rand = rand; o.mat = LP_MAT_FRUIT; o.rings = 1;
    qlcg_lp_ico(b, pos, size, 5, qlcg_pa_tone(def, "fruit", 1), &o);
}

void qlcg_pa_fruit_cluster(LpBuilder *b, const PdDef *def, const double center[3],
                           double r, int count, LpRng *rand, double size) {
    qlcg_pa_fruit_layout(b, def, center, r, count, rand, size, 0, build_cluster_fruit);
}

void qlcg_pa_limb_to(LpBuilder *b, const double start[3], const double dir[3],
                    double length, double radius, LpRgb col, double out_end[3]) {
    double len = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len == 0.0) len = 1.0;
    double d[3] = { dir[0] / len, dir[1] / len, dir[2] / len };
    LpOpts o = qlcg_lp_opts();
    o.sides = 3; o.tip_ratio = 0.42; o.cap = 0;
    qlcg_lp_limb(b, start, d, length, radius, col, &o);
    if (out_end) {
        out_end[0] = start[0] + d[0] * length;
        out_end[1] = start[1] + d[1] * length;
        out_end[2] = start[2] + d[2] * length;
    }
}

void qlcg_pa_pollen_grains(LpBuilder *b, const PdDef *def, int count, double base_radius,
                          double bottom, double top, LpRng *rand) {
    LpRgb col = qlcg_pa_tone(def, "pollen", 1.0);
    double span = top - bottom;
    if (span < 0.4) span = 0.4;
    double grain = qlcg_pa_clampd(base_radius * 0.035, 0.05, 0.09);
    for (int i = 0; i < count; i++) {
        double t = pow(qlcg_lp_rng_next(rand), 0.65);
        double y = bottom + span * t;
        double r = base_radius * (0.92 - t * 0.72) * (0.55 + qlcg_lp_rng_next(rand) * 0.45);
        double a = qlcg_lp_rng_next(rand) * TAU;
        double rad = grain * (0.8 + qlcg_lp_rng_next(rand) * 0.45);
        double pos[3] = { cos(a) * r, y, sin(a) * r };
        double rad3[3] = { rad, rad, rad };
        LpOpts o = qlcg_lp_opts();
        o.mat = LP_MAT_BLOOM;
        qlcg_lp_ico(b, pos, rad3, 3, col, &o);
    }
}

int qlcg_pa_tag_has(const PdDef *def, const char *tag) {
    for (int i = 0; i < def->tag_count; i++)
        if (def->tags[i] && strcmp(def->tags[i], tag) == 0) return 1;
    return 0;
}
