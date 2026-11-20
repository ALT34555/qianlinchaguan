#include "yao_model.h"
#include "../../plant_forms.h"

#include <math.h>

YaoPlant qlcg_pa_yao_plant(const PdDef *def, int season, int form, int bloom_season) {
    static const double height[] = {.79, 1, .93, .54};
    static const double foliage[] = {.84, 1, .87, .62};
    static const double shape_height[] = {1, 1.13, .84};
    static const double shape_spread[] = {1, .86, 1.22};
    unsigned shape = qlcg_pa_shape_variant(def);
    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);

    YaoPlant p = {0};
    p.simple = form == VEG_FORM_SIMPLE;
    p.seedling = def->state == PD_STATE_SEEDLING;
    p.bloom = def->state == PD_STATE_FLOWERING ||
              (def->state == PD_STATE_NORMAL && season == bloom_season);
    p.fruit = def->state == PD_STATE_FRUITING ||
              (def->state == PD_STATE_NORMAL && season == PD_SEASON_AUTUMN && !p.bloom);
    p.dry = def->state == PD_STATE_WITHERED ||
            (def->state == PD_STATE_NORMAL && season == PD_SEASON_WINTER);
    p.height = qlcg_pa_pnum(def, "height", .7) * shape_height[shape] * (season == PD_SEASON_NONE ? 1.0 : height[season]);
    p.spread = qlcg_pa_pnum(def, "spread", .35) * shape_spread[shape];
    p.phase = qlcg_lp_rng_next(&rng) * TAU + shape * .67;
    p.leaf_scale = season == PD_SEASON_NONE ? 1.0 : foliage[season];
    if (def->state == PD_STATE_WITHERED) {
        p.height *= .74;
        p.spread *= .59;
    }
    if (p.dry) {
        p.leaf = qlcg_lp_wind_color(qlcg_lp_hex(pd_str(&def->palette, "withered", "#8F7548"), 143, 117, 72), LP_WIND_LEAF);
        p.shade = qlcg_lp_wind_color(qlcg_lp_hex(pd_str(&def->palette, "witheredDark", "#6A5232"), 106, 82, 50), LP_WIND_LEAF);
        p.stem = qlcg_lp_wind_color(qlcg_lp_hex(pd_str(&def->palette, "witheredStem", "#A08E6A"), 160, 142, 106), LP_WIND_BARK);
    } else {
        p.leaf = qlcg_pa_tone(def, "foliage", 1);
        p.shade = qlcg_pa_tone(def, "foliageDark", 1);
        p.stem = qlcg_pa_tone(def, "barkLight", 1);
    }
    return p;
}

void qlcg_pa_yao_stalk(LpBuilder *b, const double from[3], const double to[3],
                       double radius, double taper, LpRgb color) {
    double dir[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    double len = js_hypot3(dir[0], dir[1], dir[2]);
    if (len < 1e-6) return;
    for (int axis = 0; axis < 3; axis++) dir[axis] /= len;
    LpOpts o = qlcg_lp_opts();
    o.sides = 3;
    o.tip_ratio = taper;
    o.cap = 0;
    qlcg_lp_limb(b, from, dir, len, radius, color, &o);
}

void qlcg_pa_yao_twig(LpBuilder *b, const double from[3], const double to[3],
                      double radius, LpRgb color) {
    double x = to[0] - from[0], y = to[1] - from[1], z = to[2] - from[2];
    double horizontal = hypot(x, z);
    double len = hypot(horizontal, y);
    if (len < 1e-6) return;
    double ux = horizontal > 1e-6 ? -z / horizontal : 1;
    double uz = horizontal > 1e-6 ? x / horizontal : 0;
    double cross[2][3] = {{ux, 0, uz}, {y * uz / len, (z * ux - x * uz) / len, -y * ux / len}};
    for (int i = 0; i < 2; i++) {
        double left[3], right[3];
        for (int axis = 0; axis < 3; axis++) {
            left[axis] = from[axis] + radius * cross[i][axis];
            right[axis] = from[axis] - radius * cross[i][axis];
        }
        qlcg_lp_triangle(b, left, right, to, color, LP_MAT_SOLID);
    }
}

void qlcg_pa_yao_berry(LpBuilder *b, const double at[3], double radius,
                       double aspect, LpRgb color) {
    LpOpts o = qlcg_lp_opts();
    o.mat = LP_MAT_FRUIT;
    double size[3] = {radius, radius * aspect, radius};
    qlcg_lp_ico(b, at, size, 5, color, &o);
    qlcg_lp_socket(b, at, LP_SOCKET_FRUIT);
}

void qlcg_pa_yao_flower(LpBuilder *b, const double at[3], double a, int count,
                        double length, double width, double pitch, LpRgb color) {
    for (int i = 0; i < count; i++)
        qlcg_pa_folded_petal(b, at, a + TAU * i / count, length, width, pitch + .12 * (i % 2),
                          qlcg_lp_lighten(color, .07 * (i % 2)), LP_MAT_BLOOM);
}
