/* 地表配色，对齐 surface-cover.mjs */
#ifndef QLCG_GEN_COVER_H
#define QLCG_GEN_COVER_H

#include "../planet_macro.h"
#include <stdint.h>

/* 七种基础材质的权重 */
typedef struct {
    float stone, snow, sand, gravel, forest_soil, dirt, grass;
} CoverWeights;

/* 地面季节上下文；season 越界按年度色 */
typedef struct {
    int   season;     /* 0 春 1 夏 2 秋 3 冬 */
    float latitude;   /* 弧度，南负北正 */
} CoverSeason;

/* 按温度、干燥度、抬升与坡度求权重 */
void qlcg_cover_weights(float temperature, float aridity, float uplift, float slope,
                        CoverWeights *out);

/* 连续气候带位 0..4（0 冰封 1 寒 2 温 3 亚热 4 热） */
float qlcg_cover_band_of_temp(float temperature);

/* 湿润度 0..1（干 0 湿 1），供片元噪声选频率 */
float qlcg_cover_wet(float aridity);

/* 球面点地表色；season、material 可空 */
void qlcg_cover_color(const MacroWorld *w, float px, float py, float pz,
                      float temperature, float aridity, float uplift, float slope,
                      const CoverSeason *season, uint8_t out[3], int *out_material);

#endif
