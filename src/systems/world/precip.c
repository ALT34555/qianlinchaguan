/* 每日降水场：水汽累积、季节雨热与多级采样 */
#include "precip.h"
#include "climate_season.h"
#include "../../core/math_util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PRECIP_PI 3.14159265358979323846f

static void step_rows(PrecipField *f, int day, int y0, int y1);

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float smooth01(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static int wrapi(int v, int p) {
    if (p <= 0) return v;
    int r = v % p;
    return r < 0 ? r + p : r;
}

/* 128 噪声，period 环绕 */
static float texture_noise(uint32_t seed, float x, float y, int period) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fy * fy * (3.0f - 2.0f * fy);
    int x0 = wrapi(ix, period), x1 = wrapi(ix + 1, period);
    int y0 = wrapi(iy, period), y1 = wrapi(iy + 1, period);
    float a = qlcg_mixf(qlcg_hash2(x0, y0, seed), qlcg_hash2(x1, y0, seed), u);
    float b = qlcg_mixf(qlcg_hash2(x0, y1, seed), qlcg_hash2(x1, y1, seed), u);
    return qlcg_mixf(a, b, v);
}

static float cell_lat_deg(const PrecipField *f, int cell) {
    int row = cell / f->columns;
    return (0.5f - ((float)row + 0.5f) / (float)f->rows) * 180.0f;
}

/* 轨道季节相位 -1..1（无日期时取 0） */
float qlcg_precip_season_s(const CalendarSnapshot *c) {
    if (!c) return 0.0f;
    float phase = qlcg_climate_orbit_phase(c);
    return cosf((phase - 1.0f) * (PRECIP_PI * 0.5f));
}

static float hemi_of(float lat_deg) {
    float h = lat_deg >= 0.0f ? 1.0f : -1.0f;
    if (fabsf(lat_deg) < 5.0f) h *= 0.25f;
    return h;
}

float qlcg_precip_season_weight(float temp, float moisture, float season_amp,
                                float lat_deg, float season_s, float monsoon) {
    float m = clampf(moisture, 0.0f, 1.0f);
    float ts = clampf(temp + season_amp * season_s, -60.0f, 60.0f);
    float conv = m * m * clampf((ts - 3.0f) / 27.0f, 0.0f, 1.0f);
    float band = expf(-powf((fabsf(lat_deg) - 45.0f) / 20.0f, 2.0f));
    float wet = clampf(monsoon, 0.0f, 1.0f) * clampf(season_s, 0.0f, 1.0f);
    float front = band * (1.0f - 0.6f * m) * clampf((14.0f - ts) / 26.0f, 0.0f, 1.0f);
    front *= 1.0f - 0.5f * wet;
    return 0.45f + 1.30f * conv + 1.40f * front + 0.90f * wet * m * m;
}

float qlcg_precip_season_temp_s(const MacroWorld *w, int cell, float season_s) {
    if (!w || cell < 0 || cell >= w->cell_count || !w->temperature) return 15.0f;
    int row = cell / w->columns;
    float lat = (0.5f - ((float)row + 0.5f) / (float)w->rows) * 180.0f;
    float s = season_s * hemi_of(lat);
    float amp = w->season_amp ? w->season_amp[cell] : 8.0f;
    return clampf(w->temperature[cell] + amp * s, -60.0f, 60.0f);
}

float qlcg_precip_season_temp(const MacroWorld *w, int cell,
                              const CalendarSnapshot *clk) {
    return qlcg_precip_season_temp_s(w, cell, qlcg_precip_season_s(clk));
}

static void rebuild_levels(PrecipField *f) {
    for (int i = 1; i < f->level_count; i++) {
        free(f->levels[i].rain);
        free(f->levels[i].water);
    }
    memset(f->levels, 0, sizeof(f->levels));
    f->level_count = 1;
    f->levels[0].w = f->columns;
    f->levels[0].h = f->rows;
    f->levels[0].rain = f->rain;
    f->levels[0].water = f->water;
    int w = f->columns, h = f->rows;
    while (w > 4 && f->level_count < PRECIP_LEVEL_MAX) {
        int nw = w / 2, nh = h / 2;
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;
        PrecipLevel *dst = &f->levels[f->level_count];
        dst->w = nw;
        dst->h = nh;
        dst->rain = (float *)calloc((size_t)nw * nh, sizeof(float));
        dst->water = (float *)calloc((size_t)nw * nh, sizeof(float));
        if (!dst->rain || !dst->water) return;
        const PrecipLevel *src = &f->levels[f->level_count - 1];
        for (int y = 0; y < nh; y++) {
            int sy0 = y * 2, sy1 = y * 2 + 1;
            if (sy0 >= src->h) sy0 = src->h - 1;
            if (sy1 >= src->h) sy1 = src->h - 1;
            float wa = cosf((0.5f - ((float)sy0 + 0.5f) / (float)src->h) * PRECIP_PI);
            float wb = cosf((0.5f - ((float)sy1 + 0.5f) / (float)src->h) * PRECIP_PI);
            float wsum = wa + wb;
            if (wsum < 1e-6f) wsum = 1.0f;
            for (int x = 0; x < nw; x++) {
                int sx0 = x * 2, sx1 = x * 2 + 1;
                if (sx1 >= src->w) sx1 = src->w - 1;
                int i0 = sy0 * src->w + sx0, i1 = sy0 * src->w + sx1;
                int i2 = sy1 * src->w + sx0, i3 = sy1 * src->w + sx1;
                int di = y * nw + x;
                dst->rain[di] = ((src->rain[i0] + src->rain[i1]) * wa +
                                 (src->rain[i2] + src->rain[i3]) * wb) / (2.0f * wsum);
                dst->water[di] = ((src->water[i0] + src->water[i1]) * wa +
                                  (src->water[i2] + src->water[i3]) * wb) / (2.0f * wsum);
            }
        }
        f->level_count++;
        w = nw; h = nh;
    }
}

static void init_day(PrecipField *f, int day) {
    const MacroWorld *w = f->world;
    int period = f->columns / 8;
    if (period > 128) period = 128;
    if (period < 1) period = 1;
    uint32_t nseed = f->seed ^ 0x7261696eu;
    for (int y = 0; y < f->rows; y++) {
        for (int x = 0; x < f->columns; x++) {
            int i = y * f->columns + x;
            float r = w && w->rainfall ? w->rainfall[i] : 0.0f;
            f->water[i] = 0.0f;
            f->rain[i] = 0.0f;
            f->phase[i] = 0;
            if (r > 0.0f) {
                float n = texture_noise(nseed, ((float)x + 0.5f) / 8.0f + (float)day * 0.13f + 41.0f,
                                        ((float)y + 0.5f) / 8.0f + 17.0f, period);
                f->water[i] = PRECIP_THRESHOLD * n;
            }
        }
    }
    f->day = day;
    f->step_day = day + 1;
    f->step_row = 0;
    /* 首日先走一步，雷达与 3D 立刻有雨 */
    step_rows(f, day, 0, f->rows);
    f->ready = true;
    rebuild_levels(f);
    f->version++;
}

PrecipField *qlcg_precip_create(const MacroWorld *w) {
    if (!w || w->columns <= 0 || w->rows <= 0) return NULL;
    PrecipField *f = (PrecipField *)calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->world = w;
    f->columns = w->columns;
    f->rows = w->rows;
    f->cell_count = w->cell_count;
    f->seed = w->opt.seed;
    f->day = PRECIP_DAY_NONE;
    f->step_day = PRECIP_DAY_NONE;
    size_t n = (size_t)f->cell_count;
    f->water = (float *)calloc(n, sizeof(float));
    f->rain = (float *)calloc(n, sizeof(float));
    f->phase = (uint8_t *)calloc(n, sizeof(uint8_t));
    f->season_norm = (float *)calloc(n, sizeof(float));
    if (!f->water || !f->rain || !f->phase || !f->season_norm) {
        qlcg_precip_free(f);
        return NULL;
    }
    for (int i = 0; i < f->cell_count; i++) {
        float lat = cell_lat_deg(f, i);
        float h = hemi_of(lat);
        float norm = 0.0f;
        if (w->temperature && w->moisture) {
            float amp = w->season_amp ? w->season_amp[i] : 8.0f;
            float mon = w->monsoon_amp ? w->monsoon_amp[i] : 0.0f;
            for (int k = 0; k < 4; k++) {
                float s = cosf(((float)k + 0.5f - 1.0f) * (PRECIP_PI * 0.5f)) * h;
                norm += qlcg_precip_season_weight(w->temperature[i], w->moisture[i],
                                                  amp, lat, s, mon);
            }
            norm *= 0.25f;
        } else {
            norm = 1.0f;
        }
        f->season_norm[i] = norm > 0.2f ? norm : 0.2f;
    }
    return f;
}

void qlcg_precip_free(PrecipField *f) {
    if (!f) return;
    for (int i = 1; i < f->level_count; i++) {
        free(f->levels[i].rain);
        free(f->levels[i].water);
    }
    free(f->water);
    free(f->rain);
    free(f->phase);
    free(f->season_norm);
    free(f);
}

static void step_rows(PrecipField *f, int day, int y0, int y1) {
    const MacroWorld *w = f->world;
    if (!w || !w->rainfall || !w->temperature || !w->moisture) return;
    int period = f->columns / 8;
    if (period > 128) period = 128;
    if (period < 1) period = 1;
    float freq = (float)period / (float)f->columns;
    uint32_t nseed = f->seed ^ 0x7261696eu;
    float s0 = qlcg_precip_season_s(w->plant_calendar);
    for (int y = y0; y < y1; y++) {
        float lat = (0.5f - ((float)y + 0.5f) / (float)f->rows) * 180.0f;
        float hemi = hemi_of(lat);
        float s = s0 * hemi;
        for (int x = 0; x < f->columns; x++) {
            int i = y * f->columns + x;
            float a = w->rainfall[i] > 0.0f ? w->rainfall[i] / 365.0f : 0.0f;
            float wet = texture_noise(nseed,
                                      ((float)x + 0.5f) * freq - (float)day * 0.7f,
                                      ((float)y + 0.5f) * freq - (float)day * 0.23f,
                                      period);
            float factor = 1.0f;
            if (f->season_norm[i] > 0.05f) {
                float amp = w->season_amp ? w->season_amp[i] : 8.0f;
                float mon = w->monsoon_amp ? w->monsoon_amp[i] : 0.0f;
                factor = qlcg_precip_season_weight(w->temperature[i], w->moisture[i],
                                                   amp, lat, s, mon) / f->season_norm[i];
            }
            float input = wet > 0.5f ? a * 2.0f * factor : 0.0f;
            float water = f->water[i] + input;
            uint8_t phase = f->phase[i];
            /* 湿润处更快到达饱和，雨日更多 */
            float threshold = PRECIP_THRESHOLD * (0.55f + 0.45f * expf(-a / 3.0f));
            if (water >= threshold) phase = 1;
            float rain = 0.0f;
            if (phase) {
                float cap = fmaxf(4.0f, a * 4.0f) * clampf(factor, 0.5f, 2.0f);
                rain = fminf(water, cap);
            }
            water -= rain;
            if (water <= 1e-6f) phase = 0;
            f->water[i] = water;
            f->rain[i] = rain;
            f->phase[i] = phase;
        }
    }
}

void qlcg_precip_sync(MacroWorld *w, double unix_ms) {
    if (!w) return;
    if (!w->precip) w->precip = qlcg_precip_create(w);
    PrecipField *f = w->precip;
    if (!f) return;
    int target = (int)floor(unix_ms / 86400000.0);
    if (!f->ready || f->day == PRECIP_DAY_NONE || target < f->day || target - f->day > 60) {
        init_day(f, target);
        return;
    }
    if (target == f->day) return;
    if (f->step_day != f->day + 1) {
        f->step_day = f->day + 1;
        f->step_row = 0;
    }
    int budget = 64;
    int y0 = f->step_row;
    int y1 = y0 + budget;
    if (y1 > f->rows) y1 = f->rows;
    step_rows(f, f->step_day, y0, y1);
    f->step_row = y1;
    if (f->step_row >= f->rows) {
        f->day = f->step_day;
        f->step_day = f->day + 1;
        f->step_row = 0;
        rebuild_levels(f);
        f->version++;
    }
}

bool qlcg_precip_adopt(PrecipField *f, int day, const float *water,
                       const float *rain, const uint8_t *phase) {
    if (!f || !water || !rain || !phase) return false;
    memcpy(f->water, water, (size_t)f->cell_count * sizeof(float));
    memcpy(f->rain, rain, (size_t)f->cell_count * sizeof(float));
    memcpy(f->phase, phase, (size_t)f->cell_count);
    f->day = day;
    f->step_day = day + 1;
    f->step_row = 0;
    f->ready = true;
    rebuild_levels(f);
    f->version++;
    return true;
}

void qlcg_precip_flush_partial(MacroWorld *w) {
    if (!w || !w->precip || !w->precip->ready) return;
    PrecipField *f = w->precip;
    if (f->step_row <= 0 || f->step_day != f->day + 1) return;
    step_rows(f, f->step_day, f->step_row, f->rows);
    f->day = f->step_day;
    f->step_day = f->day + 1;
    f->step_row = 0;
    rebuild_levels(f);
    f->version++;
}

static float cloud_at(const PrecipField *f, float x, float y) {
    uint32_t seed = f->seed ^ 0x636c6f75u;
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fy * fy * (3.0f - 2.0f * fy);
    int x0 = wrapi(ix, 128), x1 = wrapi(ix + 1, 128);
    int y0 = wrapi(iy, 128), y1 = wrapi(iy + 1, 128);
    float a = qlcg_mixf(qlcg_hash2(x0, y0, seed), qlcg_hash2(x1, y0, seed), u);
    float b = qlcg_mixf(qlcg_hash2(x0, y1, seed), qlcg_hash2(x1, y1, seed), u);
    return qlcg_mixf(a, b, v);
}

static float cloud_mask(const PrecipField *f, double unix_ms, float lon, float lat) {
    const MacroWorld *w = f->world;
    float circ = w && w->circumference > 1.0f ? w->circumference : 1.0f;
    float wx = lon / (2.0f * PRECIP_PI) * circ;
    float wz = -lat / PRECIP_PI * circ;
    float days = (float)(unix_ms / 86400000.0);
    float scale = fmaxf(1.0f, roundf((float)f->columns * 3.0f / 128.0f)) * 128.0f / circ;
    float u = wx * scale - days * 1.7f;
    float v = wz * scale - days * 0.55f;
    float dx = (cloud_at(f, u * 0.5f + 41.0f, v * 0.5f + 17.0f) - 0.5f) * 1.6f;
    float dz = (cloud_at(f, u * 0.5f + 73.0f, v * 0.5f + 59.0f) - 0.5f) * 1.6f;
    float broad = cloud_at(f, u + dx, v + dz);
    float fine = cloud_at(f, wx * scale * 2.0f + dx + days * 0.8f + 37.0f,
                          wz * scale * 2.0f + dz - days * 0.7f + 71.0f);
    float pole = w && qlcg_world_flat(w) ? 1.0f : clampf(cosf(wz / circ * (2.0f * PRECIP_PI)) * 4.0f, 0.0f, 1.0f);
    return smooth01(0.40f, 0.65f, 0.5f + ((broad * 0.75f + fine * 0.25f) - 0.5f) * pole);
}

static void read_level(const PrecipLevel *L, bool flat, float lon, float lat,
                       float *rain, float *water) {
    float x = (lon / (2.0f * PRECIP_PI) + 0.5f) * (float)L->w - 0.5f;
    float y = (0.5f - lat / PRECIP_PI) * (float)L->h - 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - (float)x0, fy = y - (float)y0;
    int x1 = wrapi(x0 + 1, L->w);
    if(flat) { x1=qlcg_clampi(x0+1,0,L->w-1); x0=qlcg_clampi(x0,0,L->w-1); }
    else x0 = wrapi(x0, L->w);
    int y1 = y0 + 1;
    if (y0 < 0) y0 = 0;
    if (y0 >= L->h) y0 = L->h - 1;
    if (y1 < 0) y1 = 0;
    if (y1 >= L->h) y1 = L->h - 1;
    int i00 = y0 * L->w + x0, i10 = y0 * L->w + x1;
    int i01 = y1 * L->w + x0, i11 = y1 * L->w + x1;
    *rain = ((L->rain[i00] * (1.0f - fx) + L->rain[i10] * fx) * (1.0f - fy) +
             (L->rain[i01] * (1.0f - fx) + L->rain[i11] * fx) * fy);
    *water = ((L->water[i00] * (1.0f - fx) + L->water[i10] * fx) * (1.0f - fy) +
              (L->water[i01] * (1.0f - fx) + L->water[i11] * fx) * fy);
}

bool qlcg_precip_sample(const PrecipField *f, double unix_ms,
                        float lon, float lat, float footprint,
                        float season_temp, PrecipSample *out) {
    if (out) memset(out, 0, sizeof(*out));
    if (!f || !f->ready || !out || f->level_count <= 0) return false;
    float circ = f->world && f->world->circumference > 1.0f ? f->world->circumference : 1.0f;
    float span = circ / (float)f->columns;
    float lod = log2f(fmaxf(1.0f, footprint / span));
    if (lod < 0.0f) lod = 0.0f;
    if (lod > (float)(f->level_count - 1)) lod = (float)(f->level_count - 1);
    int lo = (int)lod;
    float blend = lod - (float)lo;
    float rain_a, water_a, rain_b, water_b;
    read_level(&f->levels[lo],qlcg_world_flat(f->world),lon,lat,&rain_a,&water_a);
    if (blend > 0.0f && lo + 1 < f->level_count) {
        read_level(&f->levels[lo + 1],qlcg_world_flat(f->world),lon,lat,&rain_b,&water_b);
        rain_a = rain_a * (1.0f - blend) + rain_b * blend;
        water_a = water_a * (1.0f - blend) + water_b * blend;
    }
    float local = cloud_mask(f, unix_ms, lon, lat);
    float average = smooth01(span / 6.0f, span * 1.5f, footprint);
    float mask = local * (1.0f - average) + 0.43f * average;
    float intensity = clampf(rain_a / PRECIP_THRESHOLD, 0.0f, 1.0f) * mask;
    if (intensity < 0.025f) intensity = 0.0f;
    float cloud = clampf(water_a / PRECIP_THRESHOLD * 0.55f + mask * 0.45f +
                         (rain_a > 0.0f ? 0.2f : 0.0f), 0.0f, 1.0f);
    int precip = 0;
    if (intensity > 0.0f) precip = season_temp > 2.0f ? 1 : (season_temp > 0.0f ? 2 : 3);
    int id;
    if (intensity <= 0.0f) {
        id = cloud < 0.18f ? 1 : cloud < 0.30f ? 4 : cloud < 0.40f ? 0
           : cloud < 0.50f ? 2 : 3;
    } else if (precip == 2) {
        id = 200;
    } else if (precip == 3) {
        id = intensity < 0.55f ? 201 : 202;
    } else if (intensity < 0.25f) {
        id = 100;
    } else if (intensity < 0.60f) {
        id = 101;
    } else if (intensity < 0.90f) {
        id = 102;
    } else {
        int ix = (int)floorf((lon / (2.0f * PRECIP_PI) + 0.5f) * (float)f->columns);
        int iy = (int)floorf((0.5f - lat / PRECIP_PI) * (float)f->rows);
        id = qlcg_hash2(ix, iy, f->seed) < 0.5f ? 103 : 104;
    }
    out->id = id;
    out->precipitation = precip;
    out->intensity = intensity;
    out->cloud_cover = cloud;
    out->rain_mm = rain_a;
    out->cloud_mm = water_a;
    out->temperature = season_temp;
    return true;
}
