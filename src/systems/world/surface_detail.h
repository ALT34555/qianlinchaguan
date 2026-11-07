#ifndef QLCG_SURFACE_DETAIL_H
#define QLCG_SURFACE_DETAIL_H
#include "surface_macro.h"
#include "surface_frame.h"

typedef struct { double uplift,rift,shear,region,weights[6]; } LocalLandformProfile;
typedef struct { SurfaceFrame frame; double dx,dz,radius,amplitude,phase; } HeadwaterLandform;

typedef enum {
    SRC_PLAN_NONE,SRC_PLAN_SPRING,SRC_PLAN_STEP,SRC_PLAN_TARN,
    SRC_PLAN_HANGING,SRC_PLAN_GENTLE,SRC_PLAN_SNOWPEAK,SRC_PLAN_LAKE_SHORE
} SourcePlanKind;

typedef struct {
    SurfaceFrame frame;
    double x,z,dx,dz,radius,amplitude,phase,slope;
    uint32_t seed;
    int valid,kind,snow,mirror;
} SourcePlan;

typedef struct {
    int cell,head,near_river,lake_in,lake_out;
    SourcePlan source;
} MacroCellPlan;
typedef struct {
    int nodes,features,masks,heads;
    unsigned long long node_hits,node_misses,feature_builds;
} TerrainDetailStats;

double qlcg_detail_pole(const MacroWorld *w,const float *values,int pole,double fallback);
void qlcg_local_landform_profile(const MacroWorld *w,const double p[3],LocalLandformProfile *out);
double qlcg_local_landform_height(const MacroWorld *w,const double p[3]);
double qlcg_local_landform_weight(const MacroWorld *w,const SurfaceMacroContext *context,double wx,double wz);
int qlcg_headwater_landform(const MacroWorld *w,int cell,HeadwaterLandform *out);
const MacroCellPlan *qlcg_macro_cell_plan(const MacroWorld *w,int cell);
double qlcg_headwater_relief(const MacroWorld *w,double wx,double wz);
double qlcg_regional_relief(const MacroWorld *w,double wx,double wz);
double qlcg_sampled_regional_relief(const MacroWorld *w,double wx,double wz);
double qlcg_natural_detail_height(const MacroWorld *w,double wx,double wz);
void qlcg_terrain_detail_free(MacroWorld *w);
TerrainDetailStats qlcg_terrain_detail_stats(const MacroWorld *w);

#endif
