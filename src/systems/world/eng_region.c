/* 区域工程：笔刷与样条河道 */
#include "planet_macro.h"
#include "surface_internal.h"
#include "surface_stream.h"
#include "ice_runtime.h"
#include "world_delta.h"
#include "world_surface_dirty.h"
#include "../../core/math_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

EngResult qlcg_eng_region_brush(RegionData *reg, MacroWorld *w, EngToolType tool,
                           float lx, float lz, float radius, int season_idx, const GameSettings *s) {
    if (!reg || !w || !s || !isfinite(lx) || !isfinite(lz) || !isfinite(radius) || radius<=0 ||
        tool<=ENG_TOOL_NONE || tool>=ENG_TOOL_COUNT || tool==ENG_TOOL_RIVER_DIVERT) return ENG_NO_CHANGE;
    if (reg->stream) {
        if(tool==ENG_TOOL_DIG_LAKE && radius<=128) {
            float x=reg->center_x+lx,z=reg->center_z+lz;
            ChdLakeStroke *stroke=qlcg_chd_lake(w,x,z,radius);
            if(!stroke) return ENG_NO_CHANGE;
            qlcg_eng_push_undo(reg,w);
            reg->undo_stack[(reg->undo_top-1)%MAX_UNDO_STEPS].lake_stroke=stroke;
            qlcg_region_stream_refresh(reg,x-radius-2,z-radius-2,x+radius+2,z+radius+2,season_idx,s);
            return ENG_APPLIED;
        }
        if (!qlcg_eng_has_delta_capacity(w,1)) return ENG_CAPACITY;
        qlcg_eng_push_undo(reg, w);
        float wx = reg->center_x + lx, wz = reg->center_z + lz;
        float dh = tool == ENG_TOOL_RAISE_MOUNT ? 24.0f :
            (tool == ENG_TOOL_DEMOLISH ? -18.0f : (tool == ENG_TOOL_DIG_LAKE ? -16.0f : 0));
        if (tool == ENG_TOOL_FLATTEN) dh = qlcg_region_height_at(reg,lx,lz);
        push_sparse_delta(w,tool,wx,wz,wx,wz,radius,dh,0);
        reg->undo_stack[(reg->undo_top-1)%MAX_UNDO_STEPS].delta_count_end=w->delta_count;
        qlcg_region_stream_refresh(reg,wx-radius,wz-radius,wx+radius,wz+radius,season_idx,s);
        return ENG_APPLIED;
    }
    if (!qlcg_eng_has_delta_capacity(w,1)) return ENG_CAPACITY;
    qlcg_eng_push_undo(reg, w);

    float wx = reg->center_x + lx;
    float wz = reg->center_z + lz;
    float dh = 0.0f;
    if (tool == ENG_TOOL_DEMOLISH)    dh = -18.0f;
    if (tool == ENG_TOOL_RAISE_MOUNT) dh =  24.0f;
    if (tool == ENG_TOOL_DIG_LAKE)    dh = -16.0f;
    float center_h = qlcg_region_height_at(reg, lx, lz);
    if (tool == ENG_TOOL_FLATTEN) dh = center_h;
    push_sparse_delta(w, tool, wx, wz, wx, wz, radius, dh, 0.0f);
    reg->undo_stack[(reg->undo_top-1)%MAX_UNDO_STEPS].delta_count_end=w->delta_count;

    float span = (float)(reg->width - 1) * reg->step;
    float half = span * 0.5f;

    for (int z = 0; z < reg->height; z++) {
        for (int x = 0; x < reg->width; x++) {
            int idx = z * reg->width + x;
            float px = (float)x * reg->step - half;
            float pz = (float)z * reg->step - half;
            float d  = hypotf(px - lx, pz - lz);
            if (d > radius) continue;
            float k = 1.0f - d / radius;
            float sm = k * k * (3.0f - 2.0f * k);

            if (tool == ENG_TOOL_DEMOLISH) {
                float rim = (k < 0.25f) ? (k / 0.25f) * 2.2f : -18.5f * ((k - 0.25f) / 0.75f);
                reg->heights[idx] += rim;
                reg->forest[idx]   = qlcg_clampf(reg->forest[idx] - 0.85f * sm, 0.0f, 1.0f);
            } else if (tool == ENG_TOOL_AFFOREST) {
                reg->forest[idx]  = qlcg_clampf(reg->forest[idx] + 0.88f * sm, 0.0f, 1.0f);
                reg->desert[idx]  = qlcg_clampf(reg->desert[idx] - 0.75f * sm, 0.0f, 1.0f);
                reg->wetness[idx] = qlcg_clampf(reg->wetness[idx] + 0.32f * sm, 0.0f, 1.0f);
            } else if (tool == ENG_TOOL_RECLAIM_SEA) {
                float target = qlcg_maxf(reg->water_levels[idx] + 3.2f, 5.5f);
                if (reg->heights[idx] < target)
                    reg->heights[idx] = qlcg_lerpf(reg->heights[idx], target, sm);
            } else if (tool == ENG_TOOL_RAISE_MOUNT) {
                float ridge = 1.0f + 0.25f * sinf(px * 0.12f) * cosf(pz * 0.12f);
                reg->heights[idx] += 24.0f * sm * ridge;
            } else if (tool == ENG_TOOL_DIG_LAKE) {
                reg->heights[idx] -= 16.0f * sm;
                reg->water_levels[idx] = qlcg_maxf(reg->water_levels[idx], reg->heights[idx] + 4.5f * sm);
                reg->wetness[idx]      = qlcg_clampf(reg->wetness[idx] + 0.55f * sm, 0.0f, 1.0f);
            } else if (tool == ENG_TOOL_FLATTEN) {
                reg->heights[idx] = qlcg_lerpf(reg->heights[idx], center_h, sm * 0.85f);
            }
        }
    }
    qlcg_ice_chunk_invalidate(reg);
    qlcg_region_recolor(reg, w, season_idx);
    qlcg_region_scatter_props(reg, w, season_idx, s);
    return ENG_APPLIED;
}

/* 3D 山川样条河流改道工程 */
EngResult qlcg_eng_region_river(RegionData *reg, MacroWorld *w, const float *pts_x, const float *pts_z,
                           int pt_count, float width_m, float depth_m, float meander,
                           int season_idx, const GameSettings *s) {
    if (!reg || !w || !s || !pts_x || !pts_z || pt_count<2 || !isfinite(width_m) ||
        !isfinite(depth_m) || !isfinite(meander) || width_m<=0 || depth_m<=0) return ENG_NO_CHANGE;
    int steps=reg->stream && fabsf(meander)>.01f ? 4 : 1;
    int segments=0;
    for (int i=0;i<pt_count;i++) {
        if (!isfinite(pts_x[i]) || !isfinite(pts_z[i])) return ENG_NO_CHANGE;
        if (i && (pts_x[i]!=pts_x[i-1] || pts_z[i]!=pts_z[i-1])) segments++;
    }
    if (!segments) return ENG_NO_CHANGE;
    float first_dx=pts_x[1]-pts_x[0];
    if (!qlcg_world_flat(w) && w->circumference>0) first_dx=remainderf(first_dx,w->circumference);
    if (reg->stream && pt_count==2 && width_m<=16 && depth_m<=64 && fabsf(meander)<=1 &&
        hypotf(first_dx,pts_z[1]-pts_z[0])<=128) {
        ChdLakeStroke *stroke=qlcg_chd_channel(w,reg->center_x+pts_x[0],reg->center_z+pts_z[0],
            reg->center_x+pts_x[1],reg->center_z+pts_z[1],width_m,depth_m,meander);
        if (!stroke) return ENG_NO_CHANGE;
        qlcg_eng_push_undo(reg,w);
        reg->undo_stack[(reg->undo_top-1)%MAX_UNDO_STEPS].lake_stroke=stroke;
        qlcg_region_stream_sync_changed(reg,season_idx,s);
        return ENG_APPLIED;
    }
    if (segments>MAX_MACRO_DELTAS/steps || !qlcg_eng_has_delta_capacity(w,segments*steps))
        return ENG_CAPACITY;
    uint32_t before=w->delta_version;
    qlcg_eng_push_undo(reg,w);
    float x0=FLT_MAX,z0=FLT_MAX,x1=-FLT_MAX,z1=-FLT_MAX;
    for (int i=0;i<pt_count;i++) {
        float x=reg->center_x+pts_x[i],z=reg->center_z+pts_z[i];
        x0=fminf(x0,x); z0=fminf(z0,z); x1=fmaxf(x1,x); z1=fmaxf(z1,z);
    }
    float margin=width_m*1.5f+fabsf(meander)*6;
    if (reg->stream) {
        for (int i = 0; i+1 < pt_count; i++) {
            float x = reg->center_x + pts_x[i], z = reg->center_z + pts_z[i];
            float dx = pts_x[i+1]-pts_x[i], dz = pts_z[i+1]-pts_z[i];
            if (dx==0 && dz==0) continue;
            float len = fmaxf(hypotf(dx,dz),0.001f);
            float ax = x, az = z;
            for (int k = 1; k <= steps; k++) {
                float t = (float)k/steps, bend = (k==steps?0:sinf(t*6.2831853f)*meander*6.0f);
                float bx = x+dx*t-dz/len*bend, bz = z+dz*t+dx/len*bend;
                qlcg_eng_append_delta(w,(SparseDeltaRecord){ENG_TOOL_RIVER_DIVERT,ax,az,bx,bz,
                                      width_m*1.5f,-depth_m,width_m},QLCG_DELTA_LOCAL);
                ax = bx; az = bz;
            }
        }
        w->delta_version++;
        reg->undo_stack[(reg->undo_top-1)%MAX_UNDO_STEPS].delta_count_end=w->delta_count;
        qlcg_world_surface_dirty(w,before,w->surface_revision,x0-margin,z0-margin,x1+margin,z1+margin,false);
        qlcg_region_stream_refresh(reg,x0-margin,z0-margin,x1+margin,z1+margin,season_idx,s);
        return ENG_APPLIED;
    }

    float span = (float)(reg->width - 1) * reg->step;
    float half = span * 0.5f;

    for (int seg = 0; seg < pt_count - 1; seg++) {
        float ax = pts_x[seg],     az = pts_z[seg];
        float bx = pts_x[seg + 1], bz = pts_z[seg + 1];
        if (ax==bx && az==bz) continue;
        qlcg_eng_append_delta(w,(SparseDeltaRecord){ENG_TOOL_RIVER_DIVERT,
                              reg->center_x+ax,reg->center_z+az,reg->center_x+bx,reg->center_z+bz,
                              width_m*1.5f,-depth_m,width_m},QLCG_DELTA_LOCAL);

        for (int z = 0; z < reg->height; z++) {
            for (int x = 0; x < reg->width; x++) {
                int idx = z * reg->width + x;
                float px = (float)x * reg->step - half;
                float pz = (float)z * reg->step - half;
                float t = 0.0f;
                float d = dist_to_seg2d(px, pz, ax, az, bx, bz, &t);
                float bend = sinf(t * 6.28318f) * meander * 6.0f;
                float eff_d = fabsf(d - bend);
                if (eff_d < width_m * 1.35f) {
                    float k = 1.0f - eff_d / (width_m * 1.35f);
                    float sm = k * k * (3.0f - 2.0f * k);
                    reg->heights[idx] -= depth_m * sm;
                    reg->water_levels[idx] = qlcg_maxf(reg->water_levels[idx], reg->heights[idx] + depth_m * 0.68f * sm);
                    reg->wetness[idx]      = qlcg_clampf(reg->wetness[idx] + 0.52f * sm, 0.0f, 1.0f);
                    reg->desert[idx]       = qlcg_clampf(reg->desert[idx] - 0.60f * sm, 0.0f, 1.0f);
                }
            }
        }
    }
    w->delta_version++;
    reg->undo_stack[(reg->undo_top-1)%MAX_UNDO_STEPS].delta_count_end=w->delta_count;
    qlcg_world_surface_dirty(w,before,w->surface_revision,x0-margin,z0-margin,x1+margin,z1+margin,false);
    qlcg_ice_chunk_invalidate(reg);
    qlcg_region_recolor(reg, w, season_idx);
    qlcg_region_scatter_props(reg, w, season_idx, s);
    return ENG_APPLIED;
}
