#ifndef QLCG_MATH_UTIL_H
#define QLCG_MATH_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define QLCG_PI  3.14159265358979323846f
#define QLCG_TAU 6.28318530717958647692f

/* 基础标量与插值内联函数 */
static inline float qlcg_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int qlcg_clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float qlcg_mixf(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float qlcg_lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float qlcg_minf(float a, float b) {
    return a < b ? a : b;
}

static inline float qlcg_maxf(float a, float b) {
    return a > b ? a : b;
}

static inline void qlcg_vec3_norm(float *x, float *y, float *z) {
    float l = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
    if (l > 1e-8f) {
        float inv = 1.0f / l;
        *x *= inv; *y *= inv; *z *= inv;
    } else {
        *x = 0.0f; *y = 1.0f; *z = 0.0f;
    }
}

static inline float qlcg_smoothf(float e0, float e1, float x) {
    if (e1 == e0) return x < e0 ? 0.0f : 1.0f;
    float t = qlcg_clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* 三维向量运算 */
static inline float qlcg_dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void qlcg_cross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static inline void qlcg_unit3(float v[3]) {
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-8f) {
        float inv = 1.0f / l;
        v[0] *= inv; v[1] *= inv; v[2] *= inv;
    } else {
        v[0] = 1.0f; v[1] = 0.0f; v[2] = 0.0f;
    }
}

/* 随机哈希、三维连续噪声与矩阵 */
float    qlcg_rand01(uint32_t *state);
uint32_t qlcg_hash_u32(uint32_t x);
uint32_t qlcg_hash_str(const char *str);
float    qlcg_hash2(int x, int z, uint32_t seed);
float    qlcg_noise3(float x, float y, float z, uint32_t seed);
float    qlcg_fbm3(float x, float y, float z, uint32_t seed, int octaves);
double   qlcg_now_ms(void);
#define  qlcg_hash32  qlcg_hash_u32
#define  qlcg_time_ms qlcg_now_ms

void     qlcg_mat4_identity(float m[16]);
void     qlcg_mat4_perspective(float m[16], float fovy_rad, float aspect, float zn, float zf);
void     qlcg_mat4_look_dir(float m[16], const float eye[3], float yaw, float pitch);
void     qlcg_mat4_mul(const float a[16], const float b[16], float out[16]);

/* 最小堆结构（用于水文与寻路） */
typedef struct {
    int32_t *nodes;
    float   *keys;
    int      size;
    int      cap;
} MinHeap;

void qlcg_heap_init(MinHeap *h, int cap);
void qlcg_heap_free(MinHeap *h);
void qlcg_heap_push(MinHeap *h, int32_t node, float key);
bool qlcg_heap_pop(MinHeap *h, int32_t *out_node, float *out_key);

#endif /* QLCG_MATH_UTIL_H */
