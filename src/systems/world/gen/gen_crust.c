/* 大陆壳：主核取最大分离，核间形成盆地与海岸 */
#include "gen_crust.h"
#include "gen_noise.h"
#include "../../../core/math_util.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define CRUST_TAU 6.283185307179586f

typedef struct { int count; float basin; } CrustProfile;

static int cmp_float(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static float rand01(uint32_t *s) {
    uint32_t t = (*s += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    uint32_t a = t ^ (t >> 7);
    uint32_t b = a * (t | 61u);
    t = t ^ (t + b);
    return (float)((double)(t ^ (t >> 14)) / 4294967296.0);
}

static float smooth01(float v) {
    v = qlcg_clampf(v, 0.0f, 1.0f);
    return v * v * (3.0f - 2.0f * v);
}

static void sphere_point(float lon, float y, float out[3]) {
    float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
    out[0] = r * cosf(lon);
    out[1] = y;
    out[2] = r * sinf(lon);
}

static float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void unit3(float v[3]) {
    float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n < 1e-9f) n = 1.0f;
    v[0] /= n; v[1] /= n; v[2] /= n;
}

static float noise3(float x, float y, float z, uint32_t seed) {
    return qlcg_base_noise(x, y, z, seed);
}

static CrustProfile profile_for(MacroTerrainRecipe r) {
    CrustProfile p;
    switch (r) {
        case TERRAIN_ARCHIPELAGO: p.count = 10; p.basin = 1.20f; break;
        case TERRAIN_MOUNTAINS:   p.count = 4;  p.basin = 0.80f; break;
        case TERRAIN_RIFT:        p.count = 6;  p.basin = 1.15f; break;
        default:                  p.count = 5;  p.basin = 1.00f; break;
    }
    return p;
}

void qlcg_crust_build(CrustField *cf, const MacroWorld *w, const TectonicPlate *plates,
                      int plate_count, const WorldCreatorOptions *opt, float angle) {
    memset(cf, 0, sizeof(*cf));
    CrustProfile prof = profile_for(opt->terrain_recipe);
    int count = prof.count;
    if (count > plate_count) count = plate_count;
    if (count > QLCG_CRUST_MAX) count = QLCG_CRUST_MAX;
    cf->count = count;
    cf->angle = angle;

    float sea = 1.0f - qlcg_clampf(opt->land_ratio, 0.0f, 1.0f);
    cf->half_width = (0.04f * sea + 0.05f * sea * sea) * prof.basin;
    cf->max_sin = (cf->half_width > 0.0f)
                  ? sinf(fminf(1.5707963f, cf->half_width * 1.2f)) : 0.0f;
    cf->seed_basin = opt->seed ^ 0x62617369u;
    cf->seed_crust = opt->seed ^ 0x63727573u;
    cf->seed_coast = opt->seed ^ 0x636f6173u;

    uint32_t rng = opt->seed ^ 0x63727573u;
    for (int k = 0; k < count; k++) {
        float best = -1e9f, c[3] = {1.0f, 0.0f, 0.0f};
        for (int n = 0; n < 32; n++) {
            /* 显式两步取随机，C 的实参求值顺序未定义 */
            float lon = rand01(&rng) * CRUST_TAU;
            float yy = rand01(&rng) * 2.0f - 1.0f;
            float p[3];
            sphere_point(lon, yy, p);
            float dist = 1.0f;
            if (k > 0) {
                float mx = -1e9f;
                for (int j = 0; j < k; j++) {
                    float dd = p[0] * cf->cx[j] + p[1] * cf->cy[j] + p[2] * cf->cz[j];
                    if (dd > mx) mx = dd;
                }
                dist = 1.0f - mx;
            }
            if (dist > best) {
                best = dist;
                c[0] = p[0]; c[1] = p[1]; c[2] = p[2];
            }
        }
        cf->cx[k] = c[0]; cf->cy[k] = c[1]; cf->cz[k] = c[2];
        cf->cb[k] = (rand01(&rng) - 0.5f) * 0.035f;
        /* owner 仅用于统计，这里不保存 */
        (void)plates;
    }

    for (int k = 0; k < count; k++) {
        for (int j = 0; j < count; j++) {
            if (j == k) { cf->chords[k * count + j] = 0.0f; continue; }
            float dx = cf->cx[k] - cf->cx[j];
            float dy = cf->cy[k] - cf->cy[j];
            float dz = cf->cz[k] - cf->cz[j];
            cf->chords[k * count + j] = sqrtf(dx * dx + dy * dy + dz * dz);
        }
    }

    /* 黄金角采样 2048 点，按陆比取参考分位 */
    float samples[2048];
    const int NS = 2048;
    float golden = 3.14159265358979f * (3.0f - 2.23606797749979f);
    float phase = rand01(&rng) * CRUST_TAU;
    for (int k = 0; k < NS; k++) {
        float p[3];
        sphere_point((float)k * golden + phase, 1.0f - 2.0f * ((float)k + 0.5f) / (float)NS, p);
        samples[k] = qlcg_crust_score(cf, p[0], p[1], p[2], NULL, NULL);
    }
    /* 插入排序改 qsort，避免 2048 元素平方复杂度 */
    qsort(samples, (size_t)NS, sizeof(float), cmp_float);
    float lr = qlcg_clampf(opt->land_ratio, 0.0f, 1.0f);
    if (lr <= 0.0f) cf->reference = 2.0f;
    else if (lr >= 1.0f) cf->reference = -2.0f;
    else {
        int idx = (int)((float)NS * (1.0f - lr));
        if (idx < 0) idx = 0;
        if (idx > NS - 1) idx = NS - 1;
        cf->reference = samples[idx];
    }
    (void)w;
}

float qlcg_crust_score(const CrustField *cf, float px, float py, float pz,
                       float *out_basin, int *out_core) {
    int count = cf->count;
    float best = -1e9f, second = -1e9f;
    int a = 0, b = 0;
    for (int k = 0; k < count; k++) {
        float v = px * cf->cx[k] + py * cf->cy[k] + pz * cf->cz[k] + cf->cb[k];
        if (v > best) {
            second = best; b = a;
            best = v; a = k;
        } else if (v > second) {
            second = v; b = k;
        }
    }
    float diff = best - second;
    float chord = cf->chords[a * count + b];
    float basin = 0.0f;
    if (cf->half_width > 0.0f && diff < chord * cf->max_sin && chord > 1e-6f) {
        float distance = asinf(qlcg_clampf(diff / chord, -1.0f, 1.0f));
        float width = cf->half_width * (1.0f + 0.2f * noise3(px * 3.0f, py * 3.0f, pz * 3.0f, cf->seed_basin));
        if (width > 1e-6f) basin = 1.0f - smooth01(distance / width);
        else basin = 0.0f;
    }
    float coast = noise3(px * 4.1f, py * 4.1f, pz * 4.1f, cf->seed_crust) * 0.025f
                + noise3(px * 11.0f, py * 11.0f, pz * 11.0f, cf->seed_coast) * 0.008f;
    if (out_basin) *out_basin = basin;
    if (out_core) *out_core = a;
    return best + coast - basin * 0.16f;
}

float qlcg_crust_continental_xyz(const CrustField *cf, float px, float py, float pz,
                                 const TectonicPlate *plate, float fraction) {
    float t = -cf->angle * plate->speed * fraction;
    float c = cosf(t), s = sinf(t);
    float ax = plate->axis[0], ay = plate->axis[1], az = plate->axis[2];
    float d = ax * px + ay * py + az * pz;
    float k = 1.0f - c;
    float rx = px * c + (ay * pz - az * py) * s + ax * d * k;
    float ry = py * c + (az * px - ax * pz) * s + ay * d * k;
    float rz = pz * c + (ax * py - ay * px) * s + az * d * k;
    float score = qlcg_crust_score(cf, rx, ry, rz, NULL, NULL);
    return smooth01((score - cf->reference) / 0.045f + 0.5f);
}

int qlcg_crust_core_count(const CrustField *cf) {
    return cf->count;
}
