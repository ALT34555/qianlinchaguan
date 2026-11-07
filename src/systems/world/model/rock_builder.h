/* 岩石几何构建器：对齐 geometry.ts */
#ifndef QLCG_ROCK_BUILDER_H
#define QLCG_ROCK_BUILDER_H

#include "lp_builder.h"

enum { ROCK_SOCKET_CAPACITY = 4 };

typedef struct {
    double        *pos;
    int            n;
    unsigned char  col[3];
    double         nrm[3];
    int            mat;
} RockFace;

typedef struct {
    RockFace *faces;
    int       count;
    int       cap;
    LpRng    *rand;
    int       current_mat;
    double    min_y, max_y, max_r;
} RockBuilder;

/* 覆被方案 */
typedef struct {
    int    active;
    int    mat;
    double amount;
    double slope;
    unsigned seed;
    LpRgb  color;
} RockCrust;

typedef struct {
    float         *positions;
    float         *normals;
    unsigned char *colors;
    uint32_t      *indices;
    int    vertex_count;
    int    triangle_count;
    int    index_count;
    double radius;
    double height;
    double bottom;
    int    group_mat[4];
    int    group_start[4];
    int    group_count[4];
    int    group_n;
    double cover_ratio[4];
    LpSocket sockets[ROCK_SOCKET_CAPACITY];
    int socket_count;
} RockGeometry;

/* 可选参数用 NAN 表示未设置 */
typedef struct {
    double center[3];
    double rx, rz;
    double thickness;
    double sides;
    double normal[3];
    LpRgb  color;
    double cap_color[3];      /* NAN = 未设 */
    double bottom_color[3];
    double side_color[3];
    double jitter;
    double rot_y;
    int    mat;
    double cap;               /* NAN = 画顶盖 */
    double bottom_cap;        /* NAN = 画底盖 */
    const double *radius_scale;
    const double *wobble;
    double wobble_amount;
    RockCrust crust;
    int    has_crust;
} RockShellOpts;

typedef struct {
    double base[3];
    double radius;
    double height;
    double segments;
    double tip_ratio;
    double sides;
    LpRgb  color;
    double skew;
    double twist;
    double tip_color[3];
    int    mat;
    double jitter;
    RockCrust crust;
    int    has_crust;
} RockColumnOpts;

typedef struct {
    double center[3];
    double rx, rz;
    double thickness;
    double sides;
    double tilt;
    double bearing;
    LpRgb  color;
    int    mat;
    double jitter;
    double rot_y;
    const double *wobble;
    double wobble_amount;
    RockCrust crust;
    int    has_crust;
} RockSlabOpts;

typedef struct {
    double center[3];
    double radius[3];
    double sides;
    double rings;
    LpRgb  color;
    double cap_color[3];
    double jitter;
    double rot_y;
    int    mat;
    RockCrust crust;
    int    has_crust;
} RockBlobOpts;

void qlcg_rock_builder_init(RockBuilder *b, LpRng *rand);
void qlcg_rock_builder_free(RockBuilder *b);

void qlcg_rock_push(RockBuilder *b, const double *pos, int n, LpRgb col,
                    const double nrm[3], int mat, int raw);
void qlcg_rock_tri(RockBuilder *b, const double a[3], const double bb[3], const double c[3],
                   LpRgb color, int mat);
void qlcg_rock_quad(RockBuilder *b, const double a[3], const double bb[3],
                    const double c[3], const double d[3], LpRgb color, int mat);

void qlcg_rock_shell(RockBuilder *b, const RockShellOpts *o);
void qlcg_rock_column(RockBuilder *b, const RockColumnOpts *o);
void qlcg_rock_slab(RockBuilder *b, const RockSlabOpts *o);
void qlcg_rock_blob(RockBuilder *b, const RockBlobOpts *o);

void qlcg_rock_build(RockBuilder *b, RockGeometry *out);
void qlcg_rock_geometry_free(RockGeometry *g);
void qlcg_rock_geometry_scale(RockGeometry *g, double scale);

#endif
