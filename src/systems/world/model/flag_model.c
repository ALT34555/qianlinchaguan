/* 领地旗帜网格：对齐 base 默认旗 */
#include "flag_model.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* base 默认旗尺寸与木材色 */
#define FLAG_POLE_H 6.5
#define FLAG_POLE_R 0.11
#define FLAG_TOP    5.9
#define FLAG_DROP   4.1
#define FLAG_W      3.6
#define FLAG_TRIM   0.13
#define FLAG_Z      0.22
#define FLAG_BULGE  0.10
#define FLAG_SIDES  6

static const double FLAG_WOOD[3] = {144.0 / 255.0, 133.0 / 255.0, 121.0 / 255.0};
static const double FLAG_WHITE[3] = {1.0, 1.0, 1.0};

static int mesh_vert(FlagMesh *m, const double p[3], const double n[3],
                     const double col[3], float mat, float wind) {
    if (m->vcount >= m->vcap) {
        int cap = m->vcap ? m->vcap * 2 : 512;
        FlagMeshVert *nv = (FlagMeshVert *)realloc(m->verts, (size_t)cap * sizeof(*nv));
        if (!nv) return -1;
        m->verts = nv;
        m->vcap  = cap;
    }
    FlagMeshVert *v = &m->verts[m->vcount];
    v->px = (float)p[0]; v->py = (float)p[1]; v->pz = (float)p[2];
    v->nx = (float)n[0]; v->ny = (float)n[1]; v->nz = (float)n[2];
    v->cr = (float)col[0]; v->cg = (float)col[1]; v->cb = (float)col[2];
    v->mat  = mat;
    v->wind = wind;
    return m->vcount++;
}

static int mesh_index(FlagMesh *m, uint16_t i) {
    if (m->icount >= m->icap) {
        int cap = m->icap ? m->icap * 2 : 1024;
        uint16_t *ni = (uint16_t *)realloc(m->indices, (size_t)cap * sizeof(*ni));
        if (!ni) return 0;
        m->indices = ni;
        m->icap    = cap;
    }
    m->indices[m->icount++] = i;
    return 1;
}

static int face_normal(const double a[3], const double b[3], const double c[3], double n[3]) {
    double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    double v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    n[0] = u[1] * v[2] - u[2] * v[1];
    n[1] = u[2] * v[0] - u[0] * v[2];
    n[2] = u[0] * v[1] - u[1] * v[0];
    double len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len < 1e-9) return 0;
    n[0] /= len; n[1] /= len; n[2] /= len;
    return 1;
}

static void mesh_tri_n(FlagMesh *m, const double a[3], const double b[3], const double c[3],
                       const double n[3], const double col[3], float mat,
                       float wa, float wb, float wc) {
    int ia = mesh_vert(m, a, n, col, mat, wa);
    int ib = mesh_vert(m, b, n, col, mat, wb);
    int ic = mesh_vert(m, c, n, col, mat, wc);
    if (ia < 0 || ib < 0 || ic < 0) return;
    if (!mesh_index(m, (uint16_t)ia) || !mesh_index(m, (uint16_t)ib) ||
        !mesh_index(m, (uint16_t)ic)) return;
}

/* 单面四边形：法线由绕序决定，供封闭杆件使用 */
static void mesh_quad1(FlagMesh *m, const double a[3], const double b[3],
                       const double c[3], const double d[3],
                       const double col[3], float mat, float wind) {
    double n[3];
    if (!face_normal(a, b, c, n)) return;
    mesh_tri_n(m, a, b, c, n, col, mat, wind, wind, wind);
    mesh_tri_n(m, a, c, d, n, col, mat, wind, wind, wind);
}

/* 双面四边形：旗面正反都亮，避免背面全黑 */
static void mesh_quad2(FlagMesh *m, const double a[3], const double b[3],
                       const double c[3], const double d[3],
                       const double col[3], float mat, const float w[4]) {
    double n[3];
    if (!face_normal(a, b, c, n)) return;
    mesh_tri_n(m, a, b, c, n, col, mat, w[0], w[1], w[2]);
    mesh_tri_n(m, a, c, d, n, col, mat, w[0], w[2], w[3]);
    double nb[3] = {-n[0], -n[1], -n[2]};
    mesh_tri_n(m, a, c, b, nb, col, mat, w[0], w[2], w[1]);
    mesh_tri_n(m, a, d, c, nb, col, mat, w[0], w[3], w[2]);
}

static void mesh_rod(FlagMesh *m, const double from[3], const double to[3],
                     double radius, const double col[3], int sides) {
    double axis[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    double len = sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (len < 1e-6 || radius <= 0.0) return;
    double dir[3] = {axis[0] / len, axis[1] / len, axis[2] / len};
    double ref[3];
    if (fabs(dir[1]) > 0.94) { ref[0] = 1.0; ref[1] = 0.0; ref[2] = 0.0; }
    else                     { ref[0] = 0.0; ref[1] = 1.0; ref[2] = 0.0; }
    double u[3] = {ref[1] * dir[2] - ref[2] * dir[1],
                   ref[2] * dir[0] - ref[0] * dir[2],
                   ref[0] * dir[1] - ref[1] * dir[0]};
    double ul = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (ul < 1e-9) return;
    u[0] /= ul; u[1] /= ul; u[2] /= ul;
    double v[3] = {dir[1] * u[2] - dir[2] * u[1],
                   dir[2] * u[0] - dir[0] * u[2],
                   dir[0] * u[1] - dir[1] * u[0]};

    if (sides < 3) sides = 3;
    if (sides > 8) sides = 8;
    double ra[8][3], rb[8][3];
    for (int i = 0; i < sides; i++) {
        double a = (double)i / (double)sides * 6.283185307179586;
        double ca = cos(a), sa = sin(a);
        for (int k = 0; k < 3; k++) {
            double ox = u[k] * ca + v[k] * sa;
            ra[i][k] = from[k] + ox * radius;
            rb[i][k] = to[k]   + ox * radius;
        }
    }
    for (int i = 0; i < sides; i++) {
        int j = (i + 1) % sides;
        mesh_quad1(m, ra[i], ra[j], rb[j], rb[i], col, 0.0f, 0.0f);
    }
    /* 端盖：底端朝 -dir，顶端朝 +dir */
    double nd[3] = {-dir[0], -dir[1], -dir[2]};
    for (int i = 1; i < sides - 1; i++) {
        mesh_tri_n(m, ra[0], ra[i + 1], ra[i], nd, col, 0.0f, 0.0f, 0.0f, 0.0f);
        mesh_tri_n(m, rb[0], rb[i], rb[i + 1], dir, col, 0.0f, 0.0f, 0.0f, 0.0f);
    }
}

static int axis_edges(double head, double tail, int interior, double *out) {
    int n = 0;
    out[n++] = 0.0;
    double a = head, b = 1.0 - tail;
    if (interior < 1) interior = 1;
    if (head > 1e-4) out[n++] = head;
    for (int k = 1; k <= interior; k++) out[n++] = a + (b - a) * ((double)k / (double)interior);
    if (tail > 1e-4) out[n++] = 1.0;
    return n;
}

static void cloth_point(double u, double v, double p[3]) {
    p[0] = v * FLAG_W - FLAG_W * 0.5;
    p[1] = FLAG_TOP - u * FLAG_DROP;
    p[2] = FLAG_Z + FLAG_BULGE * u * u;
}

/* 旗面网格：下缘与左右包边，顶缘贴横梁 */
static void build_cloth(FlagMesh *m) {
    double trim_u = FLAG_TRIM / FLAG_DROP;
    double trim_v = FLAG_TRIM / FLAG_W;
    double ue[16], ve[16];
    int nu = axis_edges(0.0, trim_u, 9, ue);
    int nv = axis_edges(trim_v, trim_v, 7, ve);
    for (int j = 0; j + 1 < nv; j++) {
        for (int i = 0; i + 1 < nu; i++) {
            int trim = (i + 2 == nu) || (j == 0) || (j + 2 == nv);
            double a[3], b[3], c[3], d[3];
            cloth_point(ue[i],     ve[j],     a);
            cloth_point(ue[i + 1], ve[j],     b);
            cloth_point(ue[i + 1], ve[j + 1], c);
            cloth_point(ue[i],     ve[j + 1], d);
            float w[4] = {(float)ue[i], (float)ue[i + 1], (float)ue[i + 1], (float)ue[i]};
            mesh_quad2(m, a, b, c, d, FLAG_WHITE, trim ? 2.0f : 1.0f, w);
        }
    }
}

void qlcg_flag_mesh_build(FlagMesh *m) {
    if (!m) return;
    memset(m, 0, sizeof(*m));

    double base[3] = {0.0, 0.0, 0.0};
    double top[3]  = {0.0, FLAG_POLE_H, 0.0};
    mesh_rod(m, base, top, FLAG_POLE_R, FLAG_WOOD, FLAG_SIDES);

    double bar_l[3] = {-2.02, 6.0, 0.1};
    double bar_r[3] = { 2.02, 6.0, 0.1};
    mesh_rod(m, bar_l, bar_r, 0.105, FLAG_WOOD, FLAG_SIDES);

    double diag_l[3] = {-2.34, 6.37, 0.1};
    double diag_r[3] = { 2.34, 6.37, 0.1};
    mesh_rod(m, bar_l, diag_l, 0.095, FLAG_WOOD, FLAG_SIDES);
    mesh_rod(m, bar_r, diag_r, 0.095, FLAG_WOOD, FLAG_SIDES);

    build_cloth(m);
    m->height = (float)FLAG_POLE_H;
}

void qlcg_flag_mesh_free(FlagMesh *m) {
    if (!m) return;
    free(m->verts);
    free(m->indices);
    memset(m, 0, sizeof(*m));
}
