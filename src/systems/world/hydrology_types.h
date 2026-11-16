#ifndef QLCG_HYDROLOGY_TYPES_H
#define QLCG_HYDROLOGY_TYPES_H
#include "planet_macro.h"
#include "surface_frame.h"

enum { HYDROLOGY_PATH_POINTS=1025 };

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

typedef struct {
    int kind,cell,source,count;
    RiverPathPoint nodes[HYDROLOGY_PATH_POINTS];
} RiverEdit;
enum { RIVER_EDIT_OK, RIVER_EDIT_INVALID, RIVER_EDIT_UPHILL, RIVER_EDIT_LOOP,
       RIVER_EDIT_OUTSIDE, RIVER_EDIT_TARGET, RIVER_EDIT_ATTACHED, RIVER_EDIT_MEMORY, RIVER_EDIT_ENDPOINT };
typedef struct {
    float shore_level;
    float   level;      /* -1e9 表示无水 */
    float   cut;        /* 河道下切深度 */
    float   ve, vn;     /* 单位流向，东/北 */
    float   discharge;
    float   width_m;
    float   grade;
    float   bank_distance;  /* 距获胜河段岸线，米 */
    float   lateral;        /* 0 河心 .. 1 河岸 */
    uint8_t kind,fresh;
    SourceWaterEffects source_effects;
    int decorative;
    float damp,sediment,channel_depth,waterfall_rock;
} HydrologySample;


#endif
