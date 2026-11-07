/* 舆图配色（对齐 relief.mjs 的高程色带） */
#ifndef QLCG_GEN_PALETTE_H
#define QLCG_GEN_PALETTE_H

#include <stdint.h>

/* 高程对数归一，返回 0..255 */
int   qlcg_elevation_ramp(float height);

/* 气温→积雪 0..255；2℃ 无雪，−6℃ 全雪 */
int   qlcg_snow_value(float temperature);

/* 水深归一，返回 0..255 */
int   qlcg_depth_value(float level, float height, float full);

/* 陆地色：色带 + 积雪渐隐，out 为 RGB */
void  qlcg_land_color(int ramp_value, int snow, uint8_t out[3]);

/* 水色：按深度取色 */
void  qlcg_water_color(int depth_value, uint8_t out[3]);

/* 卫星图连续色，对齐 satellite.mjs */
void  qlcg_satellite_color(float temperature, float rainfall, float evaporation,
                           float relief, float slope, float relative, uint8_t out[3]);

#endif
