/* 冰/水单元判据：网格与回归共用，两套互斥 */
#ifndef QLCG_TERRAIN_ICE_CELLS_H
#define QLCG_TERRAIN_ICE_CELLS_H

#include "terrain_types.h"
#include "../systems/world/ice_runtime.h"

/* 单元有水：四角至少两顶点水面高于地面 2cm */
static inline bool qlcg_cell_is_wet(const RegionData *reg, int x, int z) {
    if (!reg->water_levels) return false;
    int src = (z + 1) * reg->width + x + 1;
    int wet = 0;
    if (reg->water_levels[src] > reg->heights[src] + 0.02f) wet++;
    if (reg->water_levels[src + 1] > reg->heights[src + 1] + 0.02f) wet++;
    if (reg->water_levels[src + reg->width] > reg->heights[src + reg->width] + 0.02f) wet++;
    if (reg->water_levels[src + reg->width + 1] > reg->heights[src + reg->width + 1] + 0.02f) wet++;
    return wet > 0;
}

/* 画冰：四角任一覆盖≥.5 且未破洞，与水互斥 */
static inline bool qlcg_cell_draws_ice(const RegionData *reg, int x, int z) {
    if (!reg->ice_coverage) return false;
    int src = (z + 1) * reg->width + x + 1;
    if (reg->ice_coverage[src] < .5f && reg->ice_coverage[src + 1] < .5f &&
        reg->ice_coverage[src + reg->width] < .5f &&
        reg->ice_coverage[src + reg->width + 1] < .5f) return false;
    if (reg->ice && qlcg_ice_cell_open(reg->ice, x, z)) return false;
    return true;
}

/* 画冰且四邻有不画冰：洞/水边啃痕带 */
static inline bool qlcg_cell_needs_edge(const RegionData *reg, int x, int z) {
    static const int dx4[4] = { -1, 1, 0, 0 };
    static const int dz4[4] = { 0, 0, -1, 1 };
    if (!qlcg_cell_draws_ice(reg, x, z)) return false;
    for (int k = 0; k < 4; k++) {
        int nx = x + dx4[k], nz = z + dz4[k];
        if (nx < 0 || nz < 0 || nx >= CHUNK_CELLS || nz >= CHUNK_CELLS) continue;
        if (!qlcg_cell_draws_ice(reg, nx, nz)) return true;
    }
    return false;
}

#endif /* QLCG_TERRAIN_ICE_CELLS_H */
