/* 低模几何构建器，同 base geometry.ts */
#ifndef QLCG_LP_BUILDER_H
#define QLCG_LP_BUILDER_H

#include "lp_math.h"

/* 单个多边形面 */
typedef struct {
    double        *pos;   /* 顶点数 * 3 */
    int            n;     /* 顶点数 */
    unsigned char  col[3];
    double         nrm[3];
    int            mat;
    int            wind;
} LpFace;

enum { LP_SOCKET_SNOW_CANOPY, LP_SOCKET_SNOW_GROUND, LP_SOCKET_FRUIT,
       LP_SOCKET_SNOW_SURFACE, LP_SOCKET_CAPACITY = 32 };

typedef struct {
    double translation[3];
    int kind;
} LpSocket;

typedef struct {
    LpFace *faces;
    int     count;
    int     cap;
    LpSocket sockets[LP_SOCKET_CAPACITY];
    int socket_count;
} LpBuilder;

typedef struct {
    int mat;
    int start;
    int count;
} LpGroup;

typedef struct {
    float         *positions;  /* v*3 */
    float         *normals;    /* v*3 */
    unsigned char *colors;     /* v*3 */
    unsigned char *wind_parts; /* v   */
    uint32_t      *indices;    /* t*3 */
    int    vertex_count;
    int    triangle_count;
    int    index_count;
    double radius;
    double height;
    LpGroup groups[4];
    int     group_count;
    LpSocket sockets[LP_SOCKET_CAPACITY];
    int socket_count;
} LpGeometry;

/* 形状选项：NAN 表示未设置，mat 用 -1 表示未设 */
typedef struct {
    double rot_y, jitter, cap, floor_on, rings, squash;
    double radius_ratio, bend, both, pitch, cup, sides, tip_ratio;
    LpRng *rand;
    int    mat;
} LpOpts;

LpOpts qlcg_lp_opts(void);

void qlcg_lp_init(LpBuilder *b);
void qlcg_lp_clear(LpBuilder *b);
void qlcg_lp_free(LpBuilder *b);
void qlcg_lp_socket(LpBuilder *b, const double translation[3], int kind);

/* 提交面（外侧逆时针） */
void qlcg_lp_push(LpBuilder *b, const double *pos, int n, LpRgb col, const double nrm[3], int mat);

void qlcg_lp_triangle(LpBuilder *b, const double a[3], const double bb[3], const double c[3], LpRgb col, int mat);
void qlcg_lp_quad(LpBuilder *b, const double a[3], const double bb[3], const double c[3], const double d[3], LpRgb col, int mat);

void qlcg_lp_prism(LpBuilder *b, const double center[3], double rx, double rz,
                   double bottom, double top, double sides, LpRgb color, const LpOpts *o);

void qlcg_lp_cone(LpBuilder *b, const double base[3], double rx, double rz,
                  double height, double sides, LpRgb color, const LpOpts *o);

void qlcg_lp_dome(LpBuilder *b, const double center[3], double rx, double ry, double rz,
                  double sides, LpRgb color, const LpOpts *o);

void qlcg_lp_ico(LpBuilder *b, const double center[3], const double radius[3],
                 double sides, LpRgb color, const LpOpts *o);

void qlcg_lp_box(LpBuilder *b, const double center[3], const double size[3], LpRgb color, const LpOpts *o);

void qlcg_lp_cross_quad(LpBuilder *b, const double base[3], double width, double height,
                        LpRgb color, const LpOpts *o);

void qlcg_lp_petal(LpBuilder *b, const double center[3], double bearing, double length, double width,
                   LpRgb color, const LpOpts *o);

void qlcg_lp_limb(LpBuilder *b, const double start[3], const double dir[3], double length,
                  double radius, LpRgb color, const LpOpts *o);

/* 打包并按材质槽分段 */
void qlcg_lp_build(LpBuilder *b, LpGeometry *out);
void qlcg_lp_geometry_free(LpGeometry *g);
void qlcg_lp_geometry_scale(LpGeometry *g, double scale);

#endif
