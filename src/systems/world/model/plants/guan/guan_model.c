#include "guan_model.h"
#include "../../plant_forms.h"

#include <math.h>
#include <stdio.h>

typedef struct {
    double leaf_size, leaf_density, shoot, droop;
} ShrubSeason;

static int tolerance(const PdDef *def, const char *prefix, int fallback) {
    char tag[16];
    for (int i = 0; i < 4; i++) {
        snprintf(tag, sizeof(tag), "%s_%d", prefix, i);
        if (qlcg_pa_tag_has(def, tag)) return i;
    }
    return fallback;
}

static ShrubSeason seasonal_form(const PdDef *def, int season, const ShrubProfile *p) {
    if (def->state == PD_STATE_WITHERED) return (ShrubSeason){ .48, .12, .28, .29 };
    double heat = tolerance(def, "naire", p->heat) / 4.0;
    double cold = tolerance(def, "naihan", p->cold) / 4.0;
    switch (season) {
        case PD_SEASON_SPRING:
            return (ShrubSeason){ .59, p->bloom_season == PD_SEASON_SPRING && p->winter_leaf == 0
                                  ? .24 : .66, .76, -.13 };
        case PD_SEASON_SUMMER:
            return (ShrubSeason){ 1.04 - heat * .31, 1.0 - heat * .38, 1.0, heat * .54 };
        case PD_SEASON_AUTUMN:
            return (ShrubSeason){ .87, .43 + p->winter_leaf * .49, .87, .16 };
        default:
            return (ShrubSeason){ .83 - cold * .36, p->winter_leaf,
                                  .79 - cold * .54, .16 + cold * .27 };
    }
}

static void segment(LpBuilder *b, const double a[3], const double z[3],
                    double radius, double taper, int sides, LpRgb color) {
    double d[3] = {z[0] - a[0], z[1] - a[1], z[2] - a[2]};
    double len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len <= 0) return;
    for (int i = 0; i < 3; i++) d[i] /= len;
    LpOpts o = qlcg_lp_opts();
    o.sides = sides;
    o.tip_ratio = taper;
    qlcg_lp_limb(b, a, d, len, radius, color, &o);
}

static void rosette_leaf(LpBuilder *b, const double start[3], double a,
                         double length, double width, double lift, double curl, LpRgb color) {
    double dx = cos(a), dz = sin(a);
    double section[3][3][3];
    for (int i = 0; i < 3; i++) {
        double t = i == 0 ? 0 : i == 1 ? .30 : .68;
        double half = width * (i == 0 ? .23 : i == 1 ? .50 : .31);
        double x = start[0] + dx * length * t, z = start[2] + dz * length * t;
        double y = start[1] + lift * t * (2 - t) - curl * t * t;
        double left[3] = {x - dz * half, y, z + dx * half};
        double ridge[3] = {x, y + half * .36, z};
        double right[3] = {x + dz * half, y, z - dx * half};
        for (int axis = 0; axis < 3; axis++) {
            section[i][0][axis] = left[axis];
            section[i][1][axis] = ridge[axis];
            section[i][2][axis] = right[axis];
        }
    }
    double tip[3] = {start[0] + dx * length, start[1] + lift - curl, start[2] + dz * length};
    for (int side = 0; side < 2; side++) {
        LpRgb shade = side ? qlcg_pa_deepen(color, .23) : color;
        for (int i = 0; i < 2; i++)
            qlcg_lp_quad(b, section[i][side], section[i][side + 1],
                         section[i + 1][side + 1], section[i + 1][side], shade, LP_MAT_SOLID);
        qlcg_lp_triangle(b, section[2][side], section[2][side + 1], tip, shade, LP_MAT_SOLID);
    }
}

static void build_rosette(LpBuilder *b, const PdDef *def, double h, double r,
                          const ShrubSeason *s, int leaves, int flowers, LpRng *rng) {
    double base[3] = {0, 0, 0};
    LpRgb green = qlcg_pa_tone(def, "foliage", 1);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1);
    qlcg_lp_prism(b, base, r * .16, r * .16, 0, h * .39, 7, dark, NULL);
    leaves = qlcg_pa_js_round(leaves * s->leaf_density);
    for (int i = 0; i < leaves; i++) {
        double layer = (double)i / leaves;
        double a = i * GOLDEN;
        double start[3] = {cos(a) * r * .09, h * (.06 + layer * .30), sin(a) * r * .09};
        double length = r * (1.23 - layer * 1.07) * (.74 + .26 * s->shoot);
        double rise = h * (.19 + layer * .55) * (.72 + s->leaf_size * .28);
        double curl = h * (.13 + .11 * s->droop) * (1 - layer);
        rosette_leaf(b, start, a, length, r * (.30 - layer * .11),
                     rise, curl, i % 2 ? green : qlcg_lp_mix(green, dark, .35));
    }
    if (flowers) {
        double start[3] = {0, h * .13, 0}, tip[3] = {r * .07, h * 1.65, 0};
        segment(b, start, tip, r * .042, .36, 5, dark);
        for (int i = 0; i < flowers; i++) {
            double a = i * GOLDEN;
            double t = .48 + .49 * (i + 1) / flowers;
            double node[3] = {tip[0] * t, start[1] + (tip[1] - start[1]) * t, 0};
            double end[3] = {node[0] + cos(a) * r * .17, node[1] + h * .065, sin(a) * r * .17};
            segment(b, node, end, r * .013, .55, 3, dark);
            qlcg_pa_flower_at(b, def, end, r * .065, rng);
        }
    }
}

static void attach_fruit(LpBuilder *b, const PdDef *def, const ShrubProfile *p,
                         const double point[3], double radius, LpRng *rng) {
    qlcg_lp_socket(b, point, LP_SOCKET_FRUIT);
    if (def->state == PD_STATE_FRUITING) p->build_fruit(b, def, point, radius, rng);
}

static void build_columns(LpBuilder *b, const PdDef *def, const ShrubProfile *p,
                          double h, double r, int stems,
                          const ShrubSeason *s, int flowers, int fruits, LpRng *rng) {
    LpRgb green = qlcg_pa_tone(def, "foliage", 1);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1);
    for (int i = 0; i < stems; i++) {
        double a = i * GOLDEN, offset = i ? r * (.38 + .12 * i) : 0;
        double start[3] = {cos(a) * offset, 0, sin(a) * offset};
        double height = h * (i ? .69 - .075 * i : .91) * (.80 + .20 * s->shoot);
        double radius = r * (i ? .18 : .25) * (.79 + .21 * s->leaf_size);
        LpOpts rib = qlcg_lp_opts();
        rib.rot_y = a;
        qlcg_lp_prism(b, start, radius, radius, 0, height, 6, green, &rib);
        double top[3] = {start[0], height, start[2]};
        rib.squash = .35;
        qlcg_lp_dome(b, top, radius, radius * .57, radius, 6, green, &rib);
        for (int j = 0; j < 6; j++) {
            double angle = a + j * TAU / 6;
            double ridge[3] = {start[0] + cos(angle) * radius * .84, 0,
                               start[2] + sin(angle) * radius * .84};
            qlcg_lp_prism(b, ridge, radius * .21, radius * .21, 0, height * .96, 3, dark, &rib);
        }
        if (i < flowers) qlcg_pa_flower_at(b, def, top, r * .19, rng);
        if (i < fruits) attach_fruit(b, def, p, top, r, rng);
    }
}

static void branch_bloom(LpBuilder *b, const PdDef *def, const ShrubProfile *p,
                         const double tip[3], const double direction[3], double size, LpRng *rng) {
    if (p->habit == SHRUB_ARCHING) {
        for (int side = -1; side <= 1; side += 2) {
            double at[3] = {tip[0] - side * direction[2] * size * .83, tip[1] + size * .22,
                            tip[2] + side * direction[0] * size * .83};
            segment(b, tip, at, size * .064, .55, 3, qlcg_pa_tone(def, "barkLight", 1));
            qlcg_pa_flower_at(b, def, at, size, rng);
        }
    } else if (p->habit == SHRUB_CATKIN || p->habit == SHRUB_PANICLE) {
        double axis[3] = {direction[0] * size * 3, size * (p->habit == SHRUB_CATKIN ? -5 : 4),
                          direction[2] * size * 3};
        double end[3] = {tip[0] + axis[0], tip[1] + axis[1], tip[2] + axis[2]};
        LpRgb c = qlcg_pa_tone(def, "bloom", 1);
        segment(b, tip, end, size * .15, .25, 3, c);
        LpOpts o = qlcg_lp_opts();
        o.mat = LP_MAT_BLOOM;
        for (int j = 1; j <= 3; j++) {
            double t = j * .32;
            double pos[3] = {tip[0] + t * axis[0], tip[1] + t * axis[1], tip[2] + t * axis[2]};
            qlcg_lp_dome(b, pos, size * (1.5 - t), size * .8, size * (1.5 - t), 5, c, &o);
        }
    } else if (p->habit == SHRUB_FEATHER) {
        for (int j = 0; j < 3; j++) {
            double offset = (j - 2) * size * 1.35;
            double pos[3] = {tip[0] + direction[0] * offset, tip[1] + direction[1] * offset,
                             tip[2] + direction[2] * offset};
            qlcg_pa_flower_at(b, def, pos, size, rng);
        }
    } else {
        qlcg_pa_flower_at(b, def, tip, size, rng);
    }
}

static void bramble_leaf(LpBuilder *b, const double pos[3], double a,
                         double len, double width, const LpOpts *opts, LpRgb leaf, LpRgb vein) {
    double join[3] = {pos[0] + cos(a) * len * .24, pos[1] + len * .09, pos[2] + sin(a) * len * .24};
    segment(b, pos, join, len * .024, .66, 3, vein);
    qlcg_lp_petal(b, join, a, len * .77, len * width * .65, leaf, opts);
    for (int side = -1; side <= 1; side += 2)
        qlcg_lp_petal(b, join, a + side * 1.07, len * .58, len * width * .53, leaf, opts);
}

static void build_branches(LpBuilder *b, const PdDef *def, const ShrubProfile *p,
                           const ShrubSeason *s, double h, double r, int stems, int forks,
                           int flowers, int fruits, int snow_canopy, LpRng *rng) {
    int green_stem = p->habit == SHRUB_PENCIL || p->habit == SHRUB_JOINTED;
    int arching = p->habit == SHRUB_ARCHING;
    int bramble = p->habit == SHRUB_BRAMBLE;
    LpRgb leaf = qlcg_pa_tone(def, "foliage", 1);
    LpRgb dark = qlcg_pa_tone(def, "foliageDark", 1);
    LpRgb bark = qlcg_pa_tone(def, def->state == PD_STATE_WITHERED ? "witheredDark" : "barkLight", 1);
    double origin[3] = {0, 0, 0};
    if (p->habit == SHRUB_CAUDEX) {
        LpOpts o = qlcg_lp_opts();
        o.rings = 1;
        o.squash = .5;
        origin[1] = h * .18;
        qlcg_lp_dome(b, origin, r * .42, h * .36, r * .38, 7, bark, &o);
        origin[1] = h * .39;
    }
    LpRng skeleton = *rng;
    double phase = qlcg_lp_rng_next(&skeleton) * TAU;
    double crown_height = p->crown_height > 0 ? p->crown_height : .56;
    int leaf_sites = p->leaf_sites > 0 ? p->leaf_sites : 7;
    for (int i = 0; i < stems; i++) {
        double a = phase + i * GOLDEN + .38 * qlcg_lp_rng_next(&skeleton);
        double dx = cos(a), dz = sin(a);
        double variation = qlcg_lp_rng_next(&skeleton);
        double reach = r * p->reach * (.57 + variation * .43);
        double low = p->habit == SHRUB_CAUDEX ? .23 : crown_height;
        double radius = fmin(h, r) * (p->habit == SHRUB_CAUDEX ? .095 : .038);
        double base[3] = {dx * reach * .16, origin[1], dz * reach * .16};
        double elbow[3] = {dx * reach * .47, origin[1] + h * low * (1.17 - variation * .48), dz * reach * .47};
        double end[3] = {dx * reach, elbow[1] + h * (.15 + p->bend * .34), dz * reach};
        if (bramble) {
            elbow[0] = dx * reach * .58;
            elbow[2] = dz * reach * .58;
            end[1] = elbow[1] * .48;
        }
        if (p->habit == SHRUB_PENCIL) radius *= 1.85;
        LpRgb stem = p->habit == SHRUB_PENCIL ? dark : bark;
        double arch[5][3];
        if (arching) {
            for (int k = 0; k <= 4; k++) {
                double u = k / 4.0;
                double dist = reach * (.12 + u * u);
                arch[k][0] = dx * dist;
                arch[k][1] = h * low * (1.17 - variation * .40) * u * (2.9 - (2.30 - p->bend * .30) * u);
                arch[k][2] = dz * dist;
                if (k) segment(b, arch[k - 1], arch[k], radius * (1.10 - .16 * k), .82, 5, stem);
            }
        } else {
            segment(b, base, elbow, radius, .63, 5, stem);
            segment(b, elbow, end, radius * .66, .45, 4, stem);
        }
        for (int j = 0; j < forks; j++) {
            double t = .22 + .78 * (j + 1) / forks;
            double node[3] = {elbow[0] + (end[0] - elbow[0]) * t,
                              elbow[1] + (end[1] - elbow[1]) * t,
                              elbow[2] + (end[2] - elbow[2]) * t};
            if (arching) {
                int k = (int)fmin(t * 4, 3);
                double u = t * 4 - k;
                for (int axis = 0; axis < 3; axis++)
                    node[axis] = arch[k][axis] + (arch[k + 1][axis] - arch[k][axis]) * u;
            }
            double bearing = a + (j % 2 ? 1 : -1) * (1.25 - .78 * t);
            double vx = cos(bearing), vz = sin(bearing);
            double length = r * (.34 - .13 * t) * s->shoot * (arching ? .54 : 1.0);
            double lift = h * (arching ? .035 - .085 * t : .115 + p->bend * t) * s->shoot;
            double tip[3] = {node[0] + vx * length, bramble ? fmax(node[1] + lift, h * .08) : node[1] + lift,
                             node[2] + vz * length};
            segment(b, node, tip, radius * .33, green_stem ? .66 : .26, 4,
                    green_stem ? leaf : bark);
            if (green_stem) {
                double join[3] = {(node[0] + tip[0]) * .5, (node[1] + tip[1]) * .5,
                                  (node[2] + tip[2]) * .5};
                double offshoot[3] = {tip[0] - vz * length * .43, tip[1] + h * .10 * s->shoot,
                                      tip[2] + vx * length * .43};
                segment(b, join, offshoot, radius * .24, .58, 4, dark);
                if (p->habit == SHRUB_JOINTED) {
                    double joint[3] = {tip[0] + vx * length * .35, tip[1] + h * .07 * s->shoot,
                                       tip[2] + vz * length * .35};
                    segment(b, tip, joint, radius * .21, .40, 4, dark);
                }
            }
            if (bramble) {
                double bend[3] = {node[0] - vz * r * .055, node[1] + h * .073, node[2] + vx * r * .055};
                double hook[3] = {bend[0] - dx * r * .040, bend[1] - h * .025, bend[2] - dz * r * .040};
                segment(b, node, bend, radius * .36, .57, 3, bark);
                segment(b, bend, hook, radius * .21, .04, 3, bark);
            } else if (p->habit == SHRUB_THORNY) {
                double thorn[3] = {node[0] - vz * r * .073, node[1] + h * .05, node[2] + vx * r * .073};
                segment(b, node, thorn, radius * .27, .04, 3, bark);
            }
            int leaf_count = qlcg_pa_js_round(leaf_sites * s->leaf_density);
            for (int k = 0; !green_stem && k < leaf_count; k++) {
                double u = 1 - k * .82 / leaf_count;
                double pos[3] = {node[0] + (tip[0] - node[0]) * u,
                                 node[1] + (tip[1] - node[1]) * u,
                                 node[2] + (tip[2] - node[2]) * u};
                double len = r * p->leaf * s->leaf_size * (.77 + .24 * u);
                LpOpts o = qlcg_lp_opts();
                o.pitch = .18 + .10 * (k % 3) - s->droop;
                o.cup = .23 + s->droop * .35;
                double angle = bearing + (k % 2 ? 1 : -1) * (k ? 1.08 : .15);
                LpRgb color = k % 2 ? dark : leaf;
                if (bramble) bramble_leaf(b, pos, angle, len, p->width, &o, color, dark);
                else qlcg_lp_petal(b, pos, angle, len, len * p->width, color, &o);
            }
            int slot = j * stems + i;
            double direction[3] = {vx, .115 + p->bend * t, vz};
            if (slot < flowers)
                branch_bloom(b, def, p, arching ? node : tip, direction, p->bloom_size, rng);
            if (slot < fruits)
                attach_fruit(b, def, p, tip, r, rng);
            if (snow_canopy && j == forks - 1)
                qlcg_lp_socket(b, tip, LP_SOCKET_SNOW_CANOPY);
        }
    }
}

void qlcg_pa_build_shrub(LpBuilder *b, const PdDef *def, int season, int form,
                         const ShrubProfile *p) {
    ShrubSeason s = seasonal_form(def, season, p);
    double h = fmax(.08, qlcg_pa_pnum(def, "height", 1.4));
    int uses_spread = p->habit == SHRUB_ROSETTE || p->habit == SHRUB_COLUMN ||
                      p->habit == SHRUB_THORNY || p->habit == SHRUB_BRAMBLE;
    double r = fmax(.04, qlcg_pa_pnum(def, uses_spread ? "spread" : "canopy", h * .65));
    r = fmin(r, h * 1.65);
    h *= p->height_scale > 0 ? p->height_scale : 1.0;
    r *= p->spread_scale > 0 ? p->spread_scale : 1.0;
    int simple = form == VEG_FORM_SIMPLE;
    int juvenile = def->state == PD_STATE_SEEDLING;
    int stems = juvenile ? 2 : simple ? (p->stems + 1) / 2 : p->stems;
    int forks = juvenile || simple ? 2 : p->forks;
    if (def->state == PD_STATE_WITHERED) h *= .56;

    int flower_count = qlcg_pa_js_round(qlcg_pa_pnum(def, "bloomCount", 0));
    int flowers = def->state == PD_STATE_FLOWERING ? qlcg_pa_bloom_count_for(def, flower_count, 5)
                : def->state == PD_STATE_NORMAL && season == p->bloom_season && def->bloom ? flower_count : 0;
    if (p->habit == SHRUB_ARCHING) flowers *= 2;
    int fruit_count = qlcg_pa_js_round(qlcg_pa_pnum(def, "fruitCount", 0));
    int fruits = p->build_fruit && (def->state == PD_STATE_NORMAL || def->state == PD_STATE_FRUITING)
               ? qlcg_pa_fruit_count_for(def, fruit_count, 3) : 0;
    if (simple) {
        flowers = flowers > 0;
        fruits = fruits > 0;
    }

    LpRng rng;
    qlcg_lp_rng_seed(&rng, def->build_seed);
    if (p->habit == SHRUB_ROSETTE) {
        int leaves = simple || juvenile ? 8 : p->rosette_leaves;
        build_rosette(b, def, h, r, &s, leaves, flowers, &rng);
    } else if (p->habit == SHRUB_COLUMN) {
        build_columns(b, def, p, h, r, stems, &s, flowers, fruits, &rng);
    } else {
        int snow_canopy = qlcg_pa_has_snow_target(def, "canopy");
        build_branches(b, def, p, &s, h, r, stems, forks, flowers, fruits, snow_canopy, &rng);
    }
    if (qlcg_pa_has_snow_target(def, "ground")) {
        double origin[3] = {0, 0, 0};
        qlcg_lp_socket(b, origin, LP_SOCKET_SNOW_GROUND);
    }
}
