#ifndef QLCG_SURFACE_RIVERS_H
#define QLCG_SURFACE_RIVERS_H
#include "planet_macro.h"
#include "surface_frame.h"

typedef struct {
    double wx,wz,arc,level,depth,half_width;
} RiverPathPoint;

typedef struct { double wx,wz,half_width; } RiverMapPoint;
typedef struct { int count,edges[25]; } RiverDetailCandidates;
typedef struct { double position,protection; } RiverValleySample;

typedef struct {
    double wx,wz,arc,level,blend,half_width;
    int kind;
} RiverMouthPoint;
enum { OXBOW_SHAPE_STATIONS = 7, OXBOW_BANK_STATIONS = 25 };
typedef struct {
    SurfaceFrame frame;
    double x,z,axis_x,axis_z,radius,aspect,half_angle,half_width,tip,extent,depth,level;
    float radius_offset[OXBOW_SHAPE_STATIONS];
    float inner_bank[OXBOW_BANK_STATIONS],outer_bank[OXBOW_BANK_STATIONS];
    int type,variant,material,downstream_side;
} OxbowPlan;
typedef struct {
    float bank_level;
    int has_bank_level;
    float height,level,east,north,discharge,width,grade;
    float bank_distance;   /* 距获胜河段岸线，米 */
    float lateral;         /* 0 河心 .. 1 河岸 */
    int kind,decorative,fresh;
    float damp,sediment;
    float channel_depth,waterfall_rock;
    SourceWaterEffects source_effects;
} RiverSurfaceSample;

void qlcg_river_surface(const MacroWorld *w, float wx, float wz, float natural,
                        RiverSurfaceSample *out);
int qlcg_river_path(const MacroWorld *w, int cell, RiverPathPoint *out, int capacity);
/* 只读曲线，不计算地形剖面 */
int qlcg_river_map_path(const MacroWorld *w, int cell, RiverMapPoint *out, int capacity);
int qlcg_river_mouth(const MacroWorld *w,int cell,int outlet,RiverMouthPoint *out);
void qlcg_river_surface_free(MacroWorld *w);
int qlcg_is_river_head(const MacroWorld *w,int cell);
double qlcg_river_discharge_width(double discharge);
int qlcg_macro_cell_has_water(const MacroWorld *w,int cell);
int qlcg_river_detail_candidates(const MacroWorld *w,int cell,RiverDetailCandidates *out);
const OxbowPlan *qlcg_oxbow_plan(const MacroWorld *w,int cell);
void qlcg_oxbow_generate(MacroWorld *w,int cell);
void qlcg_oxbow_free(MacroWorld *w);
void qlcg_oxbow_surface(const MacroWorld *w,double wx,double wz,RiverSurfaceSample *out);
double qlcg_river_detail_gate(const MacroWorld *w,const RiverDetailCandidates *list,double wx,double wz,
                              RiverValleySample *valley);
double qlcg_river_bank_field(uint32_t seed, int cell, double along, int side, double u);
int qlcg_river_bank_section(uint32_t seed, int cell, double along, double offset,
                            double radius, double depth, double level, double natural,
                            double curvature, double *height, int *wet);

#endif
