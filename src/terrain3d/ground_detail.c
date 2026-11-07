/* 五气候带 256m 可平铺贴图，启动生成后只采样 */
#include "ground_detail.h"
#include "../core/math_util.h"
#include <math.h>
#include <stdlib.h>

/* 材质基色：冰封 / 寒带 / 温带 / 亚热 / 热带 */
static const float BAND_GRASS[QLCG_GROUND_BAND_N][3] = {
    { 138, 146, 126 }, { 130, 144, 118 }, { 122, 138, 92 }, { 122, 140, 86 }, { 140, 152, 88 }
};
static const float BAND_SOIL[QLCG_GROUND_BAND_N][3] = {
    { 172, 171, 150 }, { 165, 163, 148 }, { 150, 128, 96 }, { 158, 132, 94 }, { 146, 104, 74 }
};
static const float BAND_GRAVEL[QLCG_GROUND_BAND_N][3] = {
    { 156, 158, 152 }, { 150, 149, 141 }, { 139, 134, 130 }, { 148, 138, 120 }, { 152, 134, 112 }
};

static float ss(float a, float b, float v) {
    float t = qlcg_clampf((v - a) / ((b - a) != 0.0f ? (b - a) : 1.0f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* 周期哈希：格点对 n 取模，纹理首尾相接 */
static float phash(int x, int z, int n, uint32_t s) {
    int m = ((x % n) + n) % n;
    int q = ((z % n) + n) % n;
    uint32_t h = (uint32_t)m * 73856093u ^ (uint32_t)q * 19349663u ^ s;
    h = (h ^ (h >> 16)) * 0x7feb352du;
    h = (h ^ (h >> 15)) * 0x846ca68bu;
    return (float)((double)(h ^ (h >> 16)) / 2147483648.0) - 1.0f;
}

static float pnoise(float x, float z, int n, uint32_t s) {
    int ix = (int)floorf(x), iz = (int)floorf(z);
    float tx = x - (float)ix, tz = z - (float)iz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    float a = phash(ix, iz, n, s),     b = phash(ix + 1, iz, n, s);
    float c = phash(ix, iz + 1, n, s), d = phash(ix + 1, iz + 1, n, s);
    float x0 = a + (b - a) * tx, x1 = c + (d - c) * tx;
    return x0 + (x1 - x0) * tz;
}

void qlcg_ground_tex_at(float wx, float wz, uint32_t seed, int band, float out[3]) {
    if (band < 0) band = 0;
    if (band >= QLCG_GROUND_BAND_N) band = QLCG_GROUND_BAND_N - 1;
    const float *gr = BAND_GRASS[band];
    const float *so = BAND_SOIL[band];
    const float *gv = BAND_GRAVEL[band];
    /* 32/8/2/0.5m 四层，256m 内均为整数格 */
    float n32 = pnoise(wx / 32.0f, wz / 32.0f, 8, seed ^ 0x11u);
    float n8  = pnoise(wx / 8.0f,  wz / 8.0f,  32, seed ^ 0x22u);
    float n2  = pnoise(wx / 2.0f,  wz / 2.0f, 128, seed ^ 0x33u);
    float n05 = pnoise(wx / 0.5f, wz / 0.5f, 512, seed ^ 0x44u);

    float soil_base = qlcg_clampf(0.34f + n32 * 0.30f + n8 * 0.18f, 0.0f, 1.0f);
    float tuft  = ss(-0.16f, 0.20f, n2);
    float stone = ss(0.10f, 0.34f, n05 + soil_base * 0.22f) * (0.35f + 0.45f * soil_base);
    float k = 1.0f + n05 * 0.11f + n2 * 0.07f;

    for (int ch = 0; ch < 3; ch++) {
        float c = gr[ch];
        c += (so[ch] - c) * soil_base * 0.74f;
        c += (gv[ch] - c) * stone * 0.70f;
        c *= k * (0.92f + tuft * 0.18f);
        out[ch] = qlcg_clampf(c, 0.0f, 255.0f);
    }
}

void qlcg_ground_tex_build(uint8_t *rgb, int px, float span, int band, uint32_t seed) {
    if (!rgb || px <= 0) return;
    float step = span / (float)px;
    for (int z = 0; z < px; z++) {
        float wz = ((float)z + 0.5f) * step;
        for (int x = 0; x < px; x++) {
            float wx = ((float)x + 0.5f) * step;
            float c[3];
            qlcg_ground_tex_at(wx, wz, seed, band, c);
            size_t o = ((size_t)z * (size_t)px + (size_t)x) * 3;
            rgb[o + 0] = (uint8_t)qlcg_clampi((int)(c[0] + 0.5f), 0, 255);
            rgb[o + 1] = (uint8_t)qlcg_clampi((int)(c[1] + 0.5f), 0, 255);
            rgb[o + 2] = (uint8_t)qlcg_clampi((int)(c[2] + 0.5f), 0, 255);
        }
    }
}
