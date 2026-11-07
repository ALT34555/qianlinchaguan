/* 低模几何数学层，同 base core/math */
#ifndef QLCG_LP_MATH_H
#define QLCG_LP_MATH_H

#include <stdint.h>

/* 颜色随带风摆槽位，等价 base 的 WeakMap 标记 */
typedef struct {
    double r, g, b;
    int    wind;
} LpRgb;

enum { LP_WIND_LEAF = 1, LP_WIND_BARK = 2, LP_WIND_SOLID = 3, LP_WIND_CROWN = 4 };

enum { LP_MAT_SOLID = 0, LP_MAT_SNOW = 1, LP_MAT_BLOOM = 2, LP_MAT_FRUIT = 3, LP_MAT_COUNT = 4 };

const char *qlcg_lp_mat_name(int mat);
int         qlcg_lp_mat_index(const char *name);

double qlcg_lp_clamp(double v, double lo, double hi);
double qlcg_lp_clamp01(double v);
int    qlcg_lp_clamp_byte(double v);

LpRgb qlcg_lp_rgb(double r, double g, double b);
/* 解析 hex（# 可省），失败用 fallback */
LpRgb qlcg_lp_hex(const char *hex, double fr, double fg, double fb);
LpRgb qlcg_lp_wind_color(LpRgb c, int part);
int   qlcg_lp_palette_wind(const char *key);

LpRgb qlcg_lp_mix(LpRgb a, LpRgb b, double t);
LpRgb qlcg_lp_deepen(LpRgb c, double depth);
LpRgb qlcg_lp_lighten(LpRgb c, double amount);

double qlcg_lp_shade_of(double nx, double ny, double nz);
void   qlcg_lp_face_normal(const double a[3], const double b[3], const double c[3], double out[3]);

/* 同 ES Math.hypot 的缩放思路 */
double js_hypot2(double a, double b);
double js_hypot3(double a, double b, double c);

/* mulberry32 */
typedef struct { uint32_t a; } LpRng;
void   qlcg_lp_rng_seed(LpRng *r, uint32_t seed);
double qlcg_lp_rng_next(LpRng *r);
/* NULL 时代替 () => 0.5 */
double qlcg_lp_rand_or_half(LpRng *r);

double   qlcg_lp_hash2(int32_t x, int32_t z, int32_t seed);
uint32_t qlcg_lp_hash_string(const char *s);

#endif
