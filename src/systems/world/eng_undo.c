/* 自然工程撤销栈 */
#include "planet_macro.h"
#include "surface_internal.h"
#include "surface_stream.h"
#include "ice_runtime.h"
#include "world_delta.h"
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
    free(sn->lake_stroke);sn->lake_stroke=NULL;
    sn->delta_count_snap=sn->delta_count_end=w?w->delta_count:0;
    if (reg->stream) {
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
    sn->valid = true;
    reg->undo_top++;
}

static bool remove_stroke_deltas(MacroWorld *w,const TerrainSnapshot *sn) {
    int first=sn->delta_count_snap,end=sn->delta_count_end;
    if(first==end) return true;
    if(!w||first<0||end<first||end>w->delta_count) return false;
    double x0=DBL_MAX,z0=DBL_MAX,x1=-DBL_MAX,z1=-DBL_MAX;
    for(int i=first;i<end;i++) {
        const SparseDeltaRecord *d=&w->deltas[i];
        bool river=d->type==ENG_TOOL_RIVER_DIVERT;
        float r=river?d->width_m*1.4f:d->radius;
        float ex=river?d->ex:d->wx,ez=river?d->ez:d->wz;
        x0=fmin(x0,fmin(d->wx,ex)-r);z0=fmin(z0,fmin(d->wz,ez)-r);
        x1=fmax(x1,fmax(d->wx,ex)+r);z1=fmax(z1,fmax(d->wz,ez)+r);
    }
    uint32_t prior=w->delta_version;
    if(!qlcg_eng_remove_local_deltas(w,first,end)) return false;
    qlcg_world_surface_dirty(w,prior,w->surface_revision,x0,z0,x1,z1,false);
    return true;
}

/* 撤销上一步自然工程操作 */
bool qlcg_eng_undo(RegionData *reg, MacroWorld *w, int season_idx, const GameSettings *s) {
    if (!reg || reg->undo_top <= 0) return false;
    int slot = (reg->undo_top-1) % MAX_UNDO_STEPS;
    TerrainSnapshot *sn = &reg->undo_stack[slot];
    if (!sn->valid||!remove_stroke_deltas(w,sn)) return false;
    reg->undo_top--;
    if(sn->lake_stroke) {
        qlcg_chd_undo_lake(w,sn->lake_stroke);
        free(sn->lake_stroke);sn->lake_stroke=NULL;
    }
    if (reg->stream) {
        sn->valid = false;
        qlcg_region_stream_sync_changed(reg,season_idx,s);
        return true;
    }
    int count = reg->width * reg->height;
    memcpy(reg->heights,      sn->heights,      (size_t)count * sizeof(float));
    memcpy(reg->water_levels, sn->water_levels, (size_t)count * sizeof(float));
    memcpy(reg->desert,       sn->desert,       (size_t)count * sizeof(float));
    memcpy(reg->forest,       sn->forest,       (size_t)count * sizeof(float));
    sn->valid = false;
    qlcg_ice_chunk_invalidate(reg);
    qlcg_region_recolor(reg, w, season_idx);
    qlcg_region_scatter_props(reg, w, season_idx, s);
    return true;
}

