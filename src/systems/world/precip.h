/* 每日降水场：宏观水汽、季节雨热与雷达采样 */
#ifndef QLCG_PRECIP_H
#define QLCG_PRECIP_H

#include "planet_macro.h"

#define PRECIP_THRESHOLD 12.0f
#define PRECIP_LEVEL_MAX 8
#define PRECIP_DAY_NONE   (-2147483647)

typedef struct {
    int    w, h;
    float *rain;
    float *water;
} PrecipLevel;

typedef struct PrecipField {
    const MacroWorld *world;
    int      columns, rows, cell_count;
    int      day;                 /* UTC 日序 */
    int      step_day;            /* 正在推进的日 */
    int      step_row;            /* 已完成行 */
    uint32_t seed;
    uint32_t version;
    float   *water;
    float   *rain;
    uint8_t *phase;
    float   *season_norm;
    int      level_count;
    PrecipLevel levels[PRECIP_LEVEL_MAX];
    bool     ready;
} PrecipField;

/* 降水采样结果 */
typedef struct {
    int   id;
    int   precipitation;   /* 0 无 1 雨 2 雨夹雪 3 雪 */
    float intensity;
    float cloud_cover;
    float rain_mm;
    float cloud_mm;
    float temperature;
} PrecipSample;

PrecipField *qlcg_precip_create(const MacroWorld *w);
void         qlcg_precip_free(PrecipField *f);
/* 按世界钟推进，每帧限行 */
void         qlcg_precip_sync(MacroWorld *w, double unix_ms);

bool qlcg_precip_sample(const PrecipField *f, double unix_ms,
                        float lon, float lat, float footprint,
                        float season_temp, PrecipSample *out);

/* 存档恢复：拷贝 f32 数组并重建金字塔 */
bool qlcg_precip_adopt(PrecipField *f, int day, const float *water,
                       const float *rain, const uint8_t *phase);
/* 存档前补齐半日 */
void qlcg_precip_flush_partial(MacroWorld *w);

/* 气候季节温度与季节权重（测试可直接调用） */
float qlcg_precip_season_s(const CalendarSnapshot *clk);
float qlcg_precip_season_temp_s(const MacroWorld *w, int cell, float season_s);
float qlcg_precip_season_temp(const MacroWorld *w, int cell, const CalendarSnapshot *clk);
float qlcg_precip_season_weight(float temp, float moisture, float season_amp,
                                float lat_deg, float season_s, float monsoon);

#endif
