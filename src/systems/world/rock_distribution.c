#include "rock_distribution.h"
#include "model/rock_def.h"
#include "../../core/math_util.h"

/* 基质与已有气候量估算风化 */
RockDistribution qlcg_rock_distribution(int material, float wetness, float desert,
                                        float temperature, float patch) {
    static const float abundance[5] = {.34f, .13f, .055f, .018f, .44f};
    static const float weathering[5] = {.12f, .50f, .68f, .90f, .78f};
    if (material < 1 || material > 5) material = 2;
    float wet = qlcg_clampf(wetness, 0, 1);
    float dry = qlcg_clampf(desert, 0, 1);
    float cold = qlcg_clampf((8.0f - temperature) / 24.0f, 0, 1);
    float wear = qlcg_clampf(weathering[material - 1] + .16f * wet + .12f * dry +
                             .12f * cold + .08f * (patch - 1.0f), 0, 1);
    float chance = abundance[material - 1] * patch * (.8f + .4f * wear);
    if (material == 4) chance *= 1.0f - .8f * dry;
    return (RockDistribution){qlcg_clampf(chance, 0, 1), wear};
}

int qlcg_rock_distribution_size(float weathering, uint32_t roll) {
    float wear = qlcg_clampf(weathering, 0, 1);
    float pick = (roll >> 8) / 16777216.0f;
    float small = .25f + .65f * wear;
    float medium = .45f - .37f * wear;
    float large = .25f - .23f * wear;
    if (pick < small) return ROCK_SIZE_SMALL;
    if (pick < small + medium) return ROCK_SIZE_MEDIUM;
    if (pick < small + medium + large) return ROCK_SIZE_LARGE;
    return ROCK_SIZE_GIANT;
}

float qlcg_rock_slope_factor(float slope) {
    if (!isfinite(slope) || slope < 0) return 0;
    return 1.0f - qlcg_smoothf(.35f, .80f, slope);
}
