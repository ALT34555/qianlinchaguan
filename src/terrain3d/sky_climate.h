/* 天空气候视觉，对齐 base Sky */
#ifndef QLCG_SKY_CLIMATE_H
#define QLCG_SKY_CLIMATE_H

#include "../core/config.h"
#include "../systems/world/astronomy.h"
#include "../systems/calendar/calendar.h"
#include "../systems/world/chunk_types.h"

typedef struct {
    float sun_dir[3], moon_dir[3];
    float horizon[3], zenith[3], glow[3], cloud[3], tint[3];
    float rain_cloud[3];  /* 雨云内壳色 */
    float glow_amt;
    float star;
    float moon;           /* 月亮圆盘可见度 0..1 */
    float cloud_cov;
    float rain_alpha;     /* 雨云内壳不透明度 */
    float dim;
    float flash;
    float daylight, dusk, night;
    float light_scale;   /* 地形/植被/水面的天气衰减 */
} SkyClimate;

/* precip_override<0 时取天气降水 */
void qlcg_sky_climate(const CalendarClock *clk, int utc_offset_min, int season,
                      const WeatherProfile *wp, bool atmosphere, float sim_time,
                      float precip_override, SkyClimate *out);

void qlcg_sky_climate_astra(const CalendarClock *clk, int utc_offset_min, int season,
    const WeatherProfile *wp, bool atmosphere, float sim_time, float precip_override,
    const AstraFrame *astra, SkyClimate *out);

/* 线性雾距 near/far 米，对齐 base */
void qlcg_fog_range(float view_m, const WeatherProfile *wp, bool atmosphere,
                    float *near_m, float *far_m);

#endif
