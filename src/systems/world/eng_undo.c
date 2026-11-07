/* 自然工程撤销栈 */
#include "planet_macro.h"
#include "surface_internal.h"
#include "surface_stream.h"
#include "ice_runtime.h"
#include "../../core/math_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

void qlcg_eng_push_undo(RegionData *reg, const MacroWorld *w) {
    if (!reg) return;
    int slot = reg->undo_top % MAX_UNDO_STEPS;
    TerrainSnapshot *sn = &reg->undo_stack[slot];
    if (reg->stream) {
        sn->delta_count_snap = w ? w->delta_count : 0;
        sn->valid = true;
        reg->undo_top++;
        return;
    }
    int count = reg->width * reg->height;
    if (!sn->heights) {
        sn->heights      = (float *)malloc((size_t)count * sizeof(float));
        sn->water_levels = (float *)malloc((size_t)count * sizeof(float));
        sn->desert       = (float *)malloc((size_t)count * sizeof(float));
        sn->forest       = (float *)malloc((size_t)count * sizeof(float));
    }
    memcpy(sn->heights,      reg->heights,      (size_t)count * sizeof(float));
    memcpy(sn->water_levels, reg->water_levels, (size_t)count * sizeof(float));
    memcpy(sn->desert,       reg->desert,       (size_t)count * sizeof(float));
    memcpy(sn->forest,       reg->forest,       (size_t)count * sizeof(float));
    sn->delta_count_snap = w ? w->delta_count : 0;
    sn->valid = true;
    reg->undo_top++;
}

/* 撤销上一步自然工程操作 */
bool qlcg_eng_undo(RegionData *reg, MacroWorld *w, int season_idx, const GameSettings *s) {
    if (!reg || reg->undo_top <= 0) return false;
    reg->undo_top--;
    int slot = reg->undo_top % MAX_UNDO_STEPS;
    TerrainSnapshot *sn = &reg->undo_stack[slot];
    if (!sn->valid) return false;
    if (reg->stream) {
        w->delta_count = qlcg_clampi(sn->delta_count_snap,0,MAX_MACRO_DELTAS);
        w->delta_version++;
        sn->valid = false;
        qlcg_region_stream_refresh(reg,-FLT_MAX,-FLT_MAX,FLT_MAX,FLT_MAX,season_idx,s);
        return true;
    }
    int count = reg->width * reg->height;
    memcpy(reg->heights,      sn->heights,      (size_t)count * sizeof(float));
    memcpy(reg->water_levels, sn->water_levels, (size_t)count * sizeof(float));
    memcpy(reg->desert,       sn->desert,       (size_t)count * sizeof(float));
    memcpy(reg->forest,       sn->forest,       (size_t)count * sizeof(float));
    if (w) {
        w->delta_count = qlcg_clampi(sn->delta_count_snap, 0, MAX_MACRO_DELTAS);
        w->delta_version++;
    }
    sn->valid = false;
    qlcg_ice_chunk_invalidate(reg);
    qlcg_region_recolor(reg, w, season_idx);
    qlcg_region_scatter_props(reg, w, season_idx, s);
    return true;
}

