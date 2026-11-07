/* 岩石几何构建器实现 */
#pragma float_control(precise, on)
#pragma fp_contract(off)

#include "rock_builder.h"
#include "rock_def.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TAU 6.283185307179586476925286766559
#define PI  3.14159265358979323846
#define COLOR_JITTER 0.04

static double js_round(double v) { return floor(v + 0.5); }

void qlcg_rock_builder_init(RockBuilder *b, LpRng *rand) {
    b->faces = NULL;
    b->count = 0;
    b->cap = 0;
    b->rand = rand;
    b->current_mat = ROCK_MAT_SOLID;
    b->min_y = 0.0;
    b->max_y = 0.0;
    b->max_r = 0.0;
}

static void track(RockBuilder *b, double x, double y, double z) {
    double r = js_hypot2(x, z);
    if (r > b->max_r) b->max_r = r;
    if (y > b->max_y) b->max_y = y;
    if (y < b->min_y) b->min_y = y;
}

static double rnd(RockBuilder *b) {
    return b->rand ? qlcg_lp_rng_next(b->rand) : 0.5;
}

void qlcg_rock_push(RockBuilder *b, const double *pos, int n, LpRgb col,
                    const double nrm[3], int mat, int raw) {
    if (b->count >= b->cap) {
        int next = b->cap ? b->cap * 2 : 64;
        RockFace *nf = (RockFace *)realloc(b->faces, (size_t)next * sizeof(RockFace));
        if (!nf) return;
        b->faces = nf;
        b->cap = next;
    }
    RockFace *f = &b->faces[b->count];
    f->pos = (double *)malloc((size_t)n * 3 * sizeof(double));
    if (!f->pos) return;
    memcpy(f->pos, pos, (size_t)n * 3 * sizeof(double));
    f->n = n;
    double s = raw ? 1.0 : qlcg_lp_shade_of(nrm[0], nrm[1], nrm[2]);
    /* 与 base 一致：每面消耗一次随机数做颜色微扰 */
    double k = 1.0 + (rnd(b) * 2.0 - 1.0) * COLOR_JITTER;
    f->col[0] = (unsigned char)qlcg_lp_clamp_byte(col.r * s * k);
    f->col[1] = (unsigned char)qlcg_lp_clamp_byte(col.g * s * k);
    f->col[2] = (unsigned char)qlcg_lp_clamp_byte(col.b * s * k);
    f->nrm[0] = nrm[0]; f->nrm[1] = nrm[1]; f->nrm[2] = nrm[2];
    f->mat = mat;
    for (int i = 0; i < n; i++) track(b, f->pos[i * 3], f->pos[i * 3 + 1], f->pos[i * 3 + 2]);
    b->count++;
}

void qlcg_rock_tri(RockBuilder *b, const double a[3], const double bb[3], const double c[3],
                   LpRgb color, int mat) {
    double pos[9] = { a[0], a[1], a[2], bb[0], bb[1], bb[2], c[0], c[1], c[2] };
    double nrm[3];
    qlcg_lp_face_normal(a, bb, c, nrm);
    qlcg_rock_push(b, pos, 3, color, nrm, mat, 0);
}

void qlcg_rock_quad(RockBuilder *b, const double a[3], const double bb[3],
                    const double c[3], const double d[3], LpRgb color, int mat) {
    qlcg_rock_tri(b, a, bb, c, color, mat);
    qlcg_rock_tri(b, a, c, d, color, mat);
}

/* 逐面覆被判定 */
static void crust_face(int index, LpRgb col, const double nrm[3], int mat,
                       const RockCrust *crust, LpRgb *out_col, int *out_mat, double scale) {
    *out_col = col;
    *out_mat = mat;
    if (!crust || !crust->active) return;
    if (crust->amount <= 0.0) return;
    if (nrm[1] < crust->slope) return;
    unsigned salt = crust->seed ? crust->seed : 0x51ed2701u;
    if (qlcg_lp_hash2(index, 0, (int32_t)salt) >= crust->amount * scale) return;
    *out_col = crust->color;
    *out_mat = crust->mat;
}

static LpRgb lighten(LpRgb c, double amount) { return qlcg_lp_lighten(c, amount); }
static LpRgb deepen(LpRgb c, double depth) { return qlcg_lp_deepen(c, depth); }
static LpRgb mixc(LpRgb a, LpRgb b, double t) { return qlcg_lp_mix(a, b, t); }

void qlcg_rock_shell(RockBuilder *b, const RockShellOpts *o) {
    double n_d = o->sides < 3.0 ? 3.0 : js_round(o->sides);
    int n = (int)n_d;
    if (n < 3) n = 3;
    double jit = o->jitter;
    int mat = (o->mat >= 0) ? o->mat : b->current_mat;
    LpRgb color = o->color;
    LpRgb cap_color = isnan(o->cap_color[0]) ? lighten(color, 0.08) : qlcg_lp_rgb(o->cap_color[0], o->cap_color[1], o->cap_color[2]);
    LpRgb side_color = isnan(o->side_color[0]) ? color : qlcg_lp_rgb(o->side_color[0], o->side_color[1], o->side_color[2]);
    LpRgb bottom_color = isnan(o->bottom_color[0]) ? deepen(color, 0.4) : qlcg_lp_rgb(o->bottom_color[0], o->bottom_color[1], o->bottom_color[2]);
    double rot = o->rot_y;
    double wob_amp = o->wobble_amount * o->thickness * 0.5;

    double nl = js_hypot3(o->normal[0], o->normal[1], o->normal[2]);
    if (nl == 0.0) nl = 1.0;
    double up[3] = { o->normal[0] / nl, o->normal[1] / nl, o->normal[2] / nl };
    double ref[3] = { 0.0, 1.0, 0.0 };
    if (fabs(up[1]) > 0.94) { ref[0] = 1.0; ref[1] = 0.0; ref[2] = 0.0; }
    double ux = ref[1] * up[2] - ref[2] * up[1];
    double uy = ref[2] * up[0] - ref[0] * up[2];
    double uz = ref[0] * up[1] - ref[1] * up[0];
    double ul = js_hypot3(ux, uy, uz);
    if (ul == 0.0) ul = 1.0;
    ux /= ul; uy /= ul; uz /= ul;
    double vx = up[1] * uz - up[2] * uy;
    double vy = up[2] * ux - up[0] * uz;
    double vz = up[0] * uy - up[1] * ux;

    double half = o->thickness / 2.0;
    double *top = (double *)malloc((size_t)n * 3 * sizeof(double));
    double *bot = (double *)malloc((size_t)n * 3 * sizeof(double));
    if (!top || !bot) { free(top); free(bot); return; }

    for (int i = 0; i < n; i++) {
        double a = rot + ((double)i / (double)n) * TAU;
        double ripple = o->radius_scale ? o->radius_scale[i] : 1.0;
        double k = (jit > 0.0 ? 1.0 + (rnd(b) * 2.0 - 1.0) * jit : 1.0) * ripple;
        double wv = o->wobble ? o->wobble[i] : 0.0;
        double lift = wv * wob_amp;
        double cx = cos(a) * o->rx * k;
        double cz = sin(a) * o->rz * k;
        double px = o->center[0] + ux * cx + vx * cz;
        double py = o->center[1] + uy * cx + vy * cz;
        double pz = o->center[2] + uz * cx + vz * cz;
        top[i * 3 + 0] = px + up[0] * (half + lift);
        top[i * 3 + 1] = py + up[1] * (half + lift);
        top[i * 3 + 2] = pz + up[2] * (half + lift);
        bot[i * 3 + 0] = px - up[0] * (half - lift);
        bot[i * 3 + 1] = py - up[1] * (half - lift);
        bot[i * 3 + 2] = pz - up[2] * (half - lift);
    }
    /* 侧带 */
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        double nrm[3];
        qlcg_lp_face_normal(&bot[i * 3], &bot[j * 3], &top[j * 3], nrm);
        LpRgb col; int m;
        crust_face(0x800 + i, side_color, nrm, mat, o->has_crust ? &o->crust : NULL, &col, &m, 0.7);
        qlcg_rock_quad(b, &bot[i * 3], &bot[j * 3], &top[j * 3], &top[i * 3], col, m);
    }
    /* 顶盖 */
    if (!(o->cap == 0.0)) {
        for (int i = 1; i < n - 1; i++) {
            LpRgb col; int m;
            crust_face(0x100 + i, cap_color, up, mat, o->has_crust ? &o->crust : NULL, &col, &m, 1.0);
            double tri[9] = { top[0], top[1], top[2],
                              top[i * 3 + 0], top[i * 3 + 1], top[i * 3 + 2],
                              top[(i + 1) * 3 + 0], top[(i + 1) * 3 + 1], top[(i + 1) * 3 + 2] };
            qlcg_rock_push(b, tri, 3, col, up, m, 0);
        }
    }
    /* 底盖 */
    if (!(o->bottom_cap == 0.0)) {
        double down[3] = { -up[0], -up[1], -up[2] };
        for (int i = 1; i < n - 1; i++) {
            double tri[9] = { bot[0], bot[1], bot[2],
                              bot[(i + 1) * 3 + 0], bot[(i + 1) * 3 + 1], bot[(i + 1) * 3 + 2],
                              bot[i * 3 + 0], bot[i * 3 + 1], bot[i * 3 + 2] };
            qlcg_rock_push(b, tri, 3, bottom_color, down, mat, 0);
        }
    }
    free(top);
    free(bot);
}

void qlcg_rock_column(RockBuilder *b, const RockColumnOpts *o) {
    int segs = (int)(o->segments < 1.0 ? 1.0 : js_round(o->segments));
    int n = (int)(o->sides < 3.0 ? 3.0 : js_round(o->sides));
    if (n < 3) n = 3;
    int mat = (o->mat >= 0) ? o->mat : b->current_mat;
    double skew = isnan(o->skew) ? 0.12 : o->skew;
    double twist = isnan(o->twist) ? 0.35 : o->twist;
    double jit = o->jitter;
    double tip_ratio = qlcg_lp_clamp01(o->tip_ratio);
    LpRgb color = o->color;
    LpRgb tip_color = isnan(o->tip_color[0]) ? lighten(color, 0.06) : qlcg_lp_rgb(o->tip_color[0], o->tip_color[1], o->tip_color[2]);

    double cx = o->base[0], cz = o->base[2];
    double radius = o->radius;
    double y = o->base[1];
    double *ring_bot = (double *)malloc((size_t)n * 3 * sizeof(double));
    double *ring_top = (double *)malloc((size_t)n * 3 * sizeof(double));
    if (!ring_bot || !ring_top) { free(ring_bot); free(ring_top); return; }

    for (int s = 0; s < segs; s++) {
        double t0 = (double)s / (double)segs;
        double t1 = (double)(s + 1) / (double)segs;
        double r0 = radius;
        double r1 = o->radius * (1.0 - (1.0 - tip_ratio) * t1);
        double h = (o->height / (double)segs) * (jit > 0.0 ? 1.0 + (rnd(b) * 2.0 - 1.0) * jit : 1.0);
        double next_x = cx + (rnd(b) * 2.0 - 1.0) * skew * r1;
        double next_z = cz + (rnd(b) * 2.0 - 1.0) * skew * r1;
        double phase_bot = -twist * 0.5;
        double phase_top = twist * 0.5;
        for (int i = 0; i < n; i++) {
            double a_bot = phase_bot + ((double)i / (double)n) * TAU;
            double a_top = phase_top + ((double)i / (double)n) * TAU;
            double k_bot = jit > 0.0 ? 1.0 + (rnd(b) * 2.0 - 1.0) * jit : 1.0;
            double k_top = jit > 0.0 ? 1.0 + (rnd(b) * 2.0 - 1.0) * jit : 1.0;
            ring_bot[i * 3 + 0] = cx + cos(a_bot) * r0 * k_bot;
            ring_bot[i * 3 + 1] = y;
            ring_bot[i * 3 + 2] = cz + sin(a_bot) * r0 * k_bot;
            ring_top[i * 3 + 0] = next_x + cos(a_top) * r1 * k_top;
            ring_top[i * 3 + 1] = y + h;
            ring_top[i * 3 + 2] = next_z + sin(a_top) * r1 * k_top;
        }
        LpRgb seg_color = mixc(color, tip_color, t0 * 0.5);
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            double nrm[3];
            qlcg_lp_face_normal(&ring_bot[i * 3], &ring_bot[j * 3], &ring_top[j * 3], nrm);
            LpRgb col; int m;
            crust_face(s * 0x40 + i, seg_color, nrm, mat, o->has_crust ? &o->crust : NULL, &col, &m, 1.0);
            qlcg_rock_quad(b, &ring_bot[i * 3], &ring_bot[j * 3], &ring_top[j * 3], &ring_top[i * 3], col, m);
        }
        LpRgb face_col = (s == segs - 1) ? tip_color : mixc(color, tip_color, t1 * 0.6);
        double upv[3] = { 0.0, 1.0, 0.0 };
        for (int i = 1; i < n - 1; i++) {
            LpRgb col; int m;
            crust_face(0x200 + s * 0x40 + i, face_col, upv, mat, o->has_crust ? &o->crust : NULL, &col, &m, 1.0);
            double tri[9] = { ring_top[0], ring_top[1], ring_top[2],
                              ring_top[i * 3 + 0], ring_top[i * 3 + 1], ring_top[i * 3 + 2],
                              ring_top[(i + 1) * 3 + 0], ring_top[(i + 1) * 3 + 1], ring_top[(i + 1) * 3 + 2] };
            qlcg_rock_push(b, tri, 3, col, upv, m, 0);
        }
        cx = next_x;
        cz = next_z;
        y += h;
        radius = r1;
    }
    free(ring_bot);
    free(ring_top);
}

void qlcg_rock_slab(RockBuilder *b, const RockSlabOpts *o) {
    double bearing = o->bearing;
    double normal[3] = {
        -sin(o->tilt) * cos(bearing),
        cos(o->tilt),
        -sin(o->tilt) * sin(bearing),
    };
    RockShellOpts s;
    memset(&s, 0, sizeof(s));
    s.center[0] = o->center[0]; s.center[1] = o->center[1]; s.center[2] = o->center[2];
    s.rx = o->rx; s.rz = o->rz;
    s.thickness = o->thickness;
    s.sides = o->sides;
    s.normal[0] = normal[0]; s.normal[1] = normal[1]; s.normal[2] = normal[2];
    s.color = o->color;
    LpRgb cc = lighten(o->color, 0.14);
    s.cap_color[0] = cc.r; s.cap_color[1] = cc.g; s.cap_color[2] = cc.b;
    LpRgb bc = deepen(o->color, 0.45);
    s.bottom_color[0] = bc.r; s.bottom_color[1] = bc.g; s.bottom_color[2] = bc.b;
    LpRgb sc = deepen(o->color, 0.18);
    s.side_color[0] = sc.r; s.side_color[1] = sc.g; s.side_color[2] = sc.b;
    s.jitter = o->jitter;
    s.wobble = o->wobble;
    s.wobble_amount = o->wobble_amount;
    s.rot_y = o->rot_y;
    s.mat = o->mat;
    s.cap = NAN;
    s.bottom_cap = NAN;
    s.crust = o->crust;
    s.has_crust = o->has_crust;
    qlcg_rock_shell(b, &s);
}

void qlcg_rock_blob(RockBuilder *b, const RockBlobOpts *o) {
    double rx = o->radius[0], ry = o->radius[1], rz = o->radius[2];
    int n = (int)(o->sides < 3.0 ? 3.0 : js_round(o->sides));
    if (n < 3) n = 3;
    int rings = (int)(o->rings < 1.0 ? 1.0 : js_round(o->rings));
    if (rings < 1) rings = 1;
    double rot = o->rot_y;
    double jit = o->jitter;
    int mat = (o->mat >= 0) ? o->mat : b->current_mat;
    LpRgb color = o->color;
    LpRgb cap_color = isnan(o->cap_color[0]) ? lighten(color, 0.1) : qlcg_lp_rgb(o->cap_color[0], o->cap_color[1], o->cap_color[2]);
    double cx = o->center[0], cy = o->center[1], cz = o->center[2];

    double *rows = (double *)malloc((size_t)rings * (size_t)n * 3 * sizeof(double));
    if (!rows) return;
    for (int level = 0; level < rings; level++) {
        double t = (double)(level + 1) / (double)(rings + 1);
        double phi = PI * t;
        double ring_y = cy + cos(phi) * ry;
        double r_scale = sin(phi);
        double off = (level % 2) ? (PI / (double)n) : 0.0;
        double *row = rows + (size_t)level * (size_t)n * 3;
        for (int i = 0; i < n; i++) {
            double a = rot + ((double)i / (double)n) * TAU + off;
            double k = jit > 0.0 ? 1.0 + (rnd(b) * 2.0 - 1.0) * jit : 1.0;
            row[i * 3 + 0] = cx + cos(a) * rx * r_scale * k;
            row[i * 3 + 1] = ring_y;
            row[i * 3 + 2] = cz + sin(a) * rz * r_scale * k;
        }
    }
    double south[3] = { cx, cy - ry, cz };
    double north[3] = { cx, cy + ry, cz };

    /* 南侧扇 */
    {
        double *r0 = rows;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            double nrm[3];
            qlcg_lp_face_normal(&r0[i * 3], south, &r0[j * 3], nrm);
            LpRgb col; int m;
            crust_face(i, color, nrm, mat, o->has_crust ? &o->crust : NULL, &col, &m, 1.0);
            qlcg_rock_tri(b, &r0[i * 3], south, &r0[j * 3], col, m);
        }
    }
    /* 中间带 */
    for (int l = 0; l + 1 < rings; l++) {
        LpRgb col = mixc(color, cap_color, (double)(l + 1) / (double)rings);
        double *lo = rows + (size_t)l * (size_t)n * 3;
        double *hi = rows + (size_t)(l + 1) * (size_t)n * 3;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            int idx = 0x300 + l * 0x40 + i;
            double nrm[3];
            qlcg_lp_face_normal(&lo[i * 3], &lo[j * 3], &hi[j * 3], nrm);
            LpRgb c1; int m1;
            crust_face(idx * 2, col, nrm, mat, o->has_crust ? &o->crust : NULL, &c1, &m1, 1.0);
            qlcg_rock_tri(b, &lo[i * 3], &lo[j * 3], &hi[j * 3], c1, m1);
            double nrm2[3];
            qlcg_lp_face_normal(&lo[i * 3], &hi[j * 3], &hi[i * 3], nrm2);
            LpRgb c2; int m2;
            crust_face(idx * 2 + 1, col, nrm2, mat, o->has_crust ? &o->crust : NULL, &c2, &m2, 1.0);
            qlcg_rock_tri(b, &lo[i * 3], &hi[j * 3], &hi[i * 3], c2, m2);
        }
    }
    /* 北侧扇 */
    {
        double *top = rows + (size_t)(rings - 1) * (size_t)n * 3;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            double nrm[3];
            qlcg_lp_face_normal(&top[i * 3], &top[j * 3], north, nrm);
            LpRgb col; int m;
            crust_face(0x400 + i, cap_color, nrm, mat, o->has_crust ? &o->crust : NULL, &col, &m, 1.0);
            qlcg_rock_tri(b, &top[i * 3], &top[j * 3], north, col, m);
        }
    }
    free(rows);
}

void qlcg_rock_build(RockBuilder *b, RockGeometry *out) {
    memset(out, 0, sizeof(*out));
    int vertex_count = 0, index_count = 0;
    for (int i = 0; i < b->count; i++) {
        vertex_count += b->faces[i].n;
        index_count += (b->faces[i].n - 2) * 3;
    }
    if (vertex_count <= 0 || index_count <= 0) return;

    out->positions = (float *)malloc((size_t)vertex_count * 3 * sizeof(float));
    out->normals = (float *)malloc((size_t)vertex_count * 3 * sizeof(float));
    out->colors = (unsigned char *)malloc((size_t)vertex_count * 3);
    out->indices = (uint32_t *)malloc((size_t)index_count * sizeof(uint32_t));
    if (!out->positions || !out->normals || !out->colors || !out->indices) {
        qlcg_rock_geometry_free(out);
        return;
    }
    out->vertex_count = vertex_count;
    out->index_count = index_count;
    out->triangle_count = index_count / 3;

    int v = 0, idx = 0;
    double snow_points[ROCK_SOCKET_CAPACITY][3] = {0};
    for (int mat = 0; mat < 4; mat++) {
        int start = idx;
        for (int fi = 0; fi < b->count; fi++) {
            RockFace *f = &b->faces[fi];
            if (f->mat != mat) continue;
            int n = f->n;
            for (int i = 0; i < n * 3; i++) out->positions[v * 3 + i] = (float)f->pos[i];
            double nrm[3];
            qlcg_lp_face_normal(&f->pos[0], &f->pos[3], &f->pos[6], nrm);
            if (nrm[1] >= 0.42) {
                double center[3];
                for (int axis = 0; axis < 3; axis++)
                    center[axis] = (f->pos[axis] + f->pos[3 + axis] + f->pos[6 + axis]) / 3.0;
                int quadrant = (center[0] < 0.0) + 2 * (center[2] < 0.0);
                if (center[1] > snow_points[quadrant][1])
                    memcpy(snow_points[quadrant], center, sizeof(center));
            }
            for (int i = 0; i < n; i++) {
                out->normals[(v + i) * 3 + 0] = (float)nrm[0];
                out->normals[(v + i) * 3 + 1] = (float)nrm[1];
                out->normals[(v + i) * 3 + 2] = (float)nrm[2];
                out->colors[(v + i) * 3 + 0] = f->col[0];
                out->colors[(v + i) * 3 + 1] = f->col[1];
                out->colors[(v + i) * 3 + 2] = f->col[2];
            }
            for (int i = 2; i < n; i++) {
                out->indices[idx++] = (uint32_t)v;
                out->indices[idx++] = (uint32_t)(v + i - 1);
                out->indices[idx++] = (uint32_t)(v + i);
            }
            v += n;
        }
        if (idx > start) {
            out->group_mat[out->group_n] = mat;
            out->group_start[out->group_n] = start;
            out->group_count[out->group_n] = idx - start;
            out->group_n++;
            out->cover_ratio[mat] += (double)(idx - start) / 3.0;
        }
    }
    double total = (double)(index_count / 3);
    if (total <= 0.0) total = 1.0;
    for (int i = 0; i < 4; i++) out->cover_ratio[i] /= total;
    for (int i = 0; i < ROCK_SOCKET_CAPACITY; i++) {
        if (snow_points[i][1] <= 0.0) continue;
        LpSocket *s = &out->sockets[out->socket_count++];
        memcpy(s->translation, snow_points[i], sizeof(s->translation));
        s->kind = LP_SOCKET_SNOW_SURFACE;
    }

    out->radius = b->max_r > 1e-3 ? b->max_r : 1e-3;
    out->height = b->max_y > 1e-3 ? b->max_y : 1e-3;
    out->bottom = b->min_y < 0.0 ? b->min_y : 0.0;
}

void qlcg_rock_geometry_free(RockGeometry *g) {
    free(g->positions);
    free(g->normals);
    free(g->colors);
    free(g->indices);
    memset(g, 0, sizeof(*g));
}

void qlcg_rock_geometry_scale(RockGeometry *g, double scale) {
    if (scale == 1.0) return;
    int n = g->vertex_count * 3;
    for (int i = 0; i < n; i++) g->positions[i] = (float)(g->positions[i] * scale);
    for (int i = 0; i < g->socket_count; i++)
        for (int axis = 0; axis < 3; axis++) g->sockets[i].translation[axis] *= scale;
    g->radius *= scale;
    g->height *= scale;
    g->bottom *= scale;
}

void qlcg_rock_builder_free(RockBuilder *b) {
    for (int i = 0; i < b->count; i++) free(b->faces[i].pos);
    free(b->faces);
    memset(b, 0, sizeof(*b));
}
