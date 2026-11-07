/* 低模几何构建器实现 */
#pragma float_control(precise, on)
#pragma fp_contract(off)

#include "lp_builder.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TAU 6.283185307179586476925286766559
#define LP_O(o, f, d) ((o) && !isnan((o)->f) ? (o)->f : (d))
#define LP_MAT(o) ((o) && (o)->mat >= 0 ? (o)->mat : LP_MAT_SOLID)

static double js_round(double v) { return floor(v + 0.5); }

LpOpts qlcg_lp_opts(void) {
    LpOpts o;
    o.rot_y = NAN; o.jitter = NAN; o.cap = NAN; o.floor_on = NAN;
    o.rings = NAN; o.squash = NAN; o.radius_ratio = NAN; o.bend = NAN;
    o.both = NAN; o.pitch = NAN; o.cup = NAN; o.sides = NAN;
    o.tip_ratio = NAN; o.rand = NULL; o.mat = -1;
    return o;
}

void qlcg_lp_init(LpBuilder *b) {
    b->faces = NULL;
    b->count = 0;
    b->cap = 0;
    b->socket_count = 0;
}

void qlcg_lp_clear(LpBuilder *b) {
    for (int i = 0; i < b->count; i++) free(b->faces[i].pos);
    b->count = 0;
    b->socket_count = 0;
}

void qlcg_lp_socket(LpBuilder *b, const double translation[3], int kind) {
    if (b->socket_count >= LP_SOCKET_CAPACITY) return;
    LpSocket *socket = &b->sockets[b->socket_count++];
    memcpy(socket->translation, translation, sizeof(socket->translation));
    socket->kind = kind;
}

void qlcg_lp_free(LpBuilder *b) {
    qlcg_lp_clear(b);
    free(b->faces);
    b->faces = NULL;
    b->cap = 0;
}

static LpFace *grow(LpBuilder *b) {
    if (b->count >= b->cap) {
        int next = b->cap ? b->cap * 2 : 64;
        LpFace *nf = (LpFace *)realloc(b->faces, (size_t)next * sizeof(LpFace));
        if (!nf) return NULL;
        b->faces = nf;
        b->cap = next;
    }
    return &b->faces[b->count];
}

void qlcg_lp_push(LpBuilder *b, const double *pos, int n, LpRgb col, const double nrm[3], int mat) {
    LpFace *f = grow(b);
    if (!f) return;
    f->pos = (double *)malloc((size_t)n * 3 * sizeof(double));
    if (!f->pos) return;
    memcpy(f->pos, pos, (size_t)n * 3 * sizeof(double));
    f->n = n;
    double s = qlcg_lp_shade_of(nrm[0], nrm[1], nrm[2]);
    f->col[0] = (unsigned char)qlcg_lp_clamp_byte(col.r * s);
    f->col[1] = (unsigned char)qlcg_lp_clamp_byte(col.g * s);
    f->col[2] = (unsigned char)qlcg_lp_clamp_byte(col.b * s);
    f->nrm[0] = nrm[0]; f->nrm[1] = nrm[1]; f->nrm[2] = nrm[2];
    f->mat = mat;
    f->wind = (mat == LP_MAT_SNOW) ? LP_WIND_CROWN :
              ((mat == LP_MAT_SOLID) ? col.wind : LP_WIND_SOLID);
    b->count++;
}

void qlcg_lp_triangle(LpBuilder *b, const double a[3], const double bb[3], const double c[3], LpRgb col, int mat) {
    double pos[9] = { a[0], a[1], a[2], bb[0], bb[1], bb[2], c[0], c[1], c[2] };
    double nrm[3];
    qlcg_lp_face_normal(a, bb, c, nrm);
    qlcg_lp_push(b, pos, 3, col, nrm, mat);
}

void qlcg_lp_quad(LpBuilder *b, const double a[3], const double bb[3], const double c[3], const double d[3], LpRgb col, int mat) {
    double nrm[3];
    qlcg_lp_face_normal(a, bb, c, nrm);
    double p1[9] = { a[0], a[1], a[2], bb[0], bb[1], bb[2], c[0], c[1], c[2] };
    qlcg_lp_push(b, p1, 3, col, nrm, mat);
    double p2[9] = { a[0], a[1], a[2], c[0], c[1], c[2], d[0], d[1], d[2] };
    qlcg_lp_push(b, p2, 3, col, nrm, mat);
}

void qlcg_lp_prism(LpBuilder *b, const double center[3], double rx, double rz,
                   double bottom, double top, double sides, LpRgb color, const LpOpts *o) {
    int n = (int)(sides < 3.0 ? 3 : js_round(sides));
    if (n < 3) n = 3;
    double rot = LP_O(o, rot_y, 0.0);
    double jit = LP_O(o, jitter, 0.0);
    LpRng *rand = o ? o->rand : NULL;
    int mat = LP_MAT(o);
    double cx = center[0], cy = center[1], cz = center[2];

    double *bot = (double *)malloc((size_t)n * 3 * sizeof(double));
    double *topr = (double *)malloc((size_t)n * 3 * sizeof(double));
    if (!bot || !topr) { free(bot); free(topr); return; }
    for (int i = 0; i < n; i++) {
        double a = rot + ((double)i / (double)n) * TAU;
        double k = jit > 0.0 ? 1.0 + (qlcg_lp_rand_or_half(rand) * 2.0 - 1.0) * jit : 1.0;
        double x = cx + cos(a) * rx * k;
        double z = cz + sin(a) * rz * k;
        bot[i * 3 + 0] = x; bot[i * 3 + 1] = cy + bottom; bot[i * 3 + 2] = z;
        topr[i * 3 + 0] = x; topr[i * 3 + 1] = cy + top; topr[i * 3 + 2] = z;
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        qlcg_lp_quad(b, &bot[i * 3], &topr[i * 3], &topr[j * 3], &bot[j * 3], color, mat);
    }
    if (fabs(bottom) > 1e-6) {
        for (int i = 1; i < n - 1; i++)
            qlcg_lp_triangle(b, &bot[0], &bot[i * 3], &bot[(i + 1) * 3], color, mat);
    }
    if (!(o && !isnan(o->cap) && o->cap == 0.0)) {
        for (int i = 1; i < n - 1; i++)
            qlcg_lp_triangle(b, &topr[0], &topr[(i + 1) * 3], &topr[i * 3], color, mat);
    }
    free(bot);
    free(topr);
}

void qlcg_lp_cone(LpBuilder *b, const double base[3], double rx, double rz,
                  double height, double sides, LpRgb color, const LpOpts *o) {
    if (color.wind == LP_WIND_LEAF) color.wind = LP_WIND_CROWN;
    int n = (int)(sides < 3.0 ? 3 : js_round(sides));
    if (n < 3) n = 3;
    double rot = LP_O(o, rot_y, 0.0);
    double jit = LP_O(o, jitter, 0.0);
    double ratio = LP_O(o, radius_ratio, 0.0);
    LpRng *rand = o ? o->rand : NULL;
    int mat = LP_MAT(o);
    double cx = base[0], cy = base[1], cz = base[2];

    double *ring = (double *)malloc((size_t)n * 3 * sizeof(double));
    if (!ring) return;
    for (int i = 0; i < n; i++) {
        double a = rot + ((double)i / (double)n) * TAU;
        double r = jit > 0.0 ? 1.0 + (qlcg_lp_rand_or_half(rand) * 2.0 - 1.0) * jit : 1.0;
        ring[i * 3 + 0] = cx + cos(a) * rx * r;
        ring[i * 3 + 1] = cy;
        ring[i * 3 + 2] = cz + sin(a) * rz * r;
    }
    if (ratio > 1e-6) {
        double *top = (double *)malloc((size_t)n * 3 * sizeof(double));
        if (!top) { free(ring); return; }
        for (int i = 0; i < n; i++) {
            top[i * 3 + 0] = cx + (ring[i * 3 + 0] - cx) * ratio;
            top[i * 3 + 1] = cy + height;
            top[i * 3 + 2] = cz + (ring[i * 3 + 2] - cz) * ratio;
        }
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            qlcg_lp_quad(b, &ring[i * 3], &top[i * 3], &top[j * 3], &ring[j * 3], color, mat);
        }
        for (int i = 1; i < n - 1; i++)
            qlcg_lp_triangle(b, &top[0], &top[(i + 1) * 3], &top[i * 3], color, mat);
        free(top);
        free(ring);
        return;
    }
    double apex[3] = { cx, cy + height, cz };
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        qlcg_lp_triangle(b, &ring[i * 3], apex, &ring[j * 3], color, mat);
    }
    if (o && !isnan(o->cap) && o->cap != 0.0) {
        for (int i = 1; i < n - 1; i++)
            qlcg_lp_triangle(b, &ring[0], &ring[i * 3], &ring[(i + 1) * 3], color, mat);
    }
    free(ring);
}

/* dome 的逐圈取点，消耗随机数的方式与 base 一致 */
static void dome_ring(double *out, int n, double rot, double jit, LpRng *rand,
                      double cx, double cz, double rx, double rz, double rScale, double y) {
    for (int i = 0; i < n; i++) {
        double a = rot + ((double)i / (double)n) * TAU;
        double k = jit > 0.0 ? 1.0 + (qlcg_lp_rand_or_half(rand) * 2.0 - 1.0) * jit : 1.0;
        out[i * 3 + 0] = cx + cos(a) * rx * rScale * k;
        out[i * 3 + 1] = y;
        out[i * 3 + 2] = cz + sin(a) * rz * rScale * k;
    }
}

void qlcg_lp_dome(LpBuilder *b, const double center[3], double rx, double ry, double rz,
                  double sides, LpRgb color, const LpOpts *o) {
    if (color.wind == LP_WIND_LEAF) color.wind = LP_WIND_CROWN;
    int n = (int)(sides < 3.0 ? 3 : js_round(sides));
    if (n < 3) n = 3;
    double rot = LP_O(o, rot_y, 0.0);
    double jit = LP_O(o, jitter, 0.0);
    double rings = LP_O(o, rings, 0.0);
    int ring_n = rings < 0.0 ? 0 : (int)js_round(rings);
    LpRng *rand = o ? o->rand : NULL;
    int mat = LP_MAT(o);
    double cx = center[0], cy = center[1], cz = center[2];

    double *eq = (double *)malloc((size_t)n * 3 * sizeof(double));
    double *prev = (double *)malloc((size_t)n * 3 * sizeof(double));
    double *cur = (double *)malloc((size_t)n * 3 * sizeof(double));
    if (!eq || !prev || !cur) { free(eq); free(prev); free(cur); return; }

    dome_ring(eq, n, rot, jit, rand, cx, cz, rx, rz, 1.0, cy);
    memcpy(prev, eq, (size_t)n * 3 * sizeof(double));
    for (int l = 1; l <= ring_n; l++) {
        double phi = ((double)l / (double)(ring_n + 1)) * (3.14159265358979323846 / 2.0);
        dome_ring(cur, n, rot, jit, rand, cx, cz, rx, rz, cos(phi), cy + ry * sin(phi));
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            qlcg_lp_quad(b, &prev[i * 3], &cur[i * 3], &cur[j * 3], &prev[j * 3], color, mat);
        }
        memcpy(prev, cur, (size_t)n * 3 * sizeof(double));
    }
    double apex[3] = { cx, cy + ry, cz };
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        qlcg_lp_triangle(b, &prev[i * 3], apex, &prev[j * 3], color, mat);
    }
    if (!(o && !isnan(o->floor_on) && o->floor_on == 0.0)) {
        double belly[3] = { cx, cy - ry * LP_O(o, squash, 1.0), cz };
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            qlcg_lp_triangle(b, &eq[i * 3], &eq[j * 3], belly, color, mat);
        }
    }
    free(eq); free(prev); free(cur);
}

void qlcg_lp_ico(LpBuilder *b, const double center[3], const double radius[3],
                 double sides, LpRgb color, const LpOpts *o) {
    if (color.wind == LP_WIND_LEAF) color.wind = LP_WIND_CROWN;
    int n = (int)(sides < 3.0 ? 3 : js_round(sides));
    if (n < 3) n = 3;
    double rings = LP_O(o, rings, 1.0);
    int ring_n = rings < 1.0 ? 1 : (int)js_round(rings);
    double rot = LP_O(o, rot_y, 0.0);
    double jit = LP_O(o, jitter, 0.0);
    LpRng *rand = o ? o->rand : NULL;
    int mat = LP_MAT(o);
    double cx = center[0], cy = center[1], cz = center[2];
    double rx = radius[0], ry = radius[1], rz = radius[2];

    double *rows = (double *)malloc((size_t)ring_n * (size_t)n * 3 * sizeof(double));
    if (!rows) return;
    for (int level = 0; level < ring_n; level++) {
        double t = (double)(level + 1) / (double)(ring_n + 1);
        double phi = 3.14159265358979323846 * t;
        double ring_y = cy + cos(phi) * ry;
        double r_scale = sin(phi);
        double off = (level % 2) ? (3.14159265358979323846 / (double)n) : 0.0;
        double *row = rows + (size_t)level * (size_t)n * 3;
        for (int i = 0; i < n; i++) {
            double a = rot + ((double)i / (double)n) * TAU + off;
            double k = jit > 0.0 ? 1.0 + (qlcg_lp_rand_or_half(rand) * 2.0 - 1.0) * jit : 1.0;
            row[i * 3 + 0] = cx + cos(a) * rx * r_scale * k;
            row[i * 3 + 1] = ring_y;
            row[i * 3 + 2] = cz + sin(a) * rz * r_scale * k;
        }
    }
    double south[3] = { cx, cy - ry, cz };
    double north[3] = { cx, cy + ry, cz };
    double *r0 = rows;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        qlcg_lp_triangle(b, &r0[i * 3], north, &r0[j * 3], color, mat);
    }
    for (int l = 0; l + 1 < ring_n; l++) {
        double *lo = rows + (size_t)l * (size_t)n * 3;
        double *hi = rows + (size_t)(l + 1) * (size_t)n * 3;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            qlcg_lp_quad(b, &lo[i * 3], &lo[j * 3], &hi[j * 3], &hi[i * 3], color, mat);
        }
    }
    double *top = rows + (size_t)(ring_n - 1) * (size_t)n * 3;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        qlcg_lp_triangle(b, &top[i * 3], &top[j * 3], south, color, mat);
    }
    free(rows);
}

void qlcg_lp_box(LpBuilder *b, const double center[3], const double size[3], LpRgb color, const LpOpts *o) {
    double cx = center[0], cy = center[1], cz = center[2];
    double sx = size[0], sy = size[1], sz = size[2];
    double rot = LP_O(o, rot_y, 0.0);
    double c = cos(rot), s = sin(rot);
    int mat = LP_MAT(o);

    double v[8][3];
    for (int ix = 0; ix < 2; ix++) {
        for (int iy = 0; iy < 2; iy++) {
            for (int iz = 0; iz < 2; iz++) {
                double x = (ix ? 1.0 : -1.0) * sx;
                double z = (iz ? 1.0 : -1.0) * sz;
                int k = ix * 4 + iy * 2 + iz;
                v[k][0] = cx + x * c - z * s;
                v[k][1] = cy + (iy ? 1.0 : -1.0) * sy;
                v[k][2] = cz + x * s + z * c;
            }
        }
    }
    /* 角点索引 ix*4+iy*2+iz，同 base */
    static const int faces[6][4] = {
        { 4, 5, 7, 6 }, /* +X */
        { 0, 2, 3, 1 }, /* -X */
        { 1, 3, 7, 5 }, /* +Z */
        { 0, 4, 6, 2 }, /* -Z */
        { 2, 6, 7, 3 }, /* +Y */
        { 0, 1, 5, 4 }, /* -Y */
    };
    /* base 倒序：quad(f0,f3,f2,f1) */
    for (int f = 0; f < 6; f++)
        qlcg_lp_quad(b, v[faces[f][0]], v[faces[f][3]], v[faces[f][2]], v[faces[f][1]], color, mat);
}

void qlcg_lp_cross_quad(LpBuilder *b, const double base[3], double width, double height,
                        LpRgb color, const LpOpts *o) {
    double cx = base[0], cy = base[1], cz = base[2];
    double bend = LP_O(o, bend, 0.0);
    double rot0 = LP_O(o, rot_y, 0.0);
    int mat = LP_MAT(o);
    for (int p = 0; p < 2; p++) {
        double a = rot0 + ((double)p * 3.14159265358979323846) / 2.0;
        double dx = cos(a), dz = sin(a);
        double px = -dz, pz = dx;
        double tip_x = cx + dx * bend, tip_z = cz + dz * bend;
        double bl[3] = { cx - (px * width) / 2.0, cy, cz - (pz * width) / 2.0 };
        double br[3] = { cx + (px * width) / 2.0, cy, cz + (pz * width) / 2.0 };
        double tr[3] = { tip_x + (px * width * 0.32) / 2.0, cy + height, tip_z + (pz * width * 0.32) / 2.0 };
        double tl[3] = { tip_x - (px * width * 0.32) / 2.0, cy + height, tip_z - (pz * width * 0.32) / 2.0 };
        qlcg_lp_quad(b, bl, br, tr, tl, color, mat);
    }
}

void qlcg_lp_petal(LpBuilder *b, const double center[3], double bearing, double length, double width,
                   LpRgb color, const LpOpts *o) {
    double cx = center[0], cy = center[1], cz = center[2];
    double jit = LP_O(o, jitter, 0.0);
    LpRng *rand = o ? o->rand : NULL;
    double pitch = LP_O(o, pitch, 0.3);
    double cup = LP_O(o, cup, 0.3);
    int mat = LP_MAT(o);
    double len = length * (jit > 0.0 ? 1.0 + (qlcg_lp_rand_or_half(rand) * 2.0 - 1.0) * jit : 1.0);
    double wid = width * (jit > 0.0 ? 1.0 + (qlcg_lp_rand_or_half(rand) * 2.0 - 1.0) * jit : 1.0);
    double dx = cos(bearing), dz = sin(bearing);
    double sx = -dz, sz = dx;
    double lift = sin(pitch) * len;
    double mid_y = cy + lift * 0.52 + wid * cup * 0.5;
    double mid_x = cx + dx * len * 0.52;
    double mid_z = cz + dz * len * 0.52;
    double base_p[3] = { cx, cy, cz };
    double tip[3] = { cx + dx * len, cy + lift, cz + dz * len };
    double left[3] = { mid_x - sx * wid * 0.5, mid_y, mid_z - sz * wid * 0.5 };
    double right[3] = { mid_x + sx * wid * 0.5, mid_y, mid_z + sz * wid * 0.5 };
    qlcg_lp_quad(b, base_p, left, tip, right, color, mat);
    if (o && !isnan(o->both) && o->both != 0.0) qlcg_lp_quad(b, base_p, right, tip, left, color, mat);
}

void qlcg_lp_limb(LpBuilder *b, const double start[3], const double dir[3], double length,
                  double radius, LpRgb color, const LpOpts *o) {
    double sides_d = LP_O(o, sides, 3.0);
    int sides = (int)(sides_d < 3.0 ? 3 : js_round(sides_d));
    if (sides < 3) sides = 3;
    double tip_ratio = LP_O(o, tip_ratio, 0.35);
    int mat = LP_MAT(o);
    double end[3] = {
        start[0] + dir[0] * length,
        start[1] + dir[1] * length,
        start[2] + dir[2] * length,
    };
    double up[3] = { 0.0, 1.0, 0.0 };
    if (fabs(dir[1]) > 0.94) { up[0] = 1.0; up[1] = 0.0; up[2] = 0.0; }
    double ux = up[1] * dir[2] - up[2] * dir[1];
    double uy = up[2] * dir[0] - up[0] * dir[2];
    double uz = up[0] * dir[1] - up[1] * dir[0];
    double ul = js_hypot3(ux, uy, uz);
    if (ul == 0.0) ul = 1.0;
    ux /= ul; uy /= ul; uz /= ul;
    double vx = dir[1] * uz - dir[2] * uy;
    double vy = dir[2] * ux - dir[0] * uz;
    double vz = dir[0] * uy - dir[1] * ux;

    double *r0 = (double *)malloc((size_t)sides * 3 * sizeof(double));
    double *r1 = (double *)malloc((size_t)sides * 3 * sizeof(double));
    if (!r0 || !r1) { free(r0); free(r1); return; }
    for (int pass = 0; pass < 2; pass++) {
        const double *p = pass ? end : start;
        double r = pass ? radius * tip_ratio : radius;
        double *out = pass ? r1 : r0;
        for (int i = 0; i < sides; i++) {
            double a = ((double)i / (double)sides) * TAU;
            double ca = cos(a) * r, sa = sin(a) * r;
            out[i * 3 + 0] = p[0] + ux * ca + vx * sa;
            out[i * 3 + 1] = p[1] + uy * ca + vy * sa;
            out[i * 3 + 2] = p[2] + uz * ca + vz * sa;
        }
    }
    for (int i = 0; i < sides; i++) {
        int j = (i + 1) % sides;
        qlcg_lp_quad(b, &r0[i * 3], &r0[j * 3], &r1[j * 3], &r1[i * 3], color, mat);
    }
    if (!(o && !isnan(o->cap) && o->cap == 0.0)) {
        for (int i = 1; i < sides - 1; i++)
            qlcg_lp_triangle(b, &r1[0], &r1[i * 3], &r1[(i + 1) * 3], color, mat);
    }
    free(r0);
    free(r1);
}

void qlcg_lp_build(LpBuilder *b, LpGeometry *out) {
    memset(out, 0, sizeof(*out));
    int vertex_count = 0;
    int index_count = 0;
    for (int i = 0; i < b->count; i++) {
        vertex_count += b->faces[i].n;
        index_count += (b->faces[i].n - 2) * 3;
    }
    if (vertex_count <= 0 || index_count <= 0) return;

    out->positions = (float *)malloc((size_t)vertex_count * 3 * sizeof(float));
    out->normals = (float *)malloc((size_t)vertex_count * 3 * sizeof(float));
    out->colors = (unsigned char *)malloc((size_t)vertex_count * 3);
    out->wind_parts = (unsigned char *)malloc((size_t)vertex_count);
    out->indices = (uint32_t *)malloc((size_t)index_count * sizeof(uint32_t));
    if (!out->positions || !out->normals || !out->colors || !out->wind_parts || !out->indices) {
        qlcg_lp_geometry_free(out);
        return;
    }
    out->vertex_count = vertex_count;
    out->socket_count = b->socket_count;
    memcpy(out->sockets, b->sockets, (size_t)b->socket_count * sizeof(LpSocket));
    out->index_count = index_count;
    out->triangle_count = index_count / 3;

    int v = 0, idx = 0;
    double max_r = 0.0, max_y = 0.0;
    /* 按材质槽顺序输出 */
    for (int mat = 0; mat < LP_MAT_COUNT; mat++) {
        int start = idx;
        for (int fi = 0; fi < b->count; fi++) {
            LpFace *f = &b->faces[fi];
            if (f->mat != mat) continue;
            int n = f->n;
            for (int i = 0; i < n * 3; i++) out->positions[v * 3 + i] = (float)f->pos[i];
            for (int i = 0; i < n; i++) out->wind_parts[v + i] = (unsigned char)f->wind;
            double nrm[3];
            qlcg_lp_face_normal(&f->pos[0], &f->pos[3], &f->pos[6], nrm);
            for (int i = 0; i < n; i++) {
                out->normals[(v + i) * 3 + 0] = (float)nrm[0];
                out->normals[(v + i) * 3 + 1] = (float)nrm[1];
                out->normals[(v + i) * 3 + 2] = (float)nrm[2];
            }
            for (int i = 0; i < n; i++) {
                out->colors[(v + i) * 3 + 0] = f->col[0];
                out->colors[(v + i) * 3 + 1] = f->col[1];
                out->colors[(v + i) * 3 + 2] = f->col[2];
            }
            for (int i = 2; i < n; i++) {
                out->indices[idx++] = (uint32_t)v;
                out->indices[idx++] = (uint32_t)(v + i - 1);
                out->indices[idx++] = (uint32_t)(v + i);
            }
            for (int i = 0; i < n; i++) {
                double x = f->pos[i * 3], y = f->pos[i * 3 + 1], z = f->pos[i * 3 + 2];
                double r = js_hypot2(x, z);
                if (r > max_r) max_r = r;
                if (y > max_y) max_y = y;
            }
            v += n;
        }
        if (idx > start) {
            out->groups[out->group_count].mat = mat;
            out->groups[out->group_count].start = start;
            out->groups[out->group_count].count = idx - start;
            out->group_count++;
        }
    }
    out->radius = max_r;
    out->height = max_y;
}

void qlcg_lp_geometry_free(LpGeometry *g) {
    free(g->positions);
    free(g->normals);
    free(g->colors);
    free(g->wind_parts);
    free(g->indices);
    memset(g, 0, sizeof(*g));
}

void qlcg_lp_geometry_scale(LpGeometry *g, double scale) {
    if (scale == 1.0) return;
    int n = g->vertex_count * 3;
    for (int i = 0; i < n; i++) g->positions[i] = (float)(g->positions[i] * scale);
    for (int i = 0; i < g->socket_count; i++)
        for (int axis = 0; axis < 3; axis++) g->sockets[i].translation[axis] *= scale;
    g->radius *= scale;
    g->height *= scale;
}
