#ifndef QLCG_SURFACE_LAKES_H
#define QLCG_SURFACE_LAKES_H
#include "planet_macro.h"

typedef struct {
    int lake;
    float level, d, bed, rise, wob;
} LakeShoreField;

void qlcg_lakes_free(MacroWorld *w);
void qlcg_lakes_record(MacroWorld *w, int lake, int cell);
void qlcg_lakes_prepare(MacroWorld *w);
void qlcg_lakes_connect(MacroWorld *w, int lake, int cell);
void qlcg_lakes_attach_river(MacroWorld *w,int lake,int cell,int edge);
int qlcg_lake_overlaps_cell(const MacroWorld *w, int cell);
int qlcg_lake_outflow_at(const MacroWorld *w,int cell);
int qlcg_lake_connection(const MacroWorld *w,int cell,int outlet,double *wx,double *wz);
int qlcg_lake_river_candidates(const MacroWorld *w,int cell,int *edges,int capacity);
int qlcg_lake_channel_target(const MacroWorld *w,int cell,double from_x,double from_z,double *wx,double *wz);
LakeShoreField qlcg_lake_field(const MacroWorld *w, double wx, double wz);
int qlcg_lake_island_at(const MacroWorld *w, int lake, double *ox, double *oz);
float qlcg_lake_height(const LakeShoreField *f, float natural);
#endif
