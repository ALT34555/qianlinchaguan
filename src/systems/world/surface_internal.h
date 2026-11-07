/* 地表与工程的内部契约 */
#ifndef QLCG_SURFACE_INTERNAL_H
#define QLCG_SURFACE_INTERNAL_H

#include "planet_macro.h"

float dist_to_seg2d(float px, float pz, float ax, float az, float bx, float bz, float *out_t);
float qlcg_sample_natural_height(const MacroWorld *w, float wx, float wz);
void qlcg_chunk_shape(const float height[9],float *relief,float *slope,float *relative);
void qlcg_sample_surface_map(const MacroWorld *w, double wx, double wz,
                              float *out_h, float *out_wl, float *out_temp);
typedef struct { float h,wl,temp,depth,source_depth,coast_tint; } SurfaceMapPoint;
void qlcg_sample_surface_detail_map(const MacroWorld *w,double wx,double wz,SurfaceMapPoint *out);
void qlcg_water_ice_state(const MacroWorld *w, float wz, int season, float bed, float level,
                          float annual_temperature, SurfaceWater *out);
float qlcg_chunk_water_vertex(const RegionData *reg, int x, int z);
int   qlcg_lake_shore_at(const MacroWorld *w, float wx, float wz, float *out_level, float *out_d);
void  push_sparse_delta(MacroWorld *w, EngToolType tool, float wx, float wz,
                        float ex, float ez, float radius, float dh, float width_m);

#endif
