/* 色带：同 base ramp()，线性插值 256 级 */
#include "gen_palette.h"
#include "../../../core/math_util.h"
#include <math.h>
#include <string.h>

typedef struct { float t; uint8_t c[3]; } RampStop;

static uint8_t g_land[768];
static uint8_t g_water[768];
static int g_ready = 0;

static void build_ramp(const RampStop *stops, int n, uint8_t *out) {
    for (int i = 0; i < 256; i++) {
        float t = (float)i / 255.0f;
        int k = 0;
        while (k < n - 2 && t > stops[k + 1].t) k++;
        float p0 = stops[k].t, p1 = stops[k + 1].t;
        float q = qlcg_clampf((t - p0) / ((p1 - p0) != 0.0f ? (p1 - p0) : 1.0f), 0.0f, 1.0f);
        for (int j = 0; j < 3; j++) {
            float v = (float)stops[k].c[j] + ((float)stops[k + 1].c[j] - (float)stops[k].c[j]) * q;
            out[i * 3 + j] = (uint8_t)qlcg_clampi((int)lroundf(v), 0, 255);
        }
    }
}

static void ensure_ready(void) {
    if (g_ready) return;
    static const RampStop LAND[] = {
        { 0.00f, { 226, 231, 214 } }, { 0.18f, { 199, 209, 178 } },
        { 0.36f, { 166, 183, 140 } }, { 0.52f, { 146, 163, 124 } },
        { 0.68f, { 178, 180, 134 } }, { 0.80f, { 194, 174, 124 } },
        { 0.88f, { 180, 150, 110 } }, { 0.95f, { 166, 152, 140 } },
        { 1.00f, { 226, 226, 218 } }
    };
    static const RampStop WATER[] = {
        { 0.00f, { 40, 80, 132 } }, { 0.16f, { 46, 102, 152 } },
        { 0.34f, { 52, 124, 164 } }, { 0.52f, { 60, 146, 176 } },
        { 0.72f, { 76, 168, 182 } }, { 1.00f, { 116, 198, 198 } }
    };
    build_ramp(LAND, (int)(sizeof(LAND) / sizeof(LAND[0])), g_land);
    build_ramp(WATER, (int)(sizeof(WATER) / sizeof(WATER[0])), g_water);
    g_ready = 1;
}

int qlcg_elevation_ramp(float height) {
    if (!(height > 0.0f)) return 0;
    float t = log1pf(height / 120.0f) / log1pf(3400.0f / 120.0f);
    int v = (int)lroundf(t * 255.0f);
    if (v < 1) v = 1;
    if (v > 255) v = 255;
    return v;
}

int qlcg_snow_value(float temperature) {
    float a = qlcg_clampf((2.0f - temperature) / 8.0f, 0.0f, 1.0f);
    return (int)lroundf(a * 255.0f);
}

int qlcg_depth_value(float level, float height, float full) {
    if (full <= 0.0f) return 0;
    float a = qlcg_clampf((level - height) / full, 0.0f, 1.0f);
    return (int)lroundf(a * 255.0f);
}

void qlcg_land_color(int ramp_value, int snow, uint8_t out[3]) {
    ensure_ready();
    int o = (ramp_value < 0 ? 0 : (ramp_value > 255 ? 255 : ramp_value)) * 3;
    float s = (float)snow / 255.0f;
    if (s > 0.0f) {
        /* 积雪色 [247,251,252] 按比例混入 */
        static const float SNOWC[3] = { 247.0f, 251.0f, 252.0f };
        for (int j = 0; j < 3; j++) {
            float v = (float)g_land[o + j] + (SNOWC[j] - (float)g_land[o + j]) * s;
            out[j] = (uint8_t)qlcg_clampi((int)lroundf(v), 0, 255);
        }
        return;
    }
    out[0] = g_land[o];
    out[1] = g_land[o + 1];
    out[2] = g_land[o + 2];
}

void qlcg_water_color(int depth_value, uint8_t out[3]) {
    ensure_ready();
    int o = (depth_value < 0 ? 0 : (depth_value > 255 ? 255 : depth_value)) * 3;
    out[0] = g_water[o];
    out[1] = g_water[o + 1];
    out[2] = g_water[o + 2];
}

/* 卫星图：气候色与材质色按温度/雨量混合 */
static const float SAT_CLIMATE[6][3] = {
    {  20,  74, 116 }, { 176, 216, 232 }, { 107, 142, 153 },
    {  52,  86,  62 }, {  52,  86,  62 }, { 140, 158,  84 }
};
static const float SAT_MATERIAL[6][3] = {
    { 185, 180, 171 }, { 129, 119, 111 }, {  94,  83,  20 },
    { 178, 187, 190 }, { 244, 221, 174 }, { 122, 115, 116 }
};

static float sat_smooth(float a, float b, float v) {
    float t = qlcg_clampf((v - a) / ((b - a) != 0.0f ? (b - a) : 1.0f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float sat_rain_weight(float rainfall) {
    static const float STOPS[5] = { 200.0f, 400.0f, 800.0f, 1200.0f, 1600.0f };
    static const float VALS[5]  = { 0.10f, 0.30f, 0.55f, 0.75f, 0.90f };
    float a = 0.10f;
    for (int k = 1; k < 5; k++) {
        if (rainfall >= STOPS[k]) { a = VALS[k]; continue; }
        if (rainfall > STOPS[k - 1]) {
            a = VALS[k - 1] + (VALS[k] - VALS[k - 1]) * sat_smooth(STOPS[k - 1], STOPS[k], rainfall);
        }
        break;
    }
    return a;
}

void qlcg_satellite_color(float temperature, float rainfall, float evaporation,
                          float relief, float slope, float relative, uint8_t out[3]) {
    float aridity = rainfall / fmaxf(150.0f, evaporation);
    /* 与 cover 同一套权重（材质 1..5） */
    float exposure = 0.28f + 0.0f;
    float snow = 1.0f - sat_smooth(-3.0f, 1.0f, temperature);
    float rock = (1.0f - snow) * sat_smooth(0.48f, 0.72f, exposure + slope * 0.7f
                 + qlcg_clampf((relief - 3.0f) / 20.0f, 0.0f, 1.0f) * 0.18f
                 + qlcg_clampf(relative / 3.0f, 0.0f, 1.0f) * 0.05f);
    float rest = 1.0f - snow - rock;
    float sand = rest * (1.0f - sat_smooth(0.25f, 0.55f, aridity));
    float gravel = (rest - sand) * sat_smooth(0.25f, 0.55f, exposure + slope * 0.7f)
                   * (1.0f - sat_smooth(0.65f, 1.1f, aridity));
    float soil = rest - sand - gravel;
    float values[6] = { 0.0f, rock, soil, snow, sand, gravel };

    float clim[3] = { SAT_CLIMATE[1][0], SAT_CLIMATE[1][1], SAT_CLIMATE[1][2] };
    static const float LIMITS[4] = { -8.0f, 8.0f, 18.0f, 24.0f };
    for (int i = 0; i < 4; i++) {
        float blend = sat_smooth(LIMITS[i] - 2.0f, LIMITS[i] + 2.0f, temperature);
        for (int k = 0; k < 3; k++) clim[k] += (SAT_CLIMATE[i + 2][k] - clim[k]) * blend;
    }
    float cold = 1.0f - sat_smooth(-10.0f, -6.0f, temperature);
    float rain = sat_rain_weight(rainfall);

    float acc[3] = { 0.0f, 0.0f, 0.0f };
    for (int m = 1; m <= 5; m++) {
        float warm = (m == 2) ? rain : fminf(0.6f, rain);
        float frozen = (m == 2) ? fmaxf(0.5f, rain) : fminf(0.6f, fmaxf(0.5f, rain));
        float alpha = warm + (frozen - warm) * cold;
        for (int k = 0; k < 3; k++) {
            acc[k] += values[m] * (clim[k] * alpha + SAT_MATERIAL[m][k] * (1.0f - alpha));
        }
    }
    for (int k = 0; k < 3; k++) {
        out[k] = (uint8_t)qlcg_clampi((int)lroundf(acc[k]), 0, 255);
    }
}
