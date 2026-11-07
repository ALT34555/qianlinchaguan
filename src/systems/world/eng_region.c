/* 区域工程：笔刷与样条河道 */
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

void qlcg_eng_region_brush(RegionData *reg, MacroWorld *w, EngToolType tool,
                           float lx, float lz, float radius, int season_idx, const GameSettings *s) {
    if (!reg) return;
    if (reg->stream) {
        if (w->delta_count >= MAX_MACRO_DELTAS || radius <= 0) return;
        qlcg_eng_push_undo(reg, w);
        float wx = reg->center_x + lx, wz = reg->center_z + lz;
        float dh = tool == ENG_TOOL_RAISE_MOUNT ? 24.0f :
            (tool == ENG_TOOL_DEMOLISH ? -18.0f : (tool == ENG_TOOL_DIG_LAKE ? -16.0f : 0));
        if (tool == ENG_TOOL_FLATTEN) dh = qlcg_region_height_at(reg,lx,lz);
        push_sparse_delta(w,tool,wx,wz,wx,wz,radius,dh,0);
        qlcg_region_stream_refresh(reg,wx-radius,wz-radius,wx+radius,wz+radius,season_idx,s);
        return;
    }
    qlcg_eng_push_undo(reg, w);

    float wx = reg->center_x + lx;
    float wz = reg->center_z + lz;
    float dh = 0.0f;
    if (tool == ENG_TOOL_DEMOLISH)    dh = -18.0f;
    if (tool == ENG_TOOL_RAISE_MOUNT) dh =  24.0f;
    if (tool == ENG_TOOL_DIG_LAKE)    dh = -16.0f;
    push_sparse_delta(w, tool, wx, wz, wx, wz, radius, dh, 0.0f);

    float span = (float)(reg->width - 1) * reg->step;
    float half = span * 0.5f;
    float center_h = qlcg_region_height_at(reg, lx, lz);

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
}

/* 3D 山川样条河流改道工程 */
void qlcg_eng_region_river(RegionData *reg, MacroWorld *w, const float *pts_x, const float *pts_z,
                           int pt_count, float width_m, float depth_m, float meander,
                           int season_idx, const GameSettings *s) {
    if (!reg || pt_count < 2) return;
    if (reg->stream) {
        int steps = meander > 0.01f ? 4 : 1;
        if (w->delta_count + (pt_count-1)*steps > MAX_MACRO_DELTAS) return;
        qlcg_eng_push_undo(reg,w);
        float x0 = FLT_MAX, z0 = FLT_MAX, x1 = -FLT_MAX, z1 = -FLT_MAX;
        for (int i = 0; i < pt_count; i++) {
            float x = reg->center_x + pts_x[i], z = reg->center_z + pts_z[i];
            x0 = fminf(x0,x); z0 = fminf(z0,z); x1 = fmaxf(x1,x); z1 = fmaxf(z1,z);
            if (i+1 < pt_count) {
                float dx = pts_x[i+1]-pts_x[i], dz = pts_z[i+1]-pts_z[i];
                float len = fmaxf(hypotf(dx,dz),0.001f);
                float ax = x, az = z;
                for (int k = 1; k <= steps; k++) {
                    float t = (float)k/steps, bend = sinf(t*6.2831853f)*meander*6.0f;
                    float bx = x+dx*t-dz/len*bend, bz = z+dz*t+dx/len*bend;
                    push_sparse_delta(w,ENG_TOOL_RIVER_DIVERT,ax,az,bx,bz,width_m*1.5f,-depth_m,width_m);
                    ax = bx; az = bz;
                }
            }
        }
        float margin = width_m*1.5f+meander*6.0f;
        qlcg_region_stream_refresh(reg,x0-margin,z0-margin,x1+margin,z1+margin,season_idx,s);
        return;
    }
    qlcg_eng_push_undo(reg, w);

    float span = (float)(reg->width - 1) * reg->step;
    float half = span * 0.5f;

    for (int seg = 0; seg < pt_count - 1; seg++) {
        float ax = pts_x[seg],     az = pts_z[seg];
        float bx = pts_x[seg + 1], bz = pts_z[seg + 1];
        push_sparse_delta(w, ENG_TOOL_RIVER_DIVERT,
                          reg->center_x + ax, reg->center_z + az,
                          reg->center_x + bx, reg->center_z + bz,
                          width_m * 1.5f, -depth_m, width_m);

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
    qlcg_ice_chunk_invalidate(reg);
    qlcg_region_recolor(reg, w, season_idx);
    qlcg_region_scatter_props(reg, w, season_idx, s);
}
