/* 区域高度、水深与释放 */
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

static float water_vertex(const RegionData *reg, int i, float ground) {
    float level=reg->water_levels[i];
    if (level>ground+.02f) return level;
    float depth=reg->shore_depth ? reg->shore_depth[i] : -.25f;
    return ground+fminf(0.0f,depth);
}

float qlcg_chunk_water_vertex(const RegionData *reg, int x, int z) {
    int i=z*reg->width+x;
    return water_vertex(reg,i,reg->heights[i]);
}

static float triangle_value(float a, float b, float c, float d, float x, float z) {
    if (x+z <= 1.0f) return a+(b-a)*x+(c-a)*z;
    return d+(c-d)*(1.0f-x)+(b-d)*(1.0f-z);
}

static float chunk_surface(const RegionData *reg, float lx, float lz, float *water, float *slope) {
    float half = (reg->width-1)*reg->step*0.5f;
    float u = (lx+half)/reg->step, v = (lz+half)/reg->step;
    int x = qlcg_clampi((int)floorf(u),0,reg->width-2);
    int z = qlcg_clampi((int)floorf(v),0,reg->height-2);
    float tx = qlcg_clampf(u-x,0,1), tz = qlcg_clampf(v-z,0,1);
    int ids[4] = {z*reg->width+x,z*reg->width+x+1,(z+1)*reg->width+x,(z+1)*reg->width+x+1};
    float h[4];
    for (int i = 0; i < 4; i++) h[i] = reg->heights[ids[i]];
    if (water) {
        float levels[4];
        for (int i = 0; i < 4; i++) levels[i] = water_vertex(reg,ids[i],h[i]);
        *water=triangle_value(levels[0],levels[1],levels[2],levels[3],tx,tz);
    }
    if (slope) {
        float dx=tx+tz<=1 ? h[1]-h[0] : h[3]-h[2];
        float dz=tx+tz<=1 ? h[2]-h[0] : h[3]-h[1];
        *slope=hypotf(dx,dz)/reg->step;
    }
    return triangle_value(h[0],h[1],h[2],h[3],tx,tz);
}

float qlcg_region_height_at(const RegionData *reg, float lx, float lz) {
    float x,z;
    const RegionData *c = qlcg_region_chunk_at(reg,lx,lz,&x,&z);
    return c ? chunk_surface(c,x,z,NULL,NULL) : NAN;
}

float qlcg_region_slope_at(const RegionData *reg, float lx, float lz) {
    float x,z,slope=NAN;
    const RegionData *c=qlcg_region_chunk_at(reg,lx,lz,&x,&z);
    if (c) chunk_surface(c,x,z,NULL,&slope);
    return slope;
}

float qlcg_region_water_at(const RegionData *reg, float lx, float lz) {
    float x,z;
    const RegionData *c = qlcg_region_chunk_at(reg,lx,lz,&x,&z);
    if (!c) return NAN;
    if (c->water_form == WATER_FORM_CUTOFF) return -1e9f;
    float wl;
    float h=chunk_surface(c,x,z,&wl,NULL);
    return wl > h+0.02f ? wl : -1e9f;
}

/* 释放 3D 山川区域 */
void qlcg_region_free(RegionData *reg) {
    if (!reg) return;
    qlcg_region_stream_free(reg->stream);
    for (int i = 0; i < MAX_UNDO_STEPS; i++) {
        free(reg->undo_stack[i].lake_stroke);
        free(reg->undo_stack[i].heights);
        free(reg->undo_stack[i].water_levels);
        free(reg->undo_stack[i].desert);
        free(reg->undo_stack[i].forest);
    }
    free(reg->heights);
    free(reg->water_levels);
    free(reg->shore_depth);
    free(reg->coast_material);
    free(reg->ice_bottom);
    free(reg->ice_coverage);
    qlcg_ice_chunk_free(reg->ice);
    reg->ice = NULL;
    free(reg->flow_x);
    free(reg->flow_z);
    free(reg->water_kind);
    free(reg->river_sediment);
    free(reg->water_fresh);
    free(reg->source_effects);
    free(reg->temperatures);
    free(reg->wetness);
    free(reg->desert);
    free(reg->forest);
    free(reg->aridities);
    free(reg->uplifts);
    free(reg->colors);
    free(reg->trees);
    free(reg->rocks);
    free(reg->grass);
    free(reg);
}

