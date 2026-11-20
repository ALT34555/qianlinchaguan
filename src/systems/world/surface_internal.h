/* 地表与工程的内部契约 */
#ifndef QLCG_SURFACE_INTERNAL_H
#define QLCG_SURFACE_INTERNAL_H

#include "planet_macro.h"

PropInstance *qlcg_region_add_rock(RegionData *reg, int rock, float x, float z,
                                   float ground, uint32_t seed, int season);

float dist_to_seg2d(float px, float pz, float ax, float az, float bx, float bz, float *out_t);
PropInstance *qlcg_region_add_plant(RegionData *reg, int species, int category,
                                    int aquatic, float lx, float lz, float y,
                                    float rot_y, float size_xz, float size_y,
                                    int visual_season, int variant, uint32_t roll);
float qlcg_sample_natural_height(const MacroWorld *w, float wx, float wz);
void qlcg_chunk_shape(const float height[9],float *relief,float *slope,float *relative);
void qlcg_world_inspect_sampled_chunk(const MacroWorld *w,int cx,int cz,const float heights[9],
                                      const SurfaceWater *water,bool is_fall,ChunkInspectInfo *out);
void qlcg_surface_apply_deltas(const MacroWorld *w,float wx,float wz,
                                float *h,float *wl,float *wet,float *desert,float *forest);
void qlcg_surface_deltas_free(MacroWorld *w);
bool qlcg_surface_deltas_prepare(MacroWorld *w);
void qlcg_sample_surface_map(const MacroWorld *w, double wx, double wz,
                              float *out_h, float *out_wl, float *out_temp, float *out_wet_gain);
typedef struct { float h,wl,temp,depth,source_depth,coast_tint,wet_gain; } SurfaceMapPoint;
void qlcg_sample_surface_detail_map(const MacroWorld *w,double wx,double wz,SurfaceMapPoint *out);
void qlcg_water_ice_state(const MacroWorld *w, float wz, int season, float bed, float level,
                          float annual_temperature, SurfaceWater *out);
float qlcg_chunk_water_vertex(const RegionData *reg, int x, int z);
int   qlcg_lake_shore_at(const MacroWorld *w, float wx, float wz, float *out_level, float *out_d);
bool  push_sparse_delta(MacroWorld *w, EngToolType tool, float wx, float wz,
                        float ex, float ez, float radius, float dh, float width_m);
bool  qlcg_eng_has_delta_capacity(const MacroWorld *w, int count);
void  qlcg_eng_append_delta(MacroWorld *w, SparseDeltaRecord delta, uint8_t origin);
bool  qlcg_eng_remove_local_deltas(MacroWorld *w, int first, int end);
uint8_t qlcg_eng_legacy_delta_origin(const SparseDeltaRecord *delta);
bool qlcg_eng_install_saved_deltas(MacroWorld *w, const SparseDeltaRecord *records,
                                   const uint8_t *origins, int count);
void qlcg_eng_restore_macro_deltas(MacroWorld *w);
float qlcg_eng_macro_river_distance(const MacroWorld *w, const SparseDeltaRecord *d, float x, float z);
float qlcg_eng_macro_river_cut(const MacroWorld *w, const SparseDeltaRecord *d, int cell);

#endif
