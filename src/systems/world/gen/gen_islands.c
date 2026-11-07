/* 宏观取岛：深海连片格按概率抬升并登记 */
#include "gen_islands.h"
#include "../../../core/math_util.h"
#include <stdlib.h>
#include <string.h>

static float rand01(uint32_t *s) {
    uint32_t t = (*s += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    uint32_t a = t ^ (t >> 7);
    uint32_t b = a * (t | 61u);
    t = t ^ (t + b);
    return (float)((double)(t ^ (t >> 14)) / 4294967296.0);
}

/* 登记一条岛记录，供岛岸目录使用 */
static int record_island(MacroWorld *w, int cell, float base_h, float peak, float probability) {
    if (w->macro_island_count >= w->macro_island_cap) {
        int cap = w->macro_island_cap ? w->macro_island_cap * 2 : 32;
        MacroIsland *next = (MacroIsland *)realloc(w->macro_islands, (size_t)cap * sizeof(*next));
        if (!next) return 0;
        w->macro_islands = next;
        w->macro_island_cap = cap;
    }
    w->macro_islands[w->macro_island_count++] =
        (MacroIsland){cell, base_h, peak, probability};
    return 1;
}

int qlcg_raise_macro_islands(MacroWorld *w) {
    if (!w) return 0;
    uint32_t seed = w->opt.seed ^ 0x6d69736cu;
    int n = w->cell_count;
    int count = 0;
    for (int cell = 0; cell < n; cell++) {
        if (qlcg_world_border(w,cell)) continue;
        float base_h = w->height[cell];
        if (base_h >= -16.0f) continue;
        int eligible = 1;
        int near_collision = 0;
        for (int d = 0; d < 8; d++) {
            int next = w->neighbors[cell * 8 + d];
            if (w->height[next] >= -16.0f) { eligible = 0; break; }
            if (w->boundary[next] == 1) near_collision = 1;
        }
        if (!eligible) continue;
        float probability = (w->boundary[cell] == 1) ? 0.03f : (near_collision ? 0.004f : 0.0f);
        if (probability <= 0.0f) continue;
        if (rand01(&seed) >= probability) continue;
        float peak = 32.0f + rand01(&seed) * 128.0f;
        w->height[cell] = peak;
        record_island(w, cell, base_h, peak, probability);
        count++;
    }
    return count;
}
