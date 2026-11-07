/* 地表材质：七色混合 + 气候带色表 + 四季调色 + 分带斑驳 */
#include "gen_cover.h"
#include "gen_noise.h"
#include "../../../core/math_util.h"
#include <math.h>
#include <string.h>

/* 0冰封…4热，对应区块编号首位1..5 */
enum { GB_POLAR = 0, GB_BOREAL, GB_TEMPERATE, GB_SUBTROPIC, GB_TROPIC, GB_COUNT };

/* 季节温度系数，同 water_ice_state */
static const float SEASON_WARM[4] = { 0.0f, 1.0f, 0.0f, -1.0f };
/* 季节色：春嫩绿 / 夏沉绿 / 秋枯黄 / 冬枯灰 */
static const float SEASON_RGB[4][3] = {
    { 120, 146, 82 }, { 92, 120, 68 }, { 172, 142, 84 }, { 146, 142, 124 }
};
static const float SEASON_AMT[4] = { 0.36f, 0.30f, 0.62f, 0.72f };
/* 低纬干湿季：正为偏干 */
static const float SEASON_DRY[4] = { 0.15f, -0.30f, -0.05f, 0.30f };

/* —— 五带基色：草 / 林土 / 土 / 沙 / 砾 —— */
static const float TONE_GRASS[5][3] = {
    { 138, 146, 126 }, { 130, 144, 118 }, { 122, 138, 92 }, { 122, 140, 86 }, { 140, 152, 88 }
};
static const float TONE_FOREST[5][3] = {
    { 148, 154, 138 }, { 142, 149, 126 }, { 112, 122, 90 }, { 108, 126, 82 }, { 84, 106, 68 }
};
static const float TONE_SOIL[5][3] = {
    { 172, 171, 150 }, { 165, 163, 148 }, { 150, 128, 96 }, { 158, 132, 94 }, { 146, 104, 74 }
};
static const float TONE_SAND[5][3] = {
    { 192, 201, 190 }, { 198, 194, 170 }, { 220, 207, 146 }, { 209, 199, 154 }, { 207, 161, 119 }
};
static const float TONE_GRAVEL[5][3] = {
    { 156, 158, 152 }, { 150, 149, 141 }, { 139, 134, 130 }, { 148, 138, 120 }, { 152, 134, 112 }
};

static float smooth01(float v) {
    float t = qlcg_clampf(v, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float smooth_step(float a, float b, float v) {
    return smooth01((v - a) / ((b - a) != 0.0f ? (b - a) : 1.0f));
}

void qlcg_cover_weights(float temperature, float aridity, float uplift, float slope,
                        CoverWeights *out) {
    float exposure = 0.28f + uplift * 0.25f + slope * 0.7f;
    float snow = 1.0f - smooth_step(-3.0f, 1.0f, temperature);
    float rock = (1.0f - snow) * smooth_step(0.48f, 0.72f, exposure);
    float rest = 1.0f - snow - rock;
    float sand = rest * (1.0f - smooth_step(0.25f, 0.55f, aridity));
    float gravel = (rest - sand) * smooth_step(0.25f, 0.55f, exposure)
                   * (1.0f - smooth_step(0.65f, 1.1f, aridity));
    float soil = rest - sand - gravel;
    float forest = smooth_step(0.9f, 1.6f, aridity) * smooth_step(-5.0f, 10.0f, temperature);
    float bare = 1.0f - smooth_step(0.4f, 0.95f, aridity);
    out->stone = rock;
    out->snow = snow;
    out->sand = sand;
    out->gravel = gravel;
    out->forest_soil = soil * forest;
    out->dirt = soil * (1.0f - forest) * qlcg_clampf(bare, 0.0f, 1.0f);
    out->grass = soil * (1.0f - forest) * (1.0f - qlcg_clampf(bare, 0.0f, 1.0f));
}

/* 温度→带位 0..4，中心断点 -8/8/18/24 */
static float band_pos(float t) {
    float k = smooth_step(-11.0f, -5.0f, t)
            + smooth_step(5.0f, 11.0f, t)
            + smooth_step(15.0f, 21.0f, t)
            + smooth_step(21.0f, 27.0f, t);
    return qlcg_clampf(k, 0.0f, 4.0f);
}

float qlcg_cover_band_of_temp(float temperature) {
    return band_pos(temperature);
}

float qlcg_cover_wet(float aridity) {
    return qlcg_clampf((aridity - 0.20f) / 1.60f, 0.0f, 1.0f);
}

static void tone_mix(const float tab[5][3], float pos, float out[3]) {
    int i = qlcg_clampi((int)pos, 0, 3);
    float f = qlcg_clampf(pos - (float)i, 0.0f, 1.0f);
    for (int k = 0; k < 3; k++) out[k] = tab[i][k] + (tab[i + 1][k] - tab[i][k]) * f;
}

/* 两八度值噪声：3D 噪声取 y=0 平面切片 */
static float fbm2(float x, float z, uint32_t s) {
    return qlcg_base_noise(x, 0.0f, z, s) * 0.62f
         + qlcg_base_noise(x * 2.7f, 0.0f, z * 2.7f, s ^ 0x5b3eu) * 0.38f;
}

typedef struct { float scale, fine, amp, grain; } GrainCfg;

/* 湿度 → 斑块参数：湿润更细密，干旱更粗大 */
static void grain_cfg(int band, float wet, float slope, GrainCfg *c) {
    float dry = 1.0f - wet;
    switch (band) {
    case GB_POLAR:
        c->scale = 70.0f + 190.0f * dry; c->fine = 26.0f;
        c->amp = 0.07f + 0.05f * dry;    c->grain = 0.030f; break;
    case GB_BOREAL:
        c->scale = 22.0f + 34.0f * dry;  c->fine = 9.0f;
        c->amp = 0.16f + 0.10f * dry;    c->grain = 0.060f; break;
    case GB_TEMPERATE:
        c->scale = 70.0f + 90.0f * dry;  c->fine = 24.0f;
        c->amp = 0.20f + 0.10f * dry;    c->grain = 0.085f; break;
    case GB_SUBTROPIC:
        c->scale = 34.0f + 60.0f * dry;  c->fine = 12.0f;
        c->amp = 0.18f + 0.10f * dry;    c->grain = 0.070f; break;
    default:
        c->scale = 10.0f + 22.0f * dry;  c->fine = 4.0f;
        c->amp = 0.10f + 0.12f * dry;    c->grain = 0.050f; break;
    }
    c->amp *= 1.0f + qlcg_clampf(slope, 0.0f, 1.0f) * 0.5f;
}

/* 冰封带：风雪脊 + 吹蚀条纹 */
static float grain_polar(float x, float z, const GrainCfg *c, uint32_t s) {
    float n = fbm2(x / c->scale, z / c->scale, s);
    float r = 1.0f - fabsf(fbm2(x / c->fine, z / c->fine, s ^ 0x11u));
    float w = sinf((z + n * c->fine * 1.6f) * (6.2831853f / (c->fine * 2.4f)));
    return (r - 0.55f) * 1.1f + w * 0.32f;
}

/* 寒带：冻融多边形斑 */
static float grain_boreal(float x, float z, const GrainCfg *c, uint32_t s) {
    float a = fbm2(x / c->scale, z / c->scale, s);
    float b = fbm2(x / c->fine, z / c->fine, s ^ 0x22u);
    return (1.0f - fabsf(a * 1.7f + b * 0.6f) - 0.55f) * 1.5f;
}

/* 温带：草甸土斑 */
static float grain_temperate(float x, float z, const GrainCfg *c, uint32_t s) {
    return fbm2(x / c->scale, z / c->scale, s) * 0.62f
         + fbm2(x / c->fine, z / c->fine, s ^ 0x33u) * 0.38f;
}

/* 亚热带：灌丛泥斑叠细粒 */
static float grain_subtropic(float x, float z, const GrainCfg *c, uint32_t s) {
    float fine = qlcg_base_noise(x / (c->fine * 0.45f), 0.0f, z / (c->fine * 0.45f), s ^ 0x45u);
    return fbm2(x / c->scale, z / c->scale, s) * 0.50f
         + fbm2(x / c->fine, z / c->fine, s ^ 0x44u) * 0.34f + fine * 0.16f;
}

/* 热带：雨林叶隙细斑 / 稀树干湿斑 */
static float grain_tropic(float x, float z, const GrainCfg *c, uint32_t s) {
    return fbm2(x / c->scale, z / c->scale, s) * 0.55f
         + fbm2(x / c->fine, z / c->fine, s ^ 0x55u) * 0.45f;
}

static float grain_at(int band, float x, float z, const GrainCfg *c, uint32_t s) {
    switch (band) {
    case GB_POLAR:     return grain_polar(x, z, c, s);
    case GB_BOREAL:    return grain_boreal(x, z, c, s);
    case GB_TEMPERATE: return grain_temperate(x, z, c, s);
    case GB_SUBTROPIC: return grain_subtropic(x, z, c, s);
    default:           return grain_tropic(x, z, c, s);
    }
}

void qlcg_cover_color(const MacroWorld *w, float px, float py, float pz,
                      float temperature, float aridity, float uplift, float slope,
                      const CoverSeason *season, uint8_t out[3], int *out_material) {
    float radius = w ? w->radius : 512.0f;
    uint32_t seed = w ? w->opt.seed : 0x51ab3c7du;
    float broad = qlcg_base_noise(px * radius / 512.0f, py * radius / 512.0f, pz * radius / 512.0f,
                                  seed ^ 0x67726f31u);
    float patch = qlcg_base_noise(px * radius / 96.0f, py * radius / 96.0f, pz * radius / 96.0f,
                                  seed ^ 0x67726f32u);
    float mx = px * radius, mz = pz * radius;

    /* 季节：高纬偏冷暖，低纬偏干湿 */
    int sea = (season && season->season >= 0 && season->season < 4) ? season->season : -1;
    float lat = (sea >= 0) ? season->latitude : 0.0f;
    float ampl = fabsf(sinf(lat));
    float t_eff = temperature + (sea >= 0 ? 16.0f * sinf(lat) * SEASON_WARM[sea] : 0.0f);
    float arid = qlcg_maxf(aridity, 0.0f) * (1.0f + broad * 0.3f + patch * 0.14f);
    if (sea >= 0) {
        float dry = SEASON_DRY[sea] * (lat >= 0.0f ? 1.0f : -1.0f);
        arid = qlcg_maxf(arid * (1.0f - dry * (1.0f - ampl) * 1.15f), 0.02f);
    }

    CoverWeights cw;
    qlcg_cover_weights(t_eff + broad * 1.5f + patch, arid, uplift, slope, &cw);

    /* 斑驳：按年均温分带取专属噪声，参数随温湿 */
    float pos = band_pos(temperature);
    int b0 = qlcg_clampi((int)pos, 0, GB_COUNT - 2);
    float bw = qlcg_clampf(pos - (float)b0, 0.0f, 1.0f);
    float wet = qlcg_clampf((aridity - 0.20f) / 1.60f, 0.0f, 1.0f);
    GrainCfg c0, c1;
    grain_cfg(b0, wet, slope, &c0);
    float nb = grain_at(b0, mx, mz, &c0, seed ^ 0x67726f33u);
    if (bw > 0.02f) {
        grain_cfg(b0 + 1, wet, slope, &c1);
        float nb1 = grain_at(b0 + 1, mx, mz, &c1, seed ^ 0x67726f33u);
        nb += (nb1 - nb) * bw;
    }
    nb = qlcg_clampf(nb * 1.35f, -1.0f, 1.0f);

    /* 草与土/砾互让；湿润处少出露 */
    float expo = qlcg_clampf(0.35f + slope * 0.65f + (1.0f - wet) * 0.4f, 0.0f, 1.4f);
    /* 顶点色只保留气候带基色与季节，斑驳交给区块细节图 */
    float mixk = 1.0f;
    if (mixk < 0.25f) mixk = 0.25f;
    if (mixk > 4.0f) mixk = 4.0f;
    float wgt[7] = { cw.stone, cw.snow, cw.sand, cw.gravel, cw.forest_soil, cw.dirt, cw.grass };
    wgt[2] *= 1.0f + nb * c0.amp * 2.0f;
    wgt[3] *= mixk;
    wgt[5] *= mixk;
    wgt[6] *= 1.0f / mixk;
    float sum = 0.0f;
    for (int i = 0; i < 7; i++) {
        if (wgt[i] < 0.0f) wgt[i] = 0.0f;
        sum += wgt[i];
    }
    if (sum < 1e-6f) { wgt[4] = 1.0f; sum = 1.0f; }

    /* 五带基色 */
    float grass[3], forest[3], soil[3], sand[3], gravel[3];
    tone_mix(TONE_GRASS, pos, grass);
    tone_mix(TONE_FOREST, pos, forest);
    tone_mix(TONE_SOIL, pos, soil);
    tone_mix(TONE_SAND, pos, sand);
    tone_mix(TONE_GRAVEL, pos, gravel);

    /* 干旱向赭黄 */
    static const float DRYRGB[3] = { 179, 154, 108 };
    float dryk = smooth_step(0.55f, 0.22f, arid) * 0.55f;
    for (int k = 0; k < 3; k++) {
        grass[k] += (DRYRGB[k] - grass[k]) * dryk;
        soil[k] += (DRYRGB[k] - soil[k]) * dryk * 0.5f;
        sand[k] += (197.0f - sand[k]) * dryk * 0.25f;
    }
    /* 季节色：幅度逐点受斑驳调制 */
    if (sea >= 0) {
        float sq = qlcg_clampf(ampl * (0.70f + 0.55f * patch), 0.0f, 1.0f)
                 * SEASON_AMT[sea];
        const float *st = SEASON_RGB[sea];
        for (int k = 0; k < 3; k++) {
            grass[k] += (st[k] - grass[k]) * sq;
            soil[k] += (st[k] - soil[k]) * sq * 0.5f;
        }
    }
    /* 去荧光 */
    float lum = grass[0] * 0.299f + grass[1] * 0.587f + grass[2] * 0.114f;
    for (int k = 0; k < 3; k++) grass[k] += (lum - grass[k]) * 0.06f;

    static const float FIXED[2][3] = { { 127, 127, 127 }, { 243, 247, 250 } };
    float r = 0.0f, g = 0.0f, b = 0.0f, best = -1.0f;
    int best_i = 6;
    for (int i = 0; i < 7; i++) {
        float wi = wgt[i] / sum;
        const float *c;
        if (i == 0) c = FIXED[0];
        else if (i == 1) c = FIXED[1];
        else if (i == 2) c = sand;
        else if (i == 3) c = gravel;
        else if (i == 4) c = forest;
        else if (i == 5) c = soil;
        else c = grass;
        r += c[0] * wi;
        g += c[1] * wi;
        b += c[2] * wi;
        if (wgt[i] > best) { best = wgt[i]; best_i = i; }
    }

    /* 顶点色只管大尺度，细节靠片元噪声 */
    out[0] = (uint8_t)qlcg_clampi((int)lroundf(r), 0, 255);
    out[1] = (uint8_t)qlcg_clampi((int)lroundf(g), 0, 255);
    out[2] = (uint8_t)qlcg_clampi((int)lroundf(b), 0, 255);
    if (out_material) {
        /* 映射回编码材质：0 承托 1 岩 2 土 3 盐碱 4 沙 5 砾 */
        static const int MAP[7] = { 1, 1, 4, 5, 2, 2, 2 };
        *out_material = MAP[best_i];
    }
}
