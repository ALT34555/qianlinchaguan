#ifndef QLCG_CLIMATE_SEASON_H
#define QLCG_CLIMATE_SEASON_H

#include "planet_macro.h"

/* 本地气候季：不读历法季，只读轨道相位与本地气候 */
typedef struct {
    float phase;        /* 本地相位 0..4 */
    float temperature;  /* 本地季节温度 */
    float moisture;     /* 本地季节湿度 0..1 */
    float amplitude;    /* 本地季节振幅 */
    float monsoon;      /* 本地季风强度 0..1 */
    int   season;       /* 0春 1夏 2秋 3冬 */
    int   seasonless;   /* 全年无换季边界 */
    float since_days;   /* 距本季起点 */
    float until_days;   /* 距下季起点 */
} QlcgClimateSeason;

/* 黄赤交角倍率；0° 返回 0 */
float qlcg_climate_amp_scale(float tilt_deg);
/* 太阳视黄经折算 0..4；不读 CalendarSnapshot.season_* */
float qlcg_climate_orbit_phase(const CalendarSnapshot *date);
/* 求单格本地气候季；date 为空时回退常驻夏 */
void  qlcg_climate_season_at(const MacroWorld *w, int cell,
                             const CalendarSnapshot *date, QlcgClimateSeason *out);
int   qlcg_climate_season_index(const MacroWorld *w, int cell,
                                const CalendarSnapshot *date);

#endif
