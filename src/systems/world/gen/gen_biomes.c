/* 生物群系：按温度、降水、湿度与水体类型分九类 */
#include "gen_biomes.h"
#include <stdlib.h>

/* 用最冷/最暖月温度分带，交角才会改气候带 */
static float month_temp(float mean, float amp, int warm) {
    return warm ? mean + amp : mean - amp;
}

void qlcg_build_biomes(MacroWorld *w) {
    if (!w || !w->biome) return;
    int n = w->cell_count;
    for (int i = 0; i < n; i++) {
        float amp = w->season_amp ? w->season_amp[i] : 0.0f;
        float cold = month_temp(w->temperature[i], amp, 0);
        float warm = month_temp(w->temperature[i], amp, 1);
        int b;
        if (w->height[i] <= 0.0f)            b = 0;
        else if (w->water[i] == 2)           b = 1;
        else if (cold < -8.0f)               b = 2;
        else if (w->rainfall[i] < 250.0f)    b = 3;
        else if (warm > 23.0f && w->rainfall[i] > 1400.0f) b = 4;
        else if (warm > 18.0f)               b = 5;
        else if (w->moisture[i] > 0.65f)     b = 6;
        else if (cold < 7.0f)                b = 7;
        else                                 b = 8;
        w->biome[i] = (uint8_t)b;
    }
}
