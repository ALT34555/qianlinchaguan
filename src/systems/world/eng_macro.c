/* 宏观工程：笔刷与河道改道 */
#include "planet_macro.h"
#include "surface_internal.h"
#include "../../core/math_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void push_sparse_delta(MacroWorld *w, EngToolType tool, float wx, float wz,
                              float ex, float ez, float radius, float dh, float width_m) {
    if (!w) return;
    int idx = (w->delta_count < MAX_MACRO_DELTAS) ? w->delta_count++ : (MAX_MACRO_DELTAS - 1);
    SparseDeltaRecord *dr = &w->deltas[idx];
    dr->type    = tool;
    dr->wx      = wx;
    dr->wz      = wz;
    dr->ex      = ex;
    dr->ez      = ez;
    dr->radius  = radius;
    dr->delta_h = dh;
    dr->width_m = width_m;
    w->delta_version++;
}

/* 2D 舆图自然工程笔刷 */
void qlcg_eng_macro_brush(MacroWorld *w, EngToolType tool, float wx, float wz, float radius) {
    float dh = 0.0f;
    if (tool == ENG_TOOL_DEMOLISH)    dh = -95.0f;
    if (tool == ENG_TOOL_RAISE_MOUNT) dh =  145.0f;
    if (tool == ENG_TOOL_DIG_LAKE)    dh = -110.0f;

    push_sparse_delta(w, tool, wx, wz, wx, wz, radius, dh, 0.0f);

    for (int i = 0; i < w->cell_count; i++) {
        float cx, cz;
        qlcg_world_cell_center(w, i, &cx, &cz);
        float d = hypotf(cx - wx, cz - wz);
        if (d > radius) continue;
        float k = 1.0f - d / radius;
        float s = k * k * (3.0f - 2.0f * k);
        if (tool == ENG_TOOL_DEMOLISH) {
            w->height[i] -= 95.0f * s;
            w->forest_bonus[i] = qlcg_clampf(w->forest_bonus[i] - 0.6f * s, 0.0f, 1.0f);
        } else if (tool == ENG_TOOL_AFFOREST) {
            w->forest_bonus[i]  = qlcg_clampf(w->forest_bonus[i] + 0.75f * s, 0.0f, 1.0f);
            w->desert_factor[i] = qlcg_clampf(w->desert_factor[i] - 0.65f * s, 0.0f, 1.0f);
            w->moisture[i]      = qlcg_clampf(w->moisture[i] + 0.25f * s, 0.0f, 1.0f);
        } else if (tool == ENG_TOOL_RECLAIM_SEA) {
            if (w->height[i] < 12.0f) w->height[i] = qlcg_lerpf(w->height[i], 14.0f, s);
            w->water[i] = 0;
            w->level[i] = w->height[i];
        } else if (tool == ENG_TOOL_RAISE_MOUNT) {
            w->height[i] += 145.0f * s;
        } else if (tool == ENG_TOOL_DIG_LAKE) {
            w->height[i] -= 110.0f * s;
            w->water[i]   = 2;   /* 挖出的是湖，不是海 */
            w->level[i]   = qlcg_maxf(w->level[i], w->height[i] + 18.0f * s);
            w->moisture[i]= qlcg_clampf(w->moisture[i] + 0.45f * s, 0.0f, 1.0f);
        } else if (tool == ENG_TOOL_FLATTEN) {
            w->height[i] = qlcg_lerpf(w->height[i], 28.0f, s * 0.75f);
        }
        w->spill[i] = qlcg_maxf(w->height[i], w->level[i]);
    }
    qlcg_world_rebuild_atlas_fields(w);
}

/* 2D 舆图河流改道工程 */
void qlcg_eng_macro_river(MacroWorld *w, float x0, float z0, float x1, float z1, float width_m) {
    push_sparse_delta(w, ENG_TOOL_RIVER_DIVERT, x0, z0, x1, z1, width_m * 2.0f, -24.0f, width_m);
    if (w->river_count < w->river_cap) {
        RiverSegment *rv = &w->rivers[w->river_count++];
        rv->ax = x0; rv->az = z0;
        rv->bx = x1; rv->bz = z1;
        rv->mx = 0.5f * (x0 + x1);
        rv->mz = 0.5f * (z0 + z1);
        rv->width_m = width_m;
        rv->flow    = 260.0f;
        rv->min_level = 4;   /* 人工干道恒显示 */
    }
    for (int i = 0; i < w->cell_count; i++) {
        float cx, cz, t;
        qlcg_world_cell_center(w, i, &cx, &cz);
        float d = dist_to_seg2d(cx, cz, x0, z0, x1, z1, &t);
        if (d < width_m * 2.4f) {
            float k = 1.0f - d / (width_m * 2.4f);
            w->height[i]   -= 26.0f * k;
            w->moisture[i]  = qlcg_clampf(w->moisture[i] + 0.42f * k, 0.0f, 1.0f);
            w->river_width[i] = qlcg_maxf(w->river_width[i], width_m * 0.1f * k);
            w->flow[i]      = qlcg_maxf(w->flow[i], 95.0f * k);
        }
    }
    qlcg_world_rebuild_atlas_fields(w);
}

/* 3D 山川自由自然工程笔刷 */
