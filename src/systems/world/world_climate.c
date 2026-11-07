/* planet_climate */
#include "planet_macro.h"
#include "world_internal.h"
#include "gen/gen_climate.h"
#include "gen/gen_hydrology.h"
#include "gen/gen_water.h"
#include "gen/gen_river.h"
#include "../../core/math_util.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void calibrate_sea_level(MacroWorld *w) {
    int N = w->cell_count;
    float min_h = 1e9f, max_h = -1e9f;
    for (int i = 0; i < N; ++i) {
        if (w->height[i] < min_h) min_h = w->height[i];
        if (w->height[i] > max_h) max_h = w->height[i];
    }
    float span = fmaxf(1.0f, max_h - min_h);
    float bins[4096];
    memset(bins, 0, sizeof(bins));
    float total_area = 0.0f;
    for (int i = 0; i < N; ++i) {
        int b = qlcg_clampi((int)(((w->height[i] - min_h) / span) * 4095.0f), 0, 4095);
        bins[b] += w->areas[i];
        total_area += w->areas[i];
    }
    float target_ocean = total_area * (1.0f - qlcg_clampf(w->opt.land_ratio, 0.05f, 0.92f));
    float acc = 0.0f;
    int sea_bin = 2048;
    for (int b = 0; b < 4096; ++b) {
        acc += bins[b];
        if (acc >= target_ocean) { sea_bin = b; break; }
    }
    float sea_h = min_h + ((float)sea_bin / 4095.0f) * span;
    for (int i = 0; i < N; ++i) {
        /* base 抬高海平面后夹到 [-3000,3800] */
        w->height[i] = qlcg_clampf(w->height[i] - sea_h, -3000.0f, 3800.0f);
        w->water[i] = (w->height[i] <= 0.0f) ? 1 : 0;
    }
}

/* 求解气候、水文与特殊地貌 */
void solve_climate_and_hydrology(MacroWorld *w) {
    int N = w->cell_count;
    qlcg_gen_climate(w);
    qlcg_build_hydrology(w);

    /* 瀑落、湿地与六大特殊地貌 */
    for (int i = 0; i < N; ++i) {
        if (w->height[i] <= 0.0f) continue;
        float aridity = w->rainfall[i] / fmaxf(150.0f, w->evaporation[i]);
        float rw = w->river_width[i];
        int rec = w->receiver[i];
        float drop = (rec >= 0) ? w->height[i] - w->height[rec] : 0.0f;
        if (rec >= 0 && rw >= 8.0f && drop > 95.0f) w->waterfall_flag[i] = 1;
        if (rec >= 0 && rw >= 6.0f && drop < 12.0f && w->moisture[i] > 0.72f && w->height[i] < 160.0f) {
            w->wetland_flag[i] = 1;
        }
        if (w->uplift[i] > 0.85f && w->boundary[i]) w->landform[i] = LANDFORM_VOLCANO;
        else if (w->temperature[i] > 15.0f && aridity > 1.15f && w->height[i] > 180.0f) w->landform[i] = LANDFORM_KARST;
        else if (w->moisture[i] < 0.14f && w->temperature[i] > 12.0f) w->landform[i] = LANDFORM_YARDANG;
        else if (w->temperature[i] < -6.0f && w->height[i] > 480.0f) w->landform[i] = LANDFORM_GLACIER;
        else if (w->temperature[i] < 2.0f && w->height[i] < 140.0f && w->rift[i] > 0.4f) w->landform[i] = LANDFORM_FJORD;
        else if (w->temperature[i] > 22.0f && w->height[i] < 35.0f && w->moisture[i] > 0.65f) w->landform[i] = LANDFORM_MANGROVE;
    }
}
