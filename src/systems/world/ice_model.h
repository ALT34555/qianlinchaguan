/* 纯数值冻融模型，不查询世界、不调 GL */
#ifndef QLCG_ICE_MODEL_H
#define QLCG_ICE_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include "../calendar/calendar.h"

/* 水体种类：与 SurfaceWaterKind 同值 */
enum {
    ICE_KIND_DRY = 0, ICE_KIND_SEA = 1, ICE_KIND_LAKE = 2,
    ICE_KIND_RIVER = 3, ICE_KIND_FALL = 4, ICE_KIND_STATIC = 5
};

/* 冰面分级（由连续 H/Q 导出，不反向决定承重） */
enum {
    ICE_STAGE_NONE = 0, ICE_STAGE_FILM, ICE_STAGE_THIN,
    ICE_STAGE_WALKABLE, ICE_STAGE_THICK, ICE_STAGE_ROTTEN
};

#define QLCG_ICE_PERIOD       360.0   /* 一个气候周期的“气候日”数 */
#define QLCG_ICE_PER_SEASON    90.0
#define QLCG_ICE_SEA_POINT     (-1.8f)
/* 冰缘水深带：浅无冰，深满覆盖 */
#define QLCG_ICE_EDGE_LO       0.020f
#define QLCG_ICE_EDGE_HI       0.140f

/* 统一气候时刻：全世界共用 UTC0 快照 */
typedef struct {
    double   climate_day;   /* 360*周期年 + 90*相位 */
    float    phase;         /* 0..4 季内相位 */
    uint32_t epoch;         /* 回拨/切历法时递增，重锚孔洞年龄 */
    bool     forced;        /* 调试强制季节 */
} IceTime;

/* 一次已有地表采样的冰输入 */
typedef struct {
    float annual_temp;      /* 年均温，已含宏观高度修正 */
    float latitude;         /* 弧度 */
    float bed, top;         /* 水面下地形、显示水面高 */
    float depth;
    float raw_speed;        /* 未按冻态缩放的流速 m/s */
    float lateral;          /* 距河心 0..1 */
    float bank_distance;    /* 距河岸米 */
    float shore_weight;     /* 近岸权重 0..1 */
    float noise;            /* 固定空间噪声 0..1 */
    float grade;            /* 坡降 */
    uint8_t kind;
} IceSite;

typedef struct {
    float thickness, quality, coverage, strength;
    float top, bottom, air_temp, effective_temp;
    double cold_degree_days, warm_degree_days;
    uint8_t stage;
} IceSample;

/* 模型派生参数：与时间无关，可缓存 */
typedef struct {
    double mean, amp, shift;    /* Teff=mean+amp·sinφ */
    double k2, km, debt, cap;
    float  annual, air_amp;     /* 年均温与未滞后气温振幅 */
    float  noise;
    float  u;
} IceParams;

void        qlcg_ice_params(const IceSite *site, IceParams *out);
IceTime     qlcg_ice_time(const CalendarClock *clk, const CalendarSnapshot *snap,
                          int forced_season);
/* 由派生参数直接求样：避免每帧重算 sqrt/atan */
void        qlcg_ice_sample_params(const IceParams *p, const IceSite *site,
                                   double climate_day, IceSample *out);
void        qlcg_ice_evaluate(const IceSite *site, const IceTime *time, IceSample *out);
/* 覆盖率唯一实现：厚度 × 水深，模型与运行时共用 */
float       qlcg_ice_coverage(float thickness, float depth);
double      qlcg_ice_integral(const IceSite *site, double t0, double t1, bool cold);
float       qlcg_ice_load_capacity(float h, float q, float noise);
double      qlcg_ice_regrow(const IceSite *site, double broken_day, double now_day,
                            bool occupied, double *out_quality);
uint8_t     qlcg_ice_stage(float h, float q);
const char *qlcg_ice_stage_name(uint8_t stage);

#endif /* QLCG_ICE_MODEL_H */
