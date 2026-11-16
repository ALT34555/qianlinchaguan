/* 气候区判定、存储与快速查询 */
#include "climate_zone.h"
#include "world_infinite.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 热量带阈值，与舆图气候层一致 */
#define ZONE_HEAT_HOT      23.0f
#define ZONE_HEAT_SUBTROP  15.0f
#define ZONE_HEAT_TEMP      4.0f
#define ZONE_HEAT_COLD     -5.0f
/* 冬季档阈值，按最冷月温 */
#define ZONE_WINTER_NONE   15.0f
#define ZONE_WINTER_MILD    8.0f
#define ZONE_WINTER_COLD    0.0f
#define ZONE_WINTER_SEVERE -15.0f
/* 湿润档阈值，按湿润度与年降水 */
#define ZONE_WET_DESERT     0.20f
#define ZONE_WET_SEMIARID   0.38f
#define ZONE_WET_SUBHUMID   0.58f
#define ZONE_RAIN_DESERT    250.0f
#define ZONE_RAIN_SEMIARID  500.0f
#define ZONE_RAIN_SUBHUMID 1000.0f
#define ZONE_MONSOON_STRONG 0.45f

static const QlcgClimateZoneInfo ZONE_INFO[QLCG_ZONE_COUNT] = {
    { "climate.zone.tropical_rainforest",  0, 0, 3, { 46, 110,  52 } },
    { "climate.zone.tropical_monsoon",     0, 0, 4, { 64, 128,  54 } },
    { "climate.zone.tropical_savanna",     0, 0, 2, {154, 158,  66 } },
    { "climate.zone.tropical_desert",      0, 0, 0, {214, 178, 110 } },
    { "climate.zone.subtropical_evergreen",1, 0, 3, { 74, 132,  64 } },
    { "climate.zone.subtropical_monsoon",  1, 0, 4, { 86, 146,  70 } },
    { "climate.zone.subtropical_deciduous",1, 2, 3, {128, 142,  72 } },
    { "climate.zone.subtropical_steppe",   1, 2, 2, {176, 168,  88 } },
    { "climate.zone.subtropical_desert",   1, 1, 0, {206, 176, 116 } },
    { "climate.zone.temperate_rainforest", 2, 1, 3, { 58, 118,  82 } },
    { "climate.zone.temperate_deciduous",  2, 2, 3, {104, 138,  76 } },
    { "climate.zone.temperate_monsoon",    2, 2, 4, { 92, 136,  74 } },
    { "climate.zone.temperate_steppe",     2, 2, 2, {178, 160,  86 } },
    { "climate.zone.temperate_desert",     2, 2, 0, {196, 172, 124 } },
    { "climate.zone.boreal_mixed",         2, 3, 3, { 72, 112,  96 } },
    { "climate.zone.taiga",                3, 3, 3, { 56,  96,  88 } },
    { "climate.zone.boreal_steppe",        3, 3, 2, {150, 142, 100 } },
    { "climate.zone.tundra",               3, 4, 2, {128, 138, 124 } },
    { "climate.zone.cold_desert",          3, 3, 0, {160, 156, 144 } },
    { "climate.zone.ice_cap",              4, 4, 1, {226, 236, 244 } }
};

const char *qlcg_climate_zone_key(int zone) {
    if (zone < 0 || zone >= QLCG_ZONE_COUNT) return "climate.zone.unknown";
    return ZONE_INFO[zone].key;
}

const QlcgClimateZoneInfo *qlcg_climate_zone_info(int zone) {
    if (zone < 0 || zone >= QLCG_ZONE_COUNT) return NULL;
    return &ZONE_INFO[zone];
}

/* 热量带：五档，与舆图一致 */
static int heat_band(float tmean) {
    if (!isfinite(tmean)) return -1;
    if (tmean > ZONE_HEAT_HOT) return 0;
    if (tmean > ZONE_HEAT_SUBTROP) return 1;
    if (tmean > ZONE_HEAT_TEMP) return 2;
    if (tmean > ZONE_HEAT_COLD) return 3;
    return 4;
}

/* 冬季档：按最冷月温 */
static int winter_band(float tmin) {
    if (!isfinite(tmin)) return -1;
    if (tmin > ZONE_WINTER_NONE) return 0;
    if (tmin > ZONE_WINTER_MILD) return 1;
    if (tmin > ZONE_WINTER_COLD) return 2;
    if (tmin > ZONE_WINTER_SEVERE) return 3;
    return 4;
}

/* 湿润档：湿润度与年降水取先命中者 */
static int wet_band(float moisture, float rainfall, float monsoon) {
    if (!isfinite(moisture) || !isfinite(rainfall)) return -1;
    if (moisture < ZONE_WET_DESERT || rainfall < ZONE_RAIN_DESERT) return 0;
    if (moisture < ZONE_WET_SEMIARID || rainfall < ZONE_RAIN_SEMIARID) return 1;
    if (moisture < ZONE_WET_SUBHUMID || rainfall < ZONE_RAIN_SUBHUMID) return 2;
    if (isfinite(monsoon) && monsoon > ZONE_MONSOON_STRONG) return 4;
    return 3;
}

/* 三维档位到区的映射，覆盖全部合法组合 */
static int zone_lookup(int heat, int winter, int wet) {
    switch (heat) {
        case 0:
            if (wet == 0) return QLCG_ZONE_TROPICAL_DESERT;
            if (wet == 1 || wet == 2) return QLCG_ZONE_TROPICAL_SAVANNA;
            if (wet == 4) return QLCG_ZONE_TROPICAL_MONSOON;
            return QLCG_ZONE_TROPICAL_RAINFOREST;
        case 1:
            if (wet == 0) return QLCG_ZONE_SUBTROPICAL_DESERT;
            if (wet == 1) return QLCG_ZONE_SUBTROPICAL_STEPPE;
            if (wet == 2) return QLCG_ZONE_SUBTROPICAL_STEPPE;
            if (wet == 4) return QLCG_ZONE_SUBTROPICAL_MONSOON;
            return winter >= 2 ? QLCG_ZONE_SUBTROPICAL_DECIDUOUS
                               : QLCG_ZONE_SUBTROPICAL_EVERGREEN;
        case 2:
            if (wet == 0) return QLCG_ZONE_TEMPERATE_DESERT;
            if (wet == 1 || wet == 2) return QLCG_ZONE_TEMPERATE_STEPPE;
            if (wet == 4) return QLCG_ZONE_TEMPERATE_MONSOON;
            if (winter >= 3) return QLCG_ZONE_BOREAL_MIXED;
            return winter <= 1 ? QLCG_ZONE_TEMPERATE_RAINFOREST
                               : QLCG_ZONE_TEMPERATE_DECIDUOUS;
        case 3:
            if (wet == 0) return QLCG_ZONE_COLD_DESERT;
            if (wet == 1 || wet == 2)
                return winter >= 4 ? QLCG_ZONE_TUNDRA : QLCG_ZONE_BOREAL_STEPPE;
            return winter >= 4 ? QLCG_ZONE_TUNDRA : QLCG_ZONE_TAIGA;
        default:
            return QLCG_ZONE_ICE_CAP;
    }
}

int qlcg_climate_zone_of(float tmean, float amp, float moisture,
                         float rainfall, float monsoon) {
    int heat = heat_band(tmean);
    if (heat < 0) return QLCG_ZONE_UNKNOWN;
    float a = isfinite(amp) ? fabsf(amp) : 0.0f;
    int winter = winter_band(tmean - a);
    int wet = wet_band(moisture, rainfall, monsoon);
    if (winter < 0 || wet < 0) return QLCG_ZONE_UNKNOWN;
    return zone_lookup(heat, winter, wet);
}

void qlcg_climate_zone_build(MacroWorld *w) {
    if (!w || w->columns <= 0 || w->rows <= 0) return;
    size_t n = (size_t)w->cell_count;
    if (!n) return;
    if (!w->climate_zone) {
        w->climate_zone = (uint8_t *)malloc(n);
        if (!w->climate_zone) return;
    }
    for (int i = 0; i < w->cell_count; i++) {
        w->climate_zone[i] = (uint8_t)qlcg_climate_zone_of(
            w->temperature ? w->temperature[i] : 15.0f,
            w->season_amp ? w->season_amp[i] : 0.0f,
            w->moisture ? w->moisture[i] : 0.5f,
            w->rainfall ? w->rainfall[i] : 800.0f,
            w->monsoon_amp ? w->monsoon_amp[i] : 0.0f);
    }
}

void qlcg_climate_zone_release(MacroWorld *w) {
    if (!w || !w->climate_zone) return;
    free(w->climate_zone);
    w->climate_zone = NULL;
}

/* 查询前确保数组存在，惰性构建 */
static void zone_ensure(const MacroWorld *w) {
    if (w && !w->climate_zone && w->columns > 0 && w->rows > 0)
        qlcg_climate_zone_build((MacroWorld *)w);
}

int qlcg_climate_zone_cell(const MacroWorld *w, int cell) {
    if (!w || cell < 0 || cell >= w->cell_count) return QLCG_ZONE_UNKNOWN;
    zone_ensure(w);
    if (!w->climate_zone) return QLCG_ZONE_UNKNOWN;
    int zone = w->climate_zone[cell];
    return zone < QLCG_ZONE_COUNT ? zone : QLCG_ZONE_UNKNOWN;
}

int qlcg_climate_zone_at(const MacroWorld *w, float wx, float wz) {
    if (!w || !isfinite(wx) || !isfinite(wz)) return QLCG_ZONE_UNKNOWN;
    return qlcg_climate_zone_cell(w, qlcg_world_cell_at_wxz(w, wx, wz));
}

int qlcg_climate_zone_chunk(const MacroWorld *w, int cx, int cz) {
    if (!w) return QLCG_ZONE_UNKNOWN;
    return qlcg_climate_zone_at(w, ((float)cx + 0.5f) * (float)CHUNK_SIZE,
                                   ((float)cz + 0.5f) * (float)CHUNK_SIZE);
}

int qlcg_climate_zone_world(const MacroWorld *w, double wx, double wz) {
    if (!w || !isfinite(wx) || !isfinite(wz)) return QLCG_ZONE_UNKNOWN;
    double lx = wx, lz = wz;
    MacroWorld *tile = qlcg_infinite_resolve(w, wx, wz, &lx, &lz);
    if (!tile) return QLCG_ZONE_UNKNOWN;
    return qlcg_climate_zone_at(tile, (float)lx, (float)lz);
}

void qlcg_climate_zone_row(const MacroWorld *w, int row, uint8_t *out) {
    if (!w || !out || row < 0 || row >= w->rows) return;
    zone_ensure(w);
    if (!w->climate_zone) {
        memset(out, QLCG_ZONE_UNKNOWN, (size_t)w->columns);
        return;
    }
    memcpy(out, w->climate_zone + (size_t)row * (size_t)w->columns, (size_t)w->columns);
}
