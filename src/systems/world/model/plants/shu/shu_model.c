#include "shu_model.h"
#include "../../plant_forms.h"

#include <math.h>
#include <stdio.h>

void qlcg_pa_shu_fruit_cluster(LpBuilder *b, const PdDef *def, const double center[3],
                               double reach, int count, LpRng *rng, double size,
                               PaFruitBuilder draw) {
    if (def->state == PD_STATE_FRUITING) {
        qlcg_pa_fruit_layout(b, def, center, reach, count, rng, size, 1, draw);
    } else if (def->state == PD_STATE_NORMAL) {
        LpRng layout = *rng;
        qlcg_pa_fruit_layout(b, def, center, reach, count, &layout, size, 1, NULL);
    }
}

static int tolerance(const PdDef *def, const char *prefix, int fallback) {
    char tag[16];
    for (int i = 0; i < 4; i++) {
        snprintf(tag, sizeof(tag), "%s_%d", prefix, i);
        if (qlcg_pa_tag_has(def, tag)) return i;
    }
    return fallback;
}

ShuSeason qlcg_pa_shu_season(const PdDef *def, int season, double winter_leaf, int cold, int heat) {
    double heat_stress = tolerance(def, "naire", heat) / 4.0;
    double cold_stress = tolerance(def, "naihan", cold) / 4.0;
    switch (season) {
        case PD_SEASON_SPRING:
            return (ShuSeason){ .70 + .20 * winter_leaf, .75 + .10 * winter_leaf, -.09 };
        case PD_SEASON_SUMMER:
            return (ShuSeason){ 1.0 - .20 * heat_stress, 1.09 - .28 * heat_stress, .39 * heat_stress };
        case PD_SEASON_AUTUMN:
            return (ShuSeason){ .49 + .46 * winter_leaf, .86 + .08 * winter_leaf, .12 };
        default:
            return (ShuSeason){ winter_leaf, .83 - .34 * cold_stress, .18 + .36 * cold_stress };
    }
}

static void tree_segment(LpBuilder *b, const double a[3], const double z[3],
                         double radius, double taper, int sides, LpRgb color) {
    double d[3] = {z[0] - a[0], z[1] - a[1], z[2] - a[2]};
    double length = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (length <= 0) return;
    for (int i = 0; i < 3; i++) d[i] /= length;
    LpOpts o = qlcg_lp_opts();
    o.sides = sides;
    o.tip_ratio = taper;
    qlcg_lp_limb(b, a, d, length, radius, color, &o);
}

static void tree_leaf(LpBuilder *b, const double tip[3], double a, double len,
                      const ShuProfile *p, const ShuSeason *s, LpRgb color) {
    LpOpts o = qlcg_lp_opts();
    o.pitch = .24 - s->droop;
    o.cup = .28;
    double width = len * p->leaf_width;
    if (p->leaf_style == SHU_LEAF_FAN) {
        double dx = cos(a), dz = sin(a), y = tip[1] + len * sin(o.pitch);
        double left[3] = {tip[0] + dx * len * .58 + dz * width * .50, y, tip[2] + dz * len * .58 - dx * width * .50};
        double right[3] = {tip[0] + dx * len * .58 - dz * width * .50, y, tip[2] + dz * len * .58 + dx * width * .50};
        double outer[3] = {tip[0] + dx * len, y + len * .09, tip[2] + dz * len};
        qlcg_lp_quad(b, tip, left, outer, right, color, LP_MAT_SOLID);
    } else if (p->leaf_style == SHU_LEAF_LOBED) {
        for (int i = -1; i <= 1; i++) {
            qlcg_lp_petal(b, tip, a + .52 * i, len * (i ? .77 : 1.0), width * .64, color, &o);
        }
    } else if (p->leaf_style == SHU_LEAF_COMPOUND) {
        for (int i = 0; i < 3; i++) {
            double t = (i + 1) * .23;
            double node[3] = {tip[0] + cos(a) * len * t, tip[1], tip[2] + sin(a) * len * t};
            for (int side = -1; side <= 1; side += 2)
                qlcg_lp_petal(b, node, a + side * .91, len * (.62 - t * .35), width, color, &o);
        }
    } else {
        qlcg_lp_petal(b, tip, a, len, width, color, &o);
    }
}

void qlcg_pa_shu_broadleaf(LpBuilder *b, const PdDef *def, int season, int form,
                           const ShuProfile *p, const ShuShape *shape, PaFruitBuilder draw_fruit) {
    ShuSeason s = qlcg_pa_shu_season(def, season, p->winter_leaf, p->cold, p->heat);
    if (season == PD_SEASON_SUMMER && p->summer_fullness > 0) {
        s.size *= p->summer_fullness;
        s.density = fmin(1.0, s.density * p->summer_fullness);
    }
    double h = fmax(.25, qlcg_pa_pnum(def, "height", 10));
    double r = fmax(.10, qlcg_pa_pnum(def, "canopy", h * .36));
    double trunk = qlcg_pa_clampd((qlcg_pa_pnum(def, "trunk", h * p->crown_base)
                                 + h * p->crown_base) * .5, h * .20, h * .71);
    double radius = fmin(h * .12, qlcg_pa_pnum(def, "trunkRadius", h * .044));
    h *= shape->height;
    r *= shape->crown;
    trunk *= shape->trunk;
    double lean = r * p->lean;
    LpRgb bark = qlcg_pa_tone(def, "bark", 1);
    LpRgb twig = qlcg_pa_tone(def, "barkLight", 1);
    LpRgb leaf = qlcg_pa_tone(def, "foliage", 1);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1);
    LpRng skeleton, detail;
    qlcg_lp_rng_seed(&skeleton, def->build_seed);
    qlcg_lp_rng_seed(&detail, def->build_seed ^ 0x6f3d8ab1u);
    double phase = qlcg_lp_rng_next(&skeleton) * TAU;

    double root[3] = {0, 0, 0};
    double foot[3] = {lean * .18, trunk * .15, 0};
    double fork[3] = {lean, trunk, 0};
    double leader[3] = {lean * .82, trunk + (h - trunk) * (.57 + p->rise * .48), r * .07};
    tree_segment(b, root, foot, radius * 1.35, .74, 6, bark);
    tree_segment(b, foot, fork, radius, .57, 6, bark);
    tree_segment(b, fork, leader, radius * .58, .10, 5, twig);

    int simple = form == VEG_FORM_SIMPLE;
    int umbrella = p->hang > 1.0;
    int fan = p->leaf_style == SHU_LEAF_FAN;
    int limb_count = p->limbs + shape->branches;
    int limbs = simple ? (limb_count + 1) / 2 : limb_count;
    int flowers = qlcg_pa_bloom_count_for(def, qlcg_pa_js_round(qlcg_pa_pnum(def, "bloomCount", 0)), 6);
    int fruits = qlcg_pa_fruit_count_for(def, qlcg_pa_js_round(qlcg_pa_pnum(def, "fruitCount", 0)), 3);
    int sites = (int)ceil(fmax(flowers, fruits) / (double)limbs);
    int min_sites = fan ? 4 : 3;
    if (sites < min_sites) sites = min_sites;
    if (sites > 6) sites = 6;
    if (def->state == PD_STATE_NORMAL && season != p->bloom_season) flowers = 0;
    if (season == PD_SEASON_SPRING && flowers > 0 && p->winter_leaf < .2) s.density *= .62;
    if (simple) {
        sites = 2;
        flowers = flowers > 0;
        fruits = fruits > 0;
    }
    int snow_canopy = qlcg_pa_has_snow_target(def, "canopy");
    double fruit_size = qlcg_pa_pnum(def, "fruitRadius", 0);
    fruit_size = fruit_size > 0 ? qlcg_pa_clampd(fruit_size, .02, .5)
                               : qlcg_pa_clampd(r * .08, .035, .24);
    double bloom_size = qlcg_pa_pnum(def, "bloomRadius", 0);
    bloom_size = bloom_size > 0 ? qlcg_pa_clampd(bloom_size, .03, .6)
                               : qlcg_pa_clampd(r * .12, .04, .4);
    double umbrella_size = r * p->cluster * s.size;
    LpOpts crown = qlcg_lp_opts();
    crown.rings = 1;
    crown.squash = .18;
    if (umbrella && s.density > 0) {
        double center[3] = {leader[0], trunk + (leader[1] - trunk) * .67, leader[2] * .67};
        qlcg_lp_dome(b, center, umbrella_size * 1.60, umbrella_size * .48,
                     umbrella_size * 1.52, 7, dark, &crown);
    }

    for (int i = 0; i < limbs; i++) {
        double t = (i + .22 + .56 * qlcg_lp_rng_next(&skeleton)) / limbs;
        double a = phase + i * GOLDEN + .30 * qlcg_lp_rng_next(&skeleton);
        double dx = cos(a), dz = sin(a);
        double variation = .75 + .25 * qlcg_lp_rng_next(&skeleton);
        double reach = r * p->reach * variation * (1.10 - .30 * t);
        double branch_range = fan ? .91 : .64;
        double start[3] = {lean + (leader[0] - lean) * t * branch_range,
                           trunk + (leader[1] - trunk) * t * branch_range, leader[2] * t * branch_range};
        double lift = (h - trunk) * p->rise * variation * (1.0 - .46 * t);
        double elbow[3] = {start[0] + dx * reach * .52, start[1] + lift * .64, start[2] + dz * reach * .52};
        double end[3] = {start[0] + dx * reach, start[1] + lift, start[2] + dz * reach};
        double branch_r = radius * (.49 - .25 * t);
        tree_segment(b, start, elbow, branch_r, .57, 5, bark);
        tree_segment(b, elbow, end, branch_r * .58, .32, 4, twig);

        if (umbrella && s.density > 0) {
            double center[3] = {(elbow[0] + end[0]) * .5, (elbow[1] + end[1]) * .5,
                                (elbow[2] + end[2]) * .5};
            crown.rot_y = a;
            qlcg_lp_dome(b, center, umbrella_size * 1.16, umbrella_size * .44,
                         umbrella_size, 6, i % 2 ? leaf : dark, &crown);
        }

        for (int j = 0; j < sites; j++) {
            double u = .40 + .60 * (j + 1) / sites;
            double bearing = a + (j % 2 ? 1 : -1) * (1.12 - .54 * u);
            double vx = cos(bearing), vz = sin(bearing);
            double node[3] = {elbow[0] + (end[0] - elbow[0]) * u,
                              elbow[1] + (end[1] - elbow[1]) * u,
                              elbow[2] + (end[2] - elbow[2]) * u};
            double tip[3] = {node[0] + vx * r * .23, node[1] + r * (.12 - p->hang * u),
                             node[2] + vz * r * .23};
            tree_segment(b, node, tip, branch_r * .23, .26, 3, twig);
            double fill = (j + .5) / sites;
            if (umbrella && s.density > 0) {
                int leaves = simple ? 2 : qlcg_pa_js_round(2 + s.density * 4);
                double length = r * .19 * s.size;
                for (int k = 0; k < leaves; k++) {
                    double v = (k + .45) / leaves;
                    double point[3] = {node[0] + (tip[0] - node[0]) * v,
                                       node[1] + (tip[1] - node[1]) * v,
                                       node[2] + (tip[2] - node[2]) * v};
                    tree_leaf(b, point, bearing + (k % 2 ? 1.12 : -1.12),
                              length * (1.04 - .32 * v), p, &s, k % 2 ? leaf : dark);
                }
            } else if (fill <= s.density) {
                double clump = r * p->cluster * s.size * (.81 + .24 * u);
                double top = clump * (.72 - .34 * p->hang);
                LpOpts o = qlcg_lp_opts();
                o.rot_y = bearing;
                o.squash = .25;
                qlcg_lp_dome(b, tip, clump, top, clump * .83, 5, i % 2 ? leaf : dark, &o);
                double point[3] = {tip[0] + vx * clump * .48, tip[1] + top * .31, tip[2] + vz * clump * .48};
                if (!simple) {
                    tree_leaf(b, point, bearing, clump * 1.18, p, &s, leaf);
                    if (fan) {
                        for (int k = 1; k <= 3; k++) {
                            double v = k * .23;
                            double sprig[3] = {node[0] + (tip[0] - node[0]) * v,
                                               node[1] + (tip[1] - node[1]) * v,
                                               node[2] + (tip[2] - node[2]) * v};
                            tree_leaf(b, sprig, bearing + (k % 2 ? 1.05 : -1.05),
                                      clump * (.84 + .24 * v), p, &s, k % 2 ? dark : leaf);
                        }
                    } else if (p->hang > .7) {
                        for (int k = 1; k <= 3; k++) {
                            double v = k * .23;
                            double sprig[3] = {node[0] + (tip[0] - node[0]) * v,
                                               node[1] + (tip[1] - node[1]) * v,
                                               node[2] + (tip[2] - node[2]) * v};
                            tree_leaf(b, sprig, bearing + (k % 2 ? .92 : -.92),
                                      clump * 1.07, p, &s, dark);
                        }
                    }
                }
            }

            int slot = j * limbs + i;
            if (slot < flowers) qlcg_pa_flower_at(b, def, tip, bloom_size, &detail);
            if (slot < fruits) {
                qlcg_lp_socket(b, tip, LP_SOCKET_FRUIT);
                if (def->state == PD_STATE_FRUITING) draw_fruit(b, def, tip, fruit_size, &detail);
            }
            if (j == sites - 1 && i % 2 == 0 && snow_canopy)
                qlcg_lp_socket(b, tip, LP_SOCKET_SNOW_CANOPY);
        }
    }
    if (qlcg_pa_has_snow_target(def, "ground"))
        qlcg_lp_socket(b, root, LP_SOCKET_SNOW_GROUND);
}
