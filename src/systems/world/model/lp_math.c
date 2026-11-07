/* 低模几何数学层实现 */
#pragma float_control(precise, on)
#pragma fp_contract(off)

#include "lp_math.h"

#include <math.h>
#include <string.h>

#define LP_EPS 1e-9

static double js_round(double v) {
    return floor(v + 0.5);
}

const char *qlcg_lp_mat_name(int mat) {
    switch (mat) {
    case LP_MAT_SOLID: return "solid";
    case LP_MAT_SNOW:  return "snow";
    case LP_MAT_BLOOM: return "bloom";
    case LP_MAT_FRUIT: return "fruit";
    default:           return "?";
    }
}

int qlcg_lp_mat_index(const char *name) {
    if (!name) return LP_MAT_SOLID;
    if (strcmp(name, "snow") == 0)  return LP_MAT_SNOW;
    if (strcmp(name, "bloom") == 0) return LP_MAT_BLOOM;
    if (strcmp(name, "fruit") == 0) return LP_MAT_FRUIT;
    return LP_MAT_SOLID;
}

double qlcg_lp_clamp(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

double qlcg_lp_clamp01(double v) {
    return qlcg_lp_clamp(v, 0.0, 1.0);
}

int qlcg_lp_clamp_byte(double v) {
    double c = qlcg_lp_clamp(v, 0.0, 255.0);
    return (int)js_round(c);
}

LpRgb qlcg_lp_rgb(double r, double g, double b) {
    LpRgb c;
    c.r = r; c.g = g; c.b = b;
    c.wind = LP_WIND_SOLID;
    return c;
}

LpRgb qlcg_lp_wind_color(LpRgb c, int part) {
    c.wind = part;
    return c;
}

static int hex_digit(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

LpRgb qlcg_lp_hex(const char *hex, double fr, double fg, double fb) {
    LpRgb out = qlcg_lp_rgb(fr, fg, fb);
    if (!hex) return out;
    /* 跳过若干空白，允许前置 # */
    const char *p = hex;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == '#') p++;
    for (int i = 0; i < 6; i++) {
        if (hex_digit((unsigned char)p[i]) < 0) return out;
    }
    /* 必须恰好六位 */
    if (p[6] != '\0') {
        const char *q = p + 6;
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
        if (*q != '\0') return out;
    }
    out.r = (double)((hex_digit(p[0]) << 4) | hex_digit(p[1]));
    out.g = (double)((hex_digit(p[2]) << 4) | hex_digit(p[3]));
    out.b = (double)((hex_digit(p[4]) << 4) | hex_digit(p[5]));
    return out;
}

static int str_has(const char *s, const char *sub) {
    return s && strstr(s, sub) != NULL;
}

static int str_eq(const char *s, const char *lit) {
    return s && strcmp(s, lit) == 0;
}

static int str_starts(const char *s, const char *lit) {
    return s && strncmp(s, lit, strlen(lit)) == 0;
}

/* 同 base paletteWindPart 判定顺序 */
int qlcg_lp_palette_wind(const char *key) {
    if (!key) return LP_WIND_SOLID;
    if (str_has(key, "bark")) return LP_WIND_BARK;
    if (str_has(key, "Culm") || str_has(key, "Stem") ||
        str_eq(key, "stem") || str_eq(key, "bambooJoint")) return LP_WIND_SOLID;
    if (str_has(key, "foliage") || str_has(key, "canopy") || str_has(key, "conifer") ||
        str_has(key, "larch") || str_has(key, "jungle") || str_has(key, "Frond") ||
        str_has(key, "grassDry") || str_has(key, "waterweed") ||
        str_eq(key, "bamboo") || str_eq(key, "tea") || str_eq(key, "mulberry") ||
        str_has(key, "crop") || str_has(key, "riceGreen") || str_has(key, "sorghum") ||
        str_has(key, "witheredNeedle") || str_starts(key, "withered")) return LP_WIND_LEAF;
    return LP_WIND_SOLID;
}

LpRgb qlcg_lp_mix(LpRgb a, LpRgb b, double t) {
    double k = qlcg_lp_clamp01(t);
    LpRgb out;
    out.r = a.r + (b.r - a.r) * k;
    out.g = a.g + (b.g - a.g) * k;
    out.b = a.b + (b.b - a.b) * k;
    out.wind = a.wind;
    return out;
}

LpRgb qlcg_lp_deepen(LpRgb c, double depth) {
    double k = qlcg_lp_clamp01(1.0 - depth);
    LpRgb out;
    out.r = c.r * k;
    out.g = c.g * k;
    out.b = c.b * k;
    out.wind = c.wind;
    return out;
}

LpRgb qlcg_lp_lighten(LpRgb c, double amount) {
    double k = qlcg_lp_clamp01(amount);
    LpRgb out;
    out.r = c.r + (255.0 - c.r) * k;
    out.g = c.g + (255.0 - c.g) * k;
    out.b = c.b + (255.0 - c.b) * k;
    out.wind = c.wind;
    return out;
}

double qlcg_lp_shade_of(double nx, double ny, double nz) {
    static const double steps[4] = { 0.58, 0.7, 0.84, 1.0 };
    double d = nx * 0.42 + ny * 0.84 + nz * (-0.34);
    double t = (d + 1.0) / 2.0;
    double i = js_round(t * (double)(4 - 1));
    int idx = (int)i;
    if (idx < 0) idx = 0;
    if (idx > 3) idx = 3;
    return steps[idx];
}

/* 同 ES Math.hypot：先缩放再开方 */
double js_hypot2(double a, double b) {
    double aa = fabs(a), ab = fabs(b);
    double max = aa > ab ? aa : ab;
    if (max == 0.0) return 0.0;
    if (!isfinite(max)) return max;
    double x = a / max, y = b / max;
    return max * sqrt(x * x + y * y);
}

double js_hypot3(double a, double b, double c) {
    double aa = fabs(a), ab = fabs(b), ac = fabs(c);
    double max = aa;
    if (ab > max) max = ab;
    if (ac > max) max = ac;
    if (max == 0.0) return 0.0;
    if (!isfinite(max)) return max;
    double x = a / max, y = b / max, z = c / max;
    return max * sqrt(x * x + y * y + z * z);
}

void qlcg_lp_face_normal(const double a[3], const double b[3], const double c[3], double out[3]) {
    double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    double nx = uy * vz - uz * vy;
    double ny = uz * vx - ux * vz;
    double nz = ux * vy - uy * vx;
    double len = js_hypot3(nx, ny, nz);
    if (len < LP_EPS) {
        out[0] = 0.0; out[1] = 1.0; out[2] = 0.0;
        return;
    }
    out[0] = nx / len;
    out[1] = ny / len;
    out[2] = nz / len;
}

void qlcg_lp_rng_seed(LpRng *r, uint32_t seed) {
    r->a = seed;
}

double qlcg_lp_rng_next(LpRng *r) {
    uint32_t a = r->a + 0x6d2b79f5u;
    r->a = a;
    uint32_t t = (a ^ (a >> 15)) * (1u | a);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return (double)((t ^ (t >> 14)) / 4294967296.0);
}

double qlcg_lp_rand_or_half(LpRng *r) {
    return r ? qlcg_lp_rng_next(r) : 0.5;
}

double qlcg_lp_hash2(int32_t x, int32_t z, int32_t seed) {
    uint32_t h = (uint32_t)x * 0x27d4eb2du;
    h ^= (uint32_t)z * 0x165667b1u;
    h ^= (uint32_t)seed * 0x9e3779b1u;
    h = (h ^ (h >> 15)) * 0x85ebca6bu;
    h = (h ^ (h >> 13)) * 0xc2b2ae35u;
    h ^= h >> 16;
    return (double)(h / 4294967296.0);
}

uint32_t qlcg_lp_hash_string(const char *s) {
    uint32_t h = 0x811c9dc5u;
    if (!s) return h;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp;
        int len;
        if (p[0] < 0x80)               { cp = p[0];        len = 1; }
        else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1Fu; len = 2; }
        else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0Fu; len = 3; }
        else if ((p[0] & 0xF8) == 0xF0) { cp = p[0] & 0x07u; len = 4; }
        else                            { cp = p[0];        len = 1; }
        for (int i = 1; i < len; i++) cp = (cp << 6) | (uint32_t)(p[i] & 0x3F);
        p += len;
        /* 非 BMP 拆成代理对，贴合 JS charCodeAt */
        if (cp >= 0x10000u) {
            uint32_t v = cp - 0x10000u;
            uint32_t hi = 0xD800u + (v >> 10);
            uint32_t lo = 0xDC00u + (v & 0x3FFu);
            h ^= hi; h *= 0x01000193u;
            h ^= lo; h *= 0x01000193u;
        } else {
            h ^= cp; h *= 0x01000193u;
        }
    }
    return h;
}
