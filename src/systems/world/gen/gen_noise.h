/* base 风格值噪声，对齐 backend.mjs */
#ifndef QLCG_GEN_NOISE_H
#define QLCG_GEN_NOISE_H

#include <stdint.h>

/* 同 base hash()，逐位一致，返回 -1..1 */
float qlcg_base_hash(int x, int y, int z, uint32_t seed);

/* 同 base noise()，三次平滑插值 */
float qlcg_base_noise(float x, float y, float z, uint32_t seed);
double qlcg_base_noise_d(double x,double y,double z,uint32_t seed);

#endif
