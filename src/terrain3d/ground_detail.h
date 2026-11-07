/* 地面材质纹理：按气候带的可平铺贴图，运行时只读 */
#ifndef QLCG_GROUND_DETAIL_H
#define QLCG_GROUND_DETAIL_H

#include <stdint.h>

#define QLCG_GROUND_TEX_PX    1024
#define QLCG_GROUND_TEX_SPAN  256.0f
#define QLCG_GROUND_BAND_N    5

/* 生成可平铺材质；rgb 需 px*px*3，span 米 */
void qlcg_ground_tex_build(uint8_t *rgb, int px, float span, int band, uint32_t seed);

/* 单点材质色（舆图瓦片按世界坐标采样） */
void qlcg_ground_tex_at(float wx, float wz, uint32_t seed, int band, float out[3]);

#endif
