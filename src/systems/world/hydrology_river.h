#ifndef QLCG_HYDROLOGY_RIVER_H
#define QLCG_HYDROLOGY_RIVER_H
#include "hydrology_types.h"
#include "hydrology_channel.h"

/* 查询会更新缓存，同世界需串行 */
int qlcg_river_head_constraint(const MacroWorld *w,int cell,double *run,double *level);
void qlcg_river_surface(const MacroWorld *w, float wx, float wz, float natural,
                        RiverSurfaceSample *out);
void qlcg_river_surface_with_widths(const MacroWorld *w,float wx,float wz,float natural,
                                    const HydrologyWidthQuery *widths,RiverSurfaceSample *out);
int qlcg_river_join_station(const MacroWorld *w,int cell,double wx,double wz,double *arc);
int qlcg_river_path(const MacroWorld *w, int cell, RiverPathPoint *out, int capacity);
/* 地图路径复用既有河源约束 */
int qlcg_river_map_path(const MacroWorld *w, int cell, RiverMapPoint *out, int capacity);
int qlcg_river_mouth(const MacroWorld *w,int cell,int outlet,RiverMouthPoint *out);
void qlcg_river_surface_free(MacroWorld *w);
int qlcg_is_river_head(const MacroWorld *w,int cell);
double qlcg_river_discharge_width(double discharge);
int qlcg_macro_cell_has_water(const MacroWorld *w,int cell);
int qlcg_river_detail_candidates(const MacroWorld *w,int cell,RiverDetailCandidates *out);
double qlcg_river_detail_gate(const MacroWorld *w,const RiverDetailCandidates *list,double wx,double wz,
                              RiverValleySample *valley);
double qlcg_river_relief_gate(const MacroWorld *w,const RiverDetailCandidates *list,double wx,double wz,double fade);
double qlcg_river_bank_field(uint32_t seed, int cell, double along, int side, double u);
int qlcg_river_bank_section(uint32_t seed, int cell, double along, double offset,
                            double radius, double depth, double level, double natural,
                            double curvature, double *height, int *wet);
int qlcg_river_bank_section_span(uint32_t seed,int cell,double along,double offset,
                                double radius,double depth,double level,double natural,
                                double curvature,double bank_span,double *height,int *wet);

#endif
