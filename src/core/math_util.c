#include "math_util.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

/* Mulberry32 确定性随机数 */
float qlcg_rand01(uint32_t *state) {
    uint32_t t = (*state += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (float)((t ^ (t >> 14)) >> 8) * (1.0f / 16777216.0f);
}

/* 32位整数混淆哈希 */
uint32_t qlcg_hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* 字符串种子稳定哈希（对齐 base） */
uint32_t qlcg_hash_str(const char *str) {
    if (!str || !*str) return 20261027u;
    char *end = NULL;
    long long val = strtoll(str, &end, 10);
    if (end && *end == '\0') return (uint32_t)val;
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)str; *p; ++p) {
        h = (h ^ *p) * 16777619u;
    }
    return h ? h : 1u;
}

/* 二维格点哈希转 [0,1) 浮点 */
float qlcg_hash2(int x, int z, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + seed * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

static inline float hash3_f(int x, int y, int z, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u +
                 (uint32_t)z * 2147483647u + seed * 1664525u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0x00FFFFFFu) * (2.0f / 16777215.0f) - 1.0f;
}

/* 三维连续平滑值噪声 */
float qlcg_noise3(float x, float y, float z, uint32_t seed) {
    int ix = (int)floorf(x);
    int iy = (int)floorf(y);
    int iz = (int)floorf(z);
    float fx = x - (float)ix;
    float fy = y - (float)iy;
    float fz = z - (float)iz;
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fy * fy * (3.0f - 2.0f * fy);
    float w = fz * fz * (3.0f - 2.0f * fz);

    float n000 = hash3_f(ix,     iy,     iz,     seed);
    float n100 = hash3_f(ix + 1, iy,     iz,     seed);
    float n010 = hash3_f(ix,     iy + 1, iz,     seed);
    float n110 = hash3_f(ix + 1, iy + 1, iz,     seed);
    float n001 = hash3_f(ix,     iy,     iz + 1, seed);
    float n101 = hash3_f(ix + 1, iy,     iz + 1, seed);
    float n011 = hash3_f(ix,     iy + 1, iz + 1, seed);
    float n111 = hash3_f(ix + 1, iy + 1, iz + 1, seed);

    float nx00 = qlcg_mixf(n000, n100, u);
    float nx10 = qlcg_mixf(n010, n110, u);
    float nx01 = qlcg_mixf(n001, n101, u);
    float nx11 = qlcg_mixf(n011, n111, u);
    return qlcg_mixf(qlcg_mixf(nx00, nx10, v), qlcg_mixf(nx01, nx11, v), w);
}

/* 三维分形布朗运动噪声 */
float qlcg_fbm3(float x, float y, float z, uint32_t seed, int octaves) {
    float sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * qlcg_noise3(x * freq, y * freq, z * freq, seed + (uint32_t)i * 101u);
        norm += amp;
        freq *= 2.02f;
        amp *= 0.5f;
    }
    return norm > 0.0f ? (sum / norm) : 0.0f;
}

/* 高精度毫秒计时器 */
double qlcg_now_ms(void) {
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
}

void qlcg_mat4_identity(float m[16]) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void qlcg_mat4_perspective(float m[16], float fovy_rad, float aspect, float zn, float zf) {
    float f = 1.0f / tanf(fovy_rad * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0]  = f / (aspect > 0.01f ? aspect : 1.0f);
    m[5]  = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0f;
    m[14] = (2.0f * zf * zn) / (zn - zf);
}

void qlcg_mat4_look_dir(float m[16], const float eye[3], float yaw, float pitch) {
    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    float fx = sy * cp, fy = sp, fz = -cy * cp;
    float rx = cy, ry = 0.0f, rz = sy;
    float ux = ry * fz - rz * fy;
    float uy = rz * fx - rx * fz;
    float uz = rx * fy - ry * fx;

    m[0] = rx;  m[4] = ry;  m[8]  = rz;  m[12] = -(rx * eye[0] + ry * eye[1] + rz * eye[2]);
    m[1] = ux;  m[5] = uy;  m[9]  = uz;  m[13] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
    m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] =  (fx * eye[0] + fy * eye[1] + fz * eye[2]);
    m[3] = 0;   m[7] = 0;   m[11] = 0;   m[15] = 1.0f;
}

void qlcg_mat4_mul(const float a[16], const float b[16], float out[16]) {
    float r[16];
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            r[c * 4 + row] =
                a[0 * 4 + row] * b[c * 4 + 0] +
                a[1 * 4 + row] * b[c * 4 + 1] +
                a[2 * 4 + row] * b[c * 4 + 2] +
                a[3 * 4 + row] * b[c * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

void qlcg_heap_init(MinHeap *h, int cap) {
    h->cap   = cap > 16 ? cap : 16;
    h->size  = 0;
    h->nodes = (int32_t *)malloc((size_t)h->cap * sizeof(int32_t));
    h->keys  = (float *)malloc((size_t)h->cap * sizeof(float));
}

void qlcg_heap_free(MinHeap *h) {
    free(h->nodes);
    free(h->keys);
    h->nodes = NULL;
    h->keys  = NULL;
    h->size = h->cap = 0;
}

void qlcg_heap_push(MinHeap *h, int32_t node, float key) {
    if (h->size >= h->cap) {
        h->cap *= 2;
        h->nodes = (int32_t *)realloc(h->nodes, (size_t)h->cap * sizeof(int32_t));
        h->keys  = (float *)realloc(h->keys, (size_t)h->cap * sizeof(float));
    }
    int i = h->size++;
    while (i > 0) {
        int p = (i - 1) >> 1;
        if (h->keys[p] < key || (h->keys[p] == key && h->nodes[p] <= node)) break;
        h->nodes[i] = h->nodes[p];
        h->keys[i]  = h->keys[p];
        i = p;
    }
    h->nodes[i] = node;
    h->keys[i]  = key;
}

bool qlcg_heap_pop(MinHeap *h, int32_t *out_node, float *out_key) {
    if (h->size <= 0) return false;
    *out_node = h->nodes[0];
    *out_key  = h->keys[0];
    int last = --h->size;
    if (last > 0) {
        int32_t ln = h->nodes[last];
        float   lk = h->keys[last];
        int i = 0;
        while (1) {
            int l = (i << 1) + 1;
            if (l >= last) break;
            int r = l + 1;
            int c = (r < last && (h->keys[r] < h->keys[l] ||
                     (h->keys[r] == h->keys[l] && h->nodes[r] < h->nodes[l]))) ? r : l;
            if (lk < h->keys[c] || (lk == h->keys[c] && ln <= h->nodes[c])) break;
            h->nodes[i] = h->nodes[c];
            h->keys[i]  = h->keys[c];
            i = c;
        }
        h->nodes[i] = ln;
        h->keys[i]  = lk;
    }
    return true;
}
