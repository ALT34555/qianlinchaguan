/* fdlibm 内核，同 V8 base/ieee754 */
#include "js_trig.h"

#include <stdint.h>
#include <string.h>

typedef union { double d; uint64_t u; } DBits;

static int32_t hi_word(double x) { DBits b; b.d = x; return (int32_t)(b.u >> 32); }
static int32_t lo_word(double x) { DBits b; b.d = x; return (int32_t)b.u; }
static double set_hi(double x, int32_t hi) {
    DBits b; b.d = x;
    b.u = ((uint64_t)(uint32_t)hi << 32) | (uint32_t)b.u;
    return b.d;
}
static double make_d(int32_t hi, int32_t lo) {
    DBits b;
    b.u = ((uint64_t)(uint32_t)hi << 32) | (uint32_t)lo;
    return b.d;
}

#define S1  -1.66666666666666324348e-01
#define S2   8.33333333332248946124e-03
#define S3  -1.98412698298579493134e-04
#define S4   2.75573137070700676789e-06
#define S5  -2.50507602534068634195e-08
#define S6   1.58969099521155010221e-10

static double kernel_sin(double x, double y, int iy) {
    double z, r, v;
    int32_t ix = hi_word(x) & 0x7fffffff;
    if (ix < 0x3e400000) {
        if ((int)x == 0) return x;
    }
    z = x * x;
    v = z * x;
    r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0) return x + v * (S1 + z * r);
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

#define C1   4.16666666666666019037e-02
#define C2  -1.38888888888741095749e-03
#define C3   2.48015872894767294178e-05
#define C4  -2.75573143513906633035e-07
#define C5   2.08757232129817482790e-09
#define C6  -1.13596475577881948265e-11

static double kernel_cos(double x, double y) {
    double a, hz, z, r, qx;
    int32_t ix = hi_word(x) & 0x7fffffff;
    if (ix < 0x3e400000) {
        if ((int)x == 0) return 1.0;
    }
    z = x * x;
    r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    if (ix < 0x3FD33333) return 1.0 - (0.5 * z - (z * r - x * y));
    if (ix > 0x3fe90000) qx = 0.28125;
    else qx = make_d(ix - 0x00200000, 0);
    hz = 0.5 * z - qx;
    a = 1.0 - qx;
    return a - (hz - (z * r - x * y));
}

/* 二分之π 的拆分常量 */
static const double invpio2 = 6.36619772367581382433e-01;
static const double pio2_1  = 1.57079632673412561417e+00;
static const double pio2_1t = 6.07710050650619224932e-11;
static const double pio2_2  = 6.07710050630396597660e-11;
static const double pio2_2t = 2.02226624879595063154e-21;
static const double pio2_3  = 2.02226624871116645580e-21;
static const double pio2_3t = 8.47842766036889956997e-32;

/* 中档归约，|x| < 2^19 * pi/2 */
static int rem_pio2_medium(double x, double *y) {
    int32_t hx = hi_word(x);
    int32_t ix = hx & 0x7fffffff;

    if (ix <= 0x3fe921fb) { y[0] = x; y[1] = 0.0; return 0; }
    if (ix < 0x4002d97c) {
        double z;
        if (hx > 0) {
            z = x - pio2_1;
            y[0] = z - pio2_1t;
            y[1] = (z - y[0]) - pio2_1t;
            return 1;
        }
        z = x + pio2_1;
        y[0] = z + pio2_1t;
        y[1] = (z - y[0]) + pio2_1t;
        return -1;
    }

    double fn = (double)(int32_t)(x * invpio2);
    double r = x - fn * pio2_1;
    double w = fn * pio2_1t;
    y[0] = r - w;

    int32_t j = ix >> 20;
    int32_t i = j - ((hi_word(y[0]) >> 20) & 0x7ff);
    if (i > 16) {
        double t = r;
        w = fn * pio2_2;
        r = t - w;
        w = fn * pio2_2t - ((t - r) - w);
        y[0] = r - w;
        i = j - ((hi_word(y[0]) >> 20) & 0x7ff);
        if (i > 49) {
            t = r;
            w = fn * pio2_3;
            r = t - w;
            w = fn * pio2_3t - ((t - r) - w);
            y[0] = r - w;
        }
    }
    y[1] = (r - y[0]) - w;
    return (int)fn;
}

double js_sin(double x) {
    double y[2];
    int32_t ix = hi_word(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) {
        if (ix < 0x3e500000) return x;
        return kernel_sin(x, 0.0, 0);
    }
    if (ix >= 0x7ff00000) return x - x;
    int n = rem_pio2_medium(x, y);
    switch (n & 3) {
    case 0: return  kernel_sin(y[0], y[1], 1);
    case 1: return  kernel_cos(y[0], y[1]);
    case 2: return -kernel_sin(y[0], y[1], 1);
    default: return -kernel_cos(y[0], y[1]);
    }
}

double js_cos(double x) {
    double y[2];
    int32_t ix = hi_word(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) {
        if (ix < 0x3e400000) return 1.0;
        return kernel_cos(x, 0.0);
    }
    if (ix >= 0x7ff00000) return x - x;
    int n = rem_pio2_medium(x, y);
    switch (n & 3) {
    case 0: return  kernel_cos(y[0], y[1]);
    case 1: return -kernel_sin(y[0], y[1], 1);
    case 2: return -kernel_cos(y[0], y[1]);
    default: return  kernel_sin(y[0], y[1], 1);
    }
}
