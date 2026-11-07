/* 河谷：沿噪声谷线低洼处下切，同时给出摩擦系数 */
#include "gen_valleys.h"
#include "gen_noise.h"
#include "../../../core/math_util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static float smooth01(float v) {
    float t = qlcg_clampf(v, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void qlcg_macro_valleys(MacroWorld *w) {
    if (!w || !w->hydro_friction) return;
    int n = w->cell_count;
    float *original = (float *)malloc((size_t)n * sizeof(float));
    if (!original) return;
    memcpy(original, w->height, (size_t)n * sizeof(float));
    float scale = w->radius / 260000.0f;
    uint32_t s1 = w->opt.seed ^ 0x62d4a73bu;
    uint32_t s2 = w->opt.seed ^ 0x76616c32u;

    for (int i = 0; i < n; i++) {
        float h = original[i];
        w->hydro_friction[i] = 1.0f;
        if (h <= 0.0f) continue;
        float x = w->points[i * 3] * scale;
        float y = w->points[i * 3 + 1] * scale;
        float z = w->points[i * 3 + 2] * scale;
        float sn = qlcg_base_noise(x, y, z, s1) * 0.8f
                 + qlcg_base_noise(x * 0.37f, y * 0.37f, z * 0.37f, s2) * 0.2f;
        float valley = expf(-sn * sn / 0.009f);
        float low = h, high = h;
        for (int d = 0; d < 8; d++) {
            float v = fmaxf(0.0f, original[w->neighbors[i * 8 + d]]);
            if (v < low) low = v;
            if (v > high) high = v;
        }
        float gentle = 1.0f - smooth01((high - low - 80.0f) / 320.0f);
        float inland = smooth01(h / 60.0f);
        float mountain = 1.0f - smooth01((h - 900.0f) / 1300.0f);
        float floor_h = fminf(h, fmaxf(1.0f, low + 0.25f));
        w->height[i] = fmaxf(floor_h, h - valley * 75.0f * gentle * inland * mountain);
        w->hydro_friction[i] = expf(sn * 1.5f - valley * 1.7f * gentle);
    }
    free(original);
}
