#ifndef QLCG_ROCK_DISTRIBUTION_H
#define QLCG_ROCK_DISTRIBUTION_H

#include <stdint.h>

typedef struct {
    float chance;
    float weathering;
} RockDistribution;

RockDistribution qlcg_rock_distribution(int material, float wetness, float desert,
                                        float temperature, float patch);
int qlcg_rock_distribution_size(float weathering, uint32_t roll);
float qlcg_rock_slope_factor(float slope);

#endif
