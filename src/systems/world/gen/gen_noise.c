/* 值噪声同 base；勿混用 qlcg_noise3 */
#include "gen_noise.h"
#include <math.h>

static float smooth01(float v) {
    return v * v * (3.0f - 2.0f * v);
}

float qlcg_base_hash(int x, int y, int z, uint32_t seed) {
    uint32_t n = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u)
               ^ ((uint32_t)z * 83492791u) ^ seed;
    n = (n ^ (n >> 16)) * 0x7feb352du;
    n = (n ^ (n >> 15)) * 0x846ca68bu;
    return (float)((double)(n ^ (n >> 16)) / 2147483648.0) - 1.0f;
}

static float mixf(float a, float b, float t) {
    return a + (b - a) * t;
}

float qlcg_base_noise(float x, float y, float z, uint32_t seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float tx = smooth01(x - (float)ix);
    float ty = smooth01(y - (float)iy);
    float tz = smooth01(z - (float)iz);
    float h000 = qlcg_base_hash(ix, iy, iz, seed);
    float h100 = qlcg_base_hash(ix + 1, iy, iz, seed);
    float h010 = qlcg_base_hash(ix, iy + 1, iz, seed);
    float h110 = qlcg_base_hash(ix + 1, iy + 1, iz, seed);
    float h001 = qlcg_base_hash(ix, iy, iz + 1, seed);
    float h101 = qlcg_base_hash(ix + 1, iy, iz + 1, seed);
    float h011 = qlcg_base_hash(ix, iy + 1, iz + 1, seed);
    float h111 = qlcg_base_hash(ix + 1, iy + 1, iz + 1, seed);
    float x00 = mixf(h000, h100, tx);
    float x10 = mixf(h010, h110, tx);
    float x01 = mixf(h001, h101, tx);
    float x11 = mixf(h011, h111, tx);
    float y0 = mixf(x00, x10, ty);
    float y1 = mixf(x01, x11, ty);
    return mixf(y0, y1, tz);
}

static double hash_d(int x,int y,int z,uint32_t seed) {
    uint32_t n=(uint32_t)x*73856093u^(uint32_t)y*19349663u^(uint32_t)z*83492791u^seed;
    n=(n^(n>>16))*0x7feb352du; n=(n^(n>>15))*0x846ca68bu;
    return (n^(n>>16))/2147483648.0-1;
}
static double mix_d(double a,double b,double t) { return a+(b-a)*t; }
double qlcg_base_noise_d(double x,double y,double z,uint32_t seed) {
    int ix=(int)floor(x),iy=(int)floor(y),iz=(int)floor(z);
    double tx=x-ix,ty=y-iy,tz=z-iz;
    tx=tx*tx*(3-2*tx); ty=ty*ty*(3-2*ty); tz=tz*tz*(3-2*tz);
    double a=mix_d(mix_d(hash_d(ix,iy,iz,seed),hash_d(ix+1,iy,iz,seed),tx),
                   mix_d(hash_d(ix,iy+1,iz,seed),hash_d(ix+1,iy+1,iz,seed),tx),ty);
    double b=mix_d(mix_d(hash_d(ix,iy,iz+1,seed),hash_d(ix+1,iy,iz+1,seed),tx),
                   mix_d(hash_d(ix,iy+1,iz+1,seed),hash_d(ix+1,iy+1,iz+1,seed),tx),ty);
    return mix_d(a,b,tz);
}
