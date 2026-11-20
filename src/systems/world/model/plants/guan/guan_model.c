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
        case PD_SEASON_NONE: return (ShrubSeason){ 1, 1, 1, 0 };
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

static double seg_basis(const double a[3], const double z[3],
                        double dir[3], double u[3], double v[3]) {
    for (int i = 0; i < 3; i++) dir[i] = z[i] - a[i];
    double len = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len <= 0) return 0.0;
    for (int i = 0; i < 3; i++) dir[i] /= len;
    double up[3] = {0.0, 1.0, 0.0};
    if (fabs(dir[1]) > 0.99) { up[0] = 1.0; up[1] = 0.0; }
    u[0] = up[1] * dir[2] - up[2] * dir[1];
    u[1] = up[2] * dir[0] - up[0] * dir[2];
    u[2] = up[0] * dir[1] - up[1] * dir[0];
    double ul = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (ul <= 1e-12) ul = 1.0;
    for (int i = 0; i < 3; i++) u[i] /= ul;
    v[0] = dir[1] * u[2] - dir[2] * u[1];
    v[1] = dir[2] * u[0] - dir[0] * u[2];
    v[2] = dir[0] * u[1] - dir[1] * u[0];
    return len;
}

static int seg_normals(const double a[3], const double z[3],
                       double radius, double taper, int sides,
                       double rot, double out[][3]) {
    double dir[3], u[3], v[3];
    double len = seg_basis(a, z, dir, u, v);
    if (len <= 0) return 0;
    int n = sides < 3 ? 3 : (sides > 12 ? 12 : sides);
    double slope = cos(TAU * .5 / n) * radius * (1.0 - taper) / len;
    for (int i = 0; i < n; i++) {
        double mid = rot + ((i + .5) / n) * TAU;
        double ca = cos(mid), sa = sin(mid);
        double nx = u[0] * ca + v[0] * sa + dir[0] * slope;
        double ny = u[1] * ca + v[1] * sa + dir[1] * slope;
        double nz = u[2] * ca + v[2] * sa + dir[2] * slope;
        double nl = sqrt(nx * nx + ny * ny + nz * nz);
        if (nl <= 1e-12) nl = 1.0;
        out[i][0] = nx / nl;
        out[i][1] = ny / nl;
        out[i][2] = nz / nl;
    }
    return n;
}

static double best_fork_rot(const double pn[][3], int pcnt,
                            const double n0[3], const double n1[3],
                            double nr, double ntaper, int fsides, double seed) {
    int fn_cnt = fsides < 3 ? 3 : (fsides > 12 ? 12 : fsides);
    double best_rot = seed, best_score = 2.0;
    for (int step = 0; step < 24; step++) {
        double cand = seed + (step / 24.0) * (TAU / fn_cnt);
        double fn[12][3], worst = 0.0;
        int fcnt = seg_normals(n0, n1, nr, ntaper, fn_cnt, cand, fn);
        for (int a = 0; a < fcnt; a++) {
            for (int c = 0; c < pcnt; c++) {
                double d = fabs(fn[a][0] * pn[c][0] + fn[a][1] * pn[c][1]
                                + fn[a][2] * pn[c][2]);
                if (d > worst) worst = d;
            }
            double vy = fabs(fn[a][1]) * .45;
            if (vy > worst) worst = vy;
        }
        if (worst < best_score) {
            best_score = worst;
            best_rot = cand;
        }
    }
    return best_rot;
}

typedef struct {
    double pos[3];
    double len;
    double n[13][3];
} PlacedLeaf;

typedef struct {
    double a[3], z[3], r;
    double n[5][3];
    int ncnt;
} StemSeg;

static double pt_seg_dist_sq(const double p[3], const double a[3],
                             const double z[3]) {
    double ab[3] = {z[0] - a[0], z[1] - a[1], z[2] - a[2]};
    double ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    double ab2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    double t = ab2 > 1e-12
             ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / ab2 : 0.0;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    double dx = ap[0] - ab[0] * t, dy = ap[1] - ab[1] * t,
           dz = ap[2] - ab[2] * t;
    return dx * dx + dy * dy + dz * dz;
}

static void push_stem_seg(StemSeg *segs, int *cnt, int max_cnt,
                          const double a[3], const double z[3],
                          double r, double taper, int sides, double rot) {
    if (*cnt >= max_cnt) return;
    StemSeg *dst = &segs[(*cnt)++];
    for (int ax = 0; ax < 3; ax++) { dst->a[ax] = a[ax]; dst->z[ax] = z[ax]; }
    dst->r = r;
    dst->ncnt = seg_normals(a, z, r, taper, sides > 5 ? 5 : sides, rot, dst->n);
}

static void norm3(double v[3]) {
    double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-12) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static void leaf_normals(double angle, double pitch, double cup,
                         double out[13][3]) {
    double dx = cos(angle), dz = sin(angle), sp = sin(pitch);
    out[0][0] = cup * dz + sp * dx;
    out[0][1] = -1.0;
    out[0][2] = sp * dz - cup * dx;
    out[1][0] = sp * dx - cup * dz;
    out[1][1] = -1.0;
    out[1][2] = sp * dz + cup * dx;
    norm3(out[0]);
    norm3(out[1]);
}

static double ov_penalty(double d, double thresh, double weight) {
    if (d <= thresh) return 0.0;
    double ov = (d - thresh) / (1.0 - thresh);
    return weight * ov * ov * ov + .001 * ov;
}

static void pick_leaf_pose(const double pos[3], double len,
                           const StemSeg *segs, int scnt,
                           const PlacedLeaf *placed, int pcnt,
                           double *angle, double *pitch, double *cup,
                           double out_n[13][3]) {
    static const double da[14] = {0, .30, -.32, .62, -.64, .95, -.95,
                                  1.35, -1.35, 1.85, -1.85, 2.50, -2.50, 3.14};
    static const double dp[8] = {0, .24, -.22, .48, -.38, .72, -.46, .95};
    double a0 = *angle, p0 = *pitch, c0 = *cup, best_score = 1e9;
    for (int ip = 0; ip < 8; ip++) {
        for (int ia = 0; ia < 14; ia++) {
            double ca = a0 + da[ia];
            double cp = fmin(1.42, fmax(.08, p0 + dp[ip]));
            double cc = ip >= 5 ? fmax(.04, c0 * .25)
                      : fmax(.06, ip >= 3 ? c0 * .55 : c0 + (ia % 2 ? -.06 : .06));
            if (ip == 0 && ia == 0) cc = c0;
            double ln[13][3], pen = 0.0, worst = 0.0;
            leaf_normals(ca, cp, cc, ln);
            for (int s = 0; s < scnt; s++) {
                double lim = len + segs[s].r + .01;
                double d2 = pt_seg_dist_sq(pos, segs[s].a, segs[s].z);
                if (d2 > lim * lim) continue;
                double own = d2 <= (segs[s].r + .004) * (segs[s].r + .004) ? 10.0 : 1.0;
                for (int k = 0; k < segs[s].ncnt; k++) {
                    double pd = fabs((pos[0] - segs[s].a[0]) * segs[s].n[k][0]
                                     + (pos[1] - segs[s].a[1]) * segs[s].n[k][1]
                                     + (pos[2] - segs[s].a[2]) * segs[s].n[k][2]);
                    if (pd > segs[s].r + len * .14 + .005) continue;
                    double w = own * (pd <= segs[s].r + .005 ? 200.0 : 20.0);
                    for (int m = 0; m < 2; m++) {
                        double d = fabs(ln[m][0] * segs[s].n[k][0]
                                        + ln[m][1] * segs[s].n[k][1]
                                        + ln[m][2] * segs[s].n[k][2]);
                        if (d > worst) worst = d;
                        pen += ov_penalty(d, .992, w);
                    }
                }
            }
            for (int p = 0; p < pcnt; p++) {
                double dx = pos[0] - placed[p].pos[0];
                double dy = pos[1] - placed[p].pos[1];
                double dz = pos[2] - placed[p].pos[2];
                double rsum = len + placed[p].len + .005;
                if (dx * dx + dy * dy + dz * dz > rsum * rsum) continue;
                double mlen = len > placed[p].len ? len : placed[p].len;
                for (int q = 0; q < 2; q++) {
                    double pd = fabs(dx * placed[p].n[q][0] + dy * placed[p].n[q][1]
                                     + dz * placed[p].n[q][2]);
                    if (pd > mlen * .14 + .005) continue;
                    double w = pd <= .005 ? 100.0 : 10.0;
                    for (int m = 0; m < 2; m++) {
                        double d = fabs(ln[m][0] * placed[p].n[q][0]
                                        + ln[m][1] * placed[p].n[q][1]
                                        + ln[m][2] * placed[p].n[q][2]);
                        if (d > worst) worst = d;
                        pen += ov_penalty(d, .992, w);
                    }
                }
            }
            double score = pen + worst * 1e-4;
            if (score < best_score) {
                best_score = score;
                *angle = ca; *pitch = cp; *cup = cc;
                for (int m = 0; m < 2; m++)
                    for (int k = 0; k < 3; k++) out_n[m][k] = ln[m][k];
            }
            if (pen == 0.0) return;
        }
    }
}

static void segment_rot(LpBuilder *b, const double a[3], const double z[3],
                        double radius, double taper, int sides,
                        double rot, LpRgb color) {
    double dir[3], u[3], v[3];
    if (seg_basis(a, z, dir, u, v) <= 0) return;
    int n = sides < 3 ? 3 : sides;
    double r0[12][3], r1[12][3];
    if (n > 12) n = 12;
    for (int i = 0; i < n; i++) {
        double ang = rot + ((double)i / (double)n) * TAU;
        double ca = cos(ang), sa = sin(ang);
        double dx = u[0] * ca + v[0] * sa;
        double dy = u[1] * ca + v[1] * sa;
        double dz = u[2] * ca + v[2] * sa;
        r0[i][0] = a[0] + dx * radius;
        r0[i][1] = a[1] + dy * radius;
        r0[i][2] = a[2] + dz * radius;
        r1[i][0] = z[0] + dx * (radius * taper);
        r1[i][1] = z[1] + dy * (radius * taper);
        r1[i][2] = z[2] + dz * (radius * taper);
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        qlcg_lp_quad(b, r0[i], r0[j], r1[j], r1[i], color, LP_MAT_SOLID);
    }
    for (int i = 1; i < n - 1; i++)
        qlcg_lp_triangle(b, r1[0], r1[i], r1[i + 1], color, LP_MAT_SOLID);
}

static void segment(LpBuilder *b, const double a[3], const double z[3],
                    double radius, double taper, int sides, LpRgb color) {
    segment_rot(b, a, z, radius, taper, sides, 0.0, color);
}

static void shrub_flower_at(LpBuilder *b, const PdDef *def,
                            const double at[3], double r, LpRng *rng) {
    LpOpts co = qlcg_lp_opts();
    co.rings = 1;
    co.floor_on = 1.0;
    co.squash = 0.4;
    co.rot_y = at[0] * 11.7 + at[2] * 7.3;
    qlcg_lp_dome(b, at, r * .22, r * .36, r * .22, 5,
                 qlcg_pa_tone(def, "foliageDark", 1), &co);
    qlcg_pa_flower_at(b, def, at, r, rng);
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
            shrub_flower_at(b, def, end, r * .065, rng);
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
    double r0 = r * .25 * (.79 + .21 * s->leaf_size);
    for (int i = 0; i < stems; i++) {
        double a = i * GOLDEN, offset = i ? r * (.34 + .10 * i) : 0;
        double start[3] = {cos(a) * offset, 0, sin(a) * offset};
        double height = h * (i ? .69 - .075 * i : .91) * (.80 + .20 * s->shoot);
        double radius = r * (i ? .18 : .25) * (.79 + .21 * s->leaf_size);
        if (i) {
            double hub[3] = {0, fmin(height * .25, r0 * .48), 0};
            double arm[3] = {start[0], fmin(height * .30, radius * .52), start[2]};
            segment_rot(b, hub, arm, radius * .58, .75, 5, i * .13, green);
        }
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
        if (i < flowers) shrub_flower_at(b, def, top, r * .19, rng);
        if (i < fruits) attach_fruit(b, def, p, top, r, rng);
    }
}

static void branch_bloom(LpBuilder *b, const PdDef *def, const ShrubProfile *p,
                         const double tip[3], const double direction[3], double size, LpRng *rng) {
    double brot = tip[0] * 11.3 + tip[2] * 7.9 + .47;
    if (p->habit == SHRUB_ARCHING) {
        for (int side = -1; side <= 1; side += 2) {
            double at[3] = {tip[0] - side * direction[2] * size * .83, tip[1] + size * .22,
                            tip[2] + side * direction[0] * size * .83};
            segment_rot(b, tip, at, size * .064, .55, 3, brot + side * .41,
                        qlcg_pa_tone(def, "barkLight", 1));
            shrub_flower_at(b, def, at, size, rng);
        }
    } else if (p->habit == SHRUB_CATKIN || p->habit == SHRUB_PANICLE) {
        double axis[3] = {direction[0] * size * 3, size * (p->habit == SHRUB_CATKIN ? -5 : 4),
                          direction[2] * size * 3};
        double end[3] = {tip[0] + axis[0], tip[1] + axis[1], tip[2] + axis[2]};
        LpRgb c = qlcg_pa_tone(def, "bloom", 1);
        segment_rot(b, tip, end, size * .15, .25, 3, brot, c);
        LpOpts o = qlcg_lp_opts();
        o.mat = LP_MAT_BLOOM;
        for (int j = 1; j <= 3; j++) {
            double t = j * .32;
            double pos[3] = {tip[0] + t * axis[0], tip[1] + t * axis[1], tip[2] + t * axis[2]};
            o.rot_y = brot + j * .41;
            qlcg_lp_dome(b, pos, size * (1.5 - t), size * .8, size * (1.5 - t), 5, c, &o);
        }
    } else if (p->habit == SHRUB_FEATHER) {
        double axis[3] = {direction[0] * size * 2.75, direction[1] * size * 2.75,
                          direction[2] * size * 2.75};
        double end[3] = {tip[0] - axis[0], tip[1] - axis[1], tip[2] - axis[2]};
        segment_rot(b, tip, end, size * .08, .55, 3, brot, qlcg_pa_tone(def, "barkLight", 1));
        for (int j = 0; j < 3; j++) {
            double offset = (j - 2) * size * 1.35;
            double pos[3] = {tip[0] + direction[0] * offset, tip[1] + direction[1] * offset,
                             tip[2] + direction[2] * offset};
            shrub_flower_at(b, def, pos, size, rng);
        }
    } else {
        shrub_flower_at(b, def, tip, size, rng);
    }
}

static void push_placed_leaf(PlacedLeaf *placed, int *cnt, int max_cnt,
                             const double pos[3], double len,
                             const double ln[13][3]) {
    if (*cnt >= max_cnt) return;
    PlacedLeaf *dst = &placed[(*cnt)++];
    for (int ax = 0; ax < 3; ax++) dst->pos[ax] = pos[ax];
    dst->len = len;
    for (int m = 0; m < 2; m++)
        for (int ax = 0; ax < 3; ax++) dst->n[m][ax] = ln[m][ax];
}

static void bramble_leaf(LpBuilder *b, const double pos[3], double a,
                         double len, double width, const LpOpts *opts,
                         LpRgb leaf, LpRgb vein,
                         StemSeg *segs, int *seg_cnt, int max_segs,
                         PlacedLeaf *placed, int *placed_cnt, int max_placed) {
    double join[3] = {pos[0] + cos(a) * len * .24, pos[1] + len * .11, pos[2] + sin(a) * len * .24};
    double fn[32][3];
    int fcnt = 0;
    for (int s = 0; s < *seg_cnt && fcnt < 32; s++) {
        double lim = len * .35 + segs[s].r + .01;
        if (pt_seg_dist_sq(pos, segs[s].a, segs[s].z) > lim * lim) continue;
        for (int k = 0; k < segs[s].ncnt && fcnt < 32; k++, fcnt++)
            for (int ax = 0; ax < 3; ax++) fn[fcnt][ax] = segs[s].n[k][ax];
    }
    double jrot = best_fork_rot(fn, fcnt, pos, join, len * .024, .66, 5,
                                a * .83 + pos[0] * 11.7 + pos[2] * 7.3 + .47);
    segment_rot(b, pos, join, len * .024, .66, 5, jrot, vein);
    push_stem_seg(segs, seg_cnt, max_segs, pos, join, len * .024, .66, 5, jrot);
    LpOpts co = *opts;
    double ca = a, ln[13][3];
    pick_leaf_pose(join, len * .77, segs, *seg_cnt, placed, *placed_cnt,
                   &ca, &co.pitch, &co.cup, ln);
    qlcg_lp_petal(b, join, ca, len * .77, len * width * .65, leaf, &co);
    push_placed_leaf(placed, placed_cnt, max_placed, join, len * .77, ln);
    for (int side = -1; side <= 1; side += 2) {
        LpOpts so = *opts;
        so.pitch += side > 0 ? .22 : -.14;
        double sa = a + side * 1.07;
        pick_leaf_pose(join, len * .58, segs, *seg_cnt, placed, *placed_cnt,
                       &sa, &so.pitch, &so.cup, ln);
        qlcg_lp_petal(b, join, sa, len * .58, len * width * .53, leaf, &so);
        push_placed_leaf(placed, placed_cnt, max_placed, join, len * .58, ln);
    }
}

typedef struct {
    int i;
    int j;
    double t;
    double bearing;
    double vx;
    double vz;
    double node[3];
    double tip[3];
} ShrubForkSite;

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
        o.rings = 2;
        o.floor_on = 1;
        o.squash = 0.0;
        qlcg_lp_dome(b, origin, r * .40, h * .48, r * .36, 7, bark, &o);
        origin[1] = h * .28;
    } else {
        double col_r = fmax(r * p->reach * .12, fmin(h, r) * .08);
        double col_h = fmin(h * .045, fmin(h, r) * .08);
        LpOpts co = qlcg_lp_opts();
        co.rot_y = .22;
        co.radius_ratio = .55;
        co.cap = 0.0;
        qlcg_lp_cone(b, origin, col_r * 1.15, col_r * 1.15, col_h, 7, bark, &co);
    }
    LpRng skeleton = *rng;
    double phase = qlcg_lp_rng_next(&skeleton) * TAU;
    double crown_height = p->crown_height > 0 ? p->crown_height : .56;
    int leaf_sites = p->leaf_sites > 0 ? p->leaf_sites : 7;
    PlacedLeaf placed[512];
    int placed_cnt = 0;
    StemSeg segs[256];
    int seg_cnt = 0;
    ShrubForkSite sites[256];
    int site_cnt = 0;
    for (int i = 0; i < stems; i++) {
        double a = phase + i * GOLDEN + .38 * qlcg_lp_rng_next(&skeleton);
        double dx = cos(a), dz = sin(a);
        double variation = qlcg_lp_rng_next(&skeleton);
        double reach = r * p->reach * (.57 + variation * .43);
        double low = p->habit == SHRUB_CAUDEX ? .23 : crown_height;
        double radius = fmin(h, r) * (p->habit == SHRUB_CAUDEX ? .095 : .038);
        double base_r = p->habit == SHRUB_CAUDEX ? reach * .12 : reach * (.042 + .007 * (double)(i % 10));
        double base[3] = {dx * base_r, origin[1], dz * base_r};
        double elbow[3] = {dx * reach * .47, origin[1] + h * low * (1.17 - variation * .48), dz * reach * .47};
        double end[3] = {dx * reach, elbow[1] + h * (.15 + p->bend * .34), dz * reach};
        if (bramble) {
            elbow[0] = dx * reach * .58;
            elbow[2] = dz * reach * .58;
            end[1] = elbow[1] * .48;
        }
        if (p->habit == SHRUB_PENCIL) radius *= 1.85;
        LpRgb stem = p->habit == SHRUB_PENCIL ? dark : bark;
        double srot = (double)i * GOLDEN + sin((double)i * 3.13 + .4) * .57 + .19;
        int ms = 5;
        double arch[5][3];
        if (arching) {
            for (int k = 0; k <= 4; k++) {
                double u = k / 4.0;
                double dist = reach * (.05 + u * u);
                arch[k][0] = dx * dist;
                arch[k][1] = h * low * (1.17 - variation * .40) * u * (2.9 - (2.30 - p->bend * .30) * u);
                arch[k][2] = dz * dist;
                if (k) {
                    double ar = radius * (1.10 - .16 * k);
                    double arot = srot + k * .6283;
                    segment_rot(b, arch[k - 1], arch[k], ar, .82, ms, arot, stem);
                    push_stem_seg(segs, &seg_cnt, 256, arch[k - 1], arch[k],
                                  ar, .82, ms, arot);
                }
            }
        } else {
            segment_rot(b, base, elbow, radius, .63, ms, srot, stem);
            push_stem_seg(segs, &seg_cnt, 256, base, elbow, radius, .63, ms, srot);
            segment_rot(b, elbow, end, radius * .66, .45, ms, srot + .6283, stem);
            push_stem_seg(segs, &seg_cnt, 256, elbow, end,
                          radius * .66, .45, ms, srot + .6283);
        }
        for (int j = 0; j < forks; j++) {
            double t = .20 + .72 * (j + 1) / forks;
            double node[3] = {elbow[0] + (end[0] - elbow[0]) * t,
                              elbow[1] + (end[1] - elbow[1]) * t,
                              elbow[2] + (end[2] - elbow[2]) * t};
            int ak = 0;
            if (arching) {
                ak = (int)fmin(t * 4, 3);
                double u = t * 4 - ak;
                for (int axis = 0; axis < 3; axis++)
                    node[axis] = arch[ak][axis] + (arch[ak + 1][axis] - arch[ak][axis]) * u;
            }
            double bearing = a + (j % 2 ? 1 : -1) * (1.25 - .78 * t);
            double vx = cos(bearing), vz = sin(bearing);
            double length = r * (.34 - .13 * t) * s->shoot * (arching ? .54 : 1.0);
            double lift = h * (arching ? .035 - .085 * t : .115 + p->bend * t) * s->shoot;
            double tip[3] = {node[0] + vx * length, bramble ? fmax(node[1] + lift, h * .08) : node[1] + lift,
                             node[2] + vz * length};
            int fs = 5;
            double pn[36][3];
            int pcnt = 0;
            if (arching) {
                int s0 = ak > 0 ? ak : 1, s1 = ak + 2 <= 4 ? ak + 2 : 4;
                for (int seg = s0; seg <= s1; seg++) {
                    pcnt += seg_normals(arch[seg - 1], arch[seg],
                                        radius * (1.10 - .16 * seg), .82, ms,
                                        srot + seg * .6283, pn + pcnt);
                }
            } else {
                pcnt += seg_normals(base, elbow, radius, .63, ms, srot, pn + pcnt);
                pcnt += seg_normals(elbow, end, radius * .66, .45, ms,
                                    srot + .6283, pn + pcnt);
                double d_uv[3], u_tmp[3], v_tmp[3];
                if (seg_basis(elbow, end, d_uv, u_tmp, v_tmp) > 0) {
                    for (int ax = 0; ax < 3; ax++) pn[pcnt][ax] = d_uv[ax];
                    pcnt++;
                }
                if (seg_basis(base, elbow, d_uv, u_tmp, v_tmp) > 0) {
                    for (int ax = 0; ax < 3; ax++) pn[pcnt][ax] = d_uv[ax];
                    pcnt++;
                }
            }
            double nr = radius * .27;
            double ntaper = green_stem ? .66 : .26;
            double trot = best_fork_rot(pn, pcnt, node, tip, nr, ntaper, fs,
                                        srot + .3142 + (j % 2 ? .05 : -.05));
            segment_rot(b, node, tip, nr, ntaper, fs, trot,
                        green_stem ? leaf : bark);
            push_stem_seg(segs, &seg_cnt, 256, node, tip, nr, ntaper, fs, trot);
            if (green_stem) {
                int fcnt = seg_normals(node, tip, nr, ntaper, fs, trot, pn + pcnt);
                pcnt += fcnt;
                double d_nt[3], ut[3], vt[3];
                if (seg_basis(node, tip, d_nt, ut, vt) > 0) {
                    for (int ax = 0; ax < 3; ax++) pn[pcnt][ax] = d_nt[ax];
                    pcnt++;
                }
                double join[3] = {(node[0] + tip[0]) * .5, (node[1] + tip[1]) * .5,
                                  (node[2] + tip[2]) * .5};
                double offshoot[3] = {tip[0] - vz * length * .43, tip[1] + h * .10 * s->shoot,
                                      tip[2] + vx * length * .43};
                double jrad = nr * .72;
                double jrot = best_fork_rot(pn, pcnt, join, offshoot, jrad, .58, 5,
                                            trot + .57 + (double)j * .41);
                segment_rot(b, join, offshoot, jrad, .58, 5, jrot, dark);
                push_stem_seg(segs, &seg_cnt, 256, join, offshoot, jrad, .58, 5, jrot);
                if (p->habit == SHRUB_JOINTED) {
                    pcnt += seg_normals(join, offshoot, jrad, .58, 5, jrot, pn + pcnt);
                    double joint[3] = {tip[0] + vx * length * .35, tip[1] + h * .07 * s->shoot,
                                       tip[2] + vz * length * .35};
                    double krad = nr * .52;
                    double krot = best_fork_rot(pn, pcnt, tip, joint, krad, .40, 3,
                                                trot + .93 + .29 * (double)((i + j) % 3));
                    segment_rot(b, tip, joint, krad, .40, 3, krot, dark);
                    push_stem_seg(segs, &seg_cnt, 256, tip, joint, krad, .40, 3, krot);
                }
            }
            if (bramble) {
                double bend[3] = {node[0] - vz * r * .055, node[1] + h * .073, node[2] + vx * r * .055};
                double hook[3] = {bend[0] - dx * r * .040, bend[1] - h * .025, bend[2] - dz * r * .040};
                segment_rot(b, node, bend, radius * .36, .57, 5, trot + .43, bark);
                segment_rot(b, bend, hook, radius * .21, .04, 3, trot + .97, bark);
                push_stem_seg(segs, &seg_cnt, 256, node, bend, radius * .36, .57, 5, trot + .43);
                push_stem_seg(segs, &seg_cnt, 256, bend, hook, radius * .21, .04, 3, trot + .97);
            } else if (p->habit == SHRUB_THORNY) {
                double tb[3] = {node[0] + (tip[0] - node[0]) * .35,
                                node[1] + (tip[1] - node[1]) * .35,
                                node[2] + (tip[2] - node[2]) * .35};
                double thorn[3] = {tb[0] - vz * r * .073 + vx * r * .025,
                                   tb[1] + h * .05, tb[2] + vx * r * .073 + vz * r * .025};
                double throt = trot + 1.17 + (double)((i + j) % 5) * .37;
                segment_rot(b, tb, thorn, radius * .20, .05, 3, throt, bark);
                push_stem_seg(segs, &seg_cnt, 256, tb, thorn, radius * .20, .05, 3, throt);
            }
            if (site_cnt < 256) {
                ShrubForkSite *sf = &sites[site_cnt++];
                sf->i = i;
                sf->j = j;
                sf->t = t;
                sf->bearing = bearing;
                sf->vx = vx;
                sf->vz = vz;
                for (int axis = 0; axis < 3; axis++) {
                    sf->node[axis] = node[axis];
                    sf->tip[axis] = tip[axis];
                }
            }
        }
    }
    for (int si = 0; si < site_cnt; si++) {
        ShrubForkSite *sf = &sites[si];
        int i = sf->i, j = sf->j, slot = j * stems + i;
        double t = sf->t, bearing = sf->bearing, vx = sf->vx, vz = sf->vz;
        const double *node = sf->node, *tip = sf->tip;
        int leaf_count = qlcg_pa_js_round(leaf_sites * s->leaf_density);
        for (int k = 0; !green_stem && k < leaf_count; k++) {
            double u = .85 - k * .69 / leaf_count;
            double pos[3] = {node[0] + (tip[0] - node[0]) * u,
                             node[1] + (tip[1] - node[1]) * u,
                             node[2] + (tip[2] - node[2]) * u};
            double len = r * p->leaf * s->leaf_size * (.77 + .24 * u);
            LpOpts o = qlcg_lp_opts();
            double seed = pos[0] * 13.7 + pos[1] * 17.3 + pos[2] * 11.9 + (double)(i * 7 + j * 3 + k);
            double tw = .11 * sin(seed);
            o.pitch = .40 + .10 * (double)((k + j * 2) % 5) + (k == 0 ? .32 : 0.0)
                      + tw - s->droop * .30 + (k % 2 ? .08 : -.06);
            o.cup = .24 + .08 * (double)((i + j * 2 + k * 3) % 5)
                    + .04 * cos(seed * 1.7) + s->droop * .25;
            double yaw = k ? (k % 2 ? 1.0 : -1.0) * 1.15
                       : (slot < flowers ? (j % 2 ? .62 : -.62) : .18);
            double ang_off = .09 * cos(seed * 1.1) + (k % 2 ? .11 : -.09)
                             + .05 * (double)(((k + i) % 3) - 1);
            double angle = bearing + yaw + ang_off;
            LpRgb color = k % 2 ? dark : leaf;
            if (bramble) {
                bramble_leaf(b, pos, angle, len, p->width, &o, color, dark,
                             segs, &seg_cnt, 256, placed, &placed_cnt, 512);
            } else {
                double ln[13][3];
                pick_leaf_pose(pos, len, segs, seg_cnt,
                               placed, placed_cnt, &angle, &o.pitch, &o.cup, ln);
                push_placed_leaf(placed, &placed_cnt, 512, pos, len, ln);
                qlcg_lp_petal(b, pos, angle, len, len * p->width, color, &o);
            }
        }
        double direction[3] = {vx, .115 + p->bend * t, vz};
        if (slot < flowers)
            branch_bloom(b, def, p, arching ? node : tip, direction, p->bloom_size, rng);
        if (slot < fruits)
            attach_fruit(b, def, p, tip, r, rng);
        if (snow_canopy && j == forks - 1)
            qlcg_lp_socket(b, tip, LP_SOCKET_SNOW_CANOPY);
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
