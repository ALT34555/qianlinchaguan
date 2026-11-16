#ifndef QLCG_HYDROLOGY_HEADWATER_H
#define QLCG_HYDROLOGY_HEADWATER_H
#include "surface_frame.h"
#include "hydrology_vectors.h"
#include "hydrology_waterfalls.h"

enum { HEAD_VARIANT_COUNT=4,HEAD_BRANCH_LIMIT=12 };
typedef struct {
    int parent,point_count;
    double ax,az,bx,bz,start,end,width,fraction,target_x,target_z;
} HeadwaterValley;
typedef struct {
    int count;
    const RiverVectorPoint *points[HEAD_BRANCH_LIMIT];
    const RiverVectorIndex *indices[HEAD_BRANCH_LIMIT];
    int sizes[HEAD_BRANCH_LIMIT];
    const float *ground[HEAD_BRANCH_LIMIT];
    double backdrop[HEAD_BRANCH_LIMIT];
} HeadwaterGeometryView;

enum { HEAD_HOLLOW,HEAD_CLIFF,HEAD_GRAVEL,HEAD_SAND,HEAD_MARSH,HEAD_STEPS,HEAD_SNOW,HEAD_ALPINE,HEAD_KIND_COUNT };
typedef enum {
    HEAD_REGION_GORGE,HEAD_REGION_NETWORK,HEAD_REGION_SNOW,HEAD_REGION_CONFLUENCE,
    HEAD_REGION_MARSH,HEAD_REGION_LAKE,HEAD_REGION_SPRING,HEAD_REGION_COUNT
} HeadwaterRegionKind;
typedef struct {
    int family;
    double aspect,shear,warp,ridge,lift,spread,bend;
    double peaks[3][4],angles[5],reaches[5];
    int branch_count,geometry_ready;
    uint64_t geometry_hash;
    HeadwaterValley branches[HEAD_BRANCH_LIMIT];
} HeadwaterDesign;
typedef struct {
    int state,kind,material,cell,springs,design,region;
    uint32_t seed;
    double x,z,dx,dz,radius,rise,pool_radius,skew,phase;
    double source_ground,outlet_ground,source_level,outlet_level,run,moisture;
    double cold,warm;
    HeadwaterDesign shape;
    int variant;
    double span,span_x,center_x,center_z,lift,ridge_height,valley_width,grade,edge_width;
} HeadwaterPlan;

const HeadwaterPlan *qlcg_headwater_plan(const MacroWorld *w,int cell);
int qlcg_headwater_build(const MacroWorld *w,int cell,HeadwaterPlan *plan);
int qlcg_headwater_store(const MacroWorld *w,const HeadwaterPlan *plan);
int qlcg_headwater_validate(const unsigned char *data,size_t size,int cell);
void qlcg_headwater_compact(const MacroWorld *w,int cell);
double qlcg_headwater_height(const MacroWorld *w,double wx,double wz,double base);
void qlcg_headwater_color(const HeadwaterPlan *plan,double x,double z,float temperature,uint8_t color[3]);
float qlcg_headwater_activity(const HeadwaterPlan *plan,float temperature);
int qlcg_headwater_excludes(const MacroWorld *w,double wx,double wz,double radius);
const char *qlcg_headwater_name(int kind);
const char *qlcg_headwater_region_name(int kind);
int qlcg_headwater_region_build(const MacroWorld *w,int cell,HeadwaterPlan *plan);
void qlcg_headwater_region_shape(HeadwaterPlan *plan);
double qlcg_headwater_region_height(const HeadwaterPlan *plan,double x,double z,double base);
void qlcg_headwater_region_color(const HeadwaterPlan *plan,double x,double z,float temperature,uint8_t color[3]);
const char *qlcg_headwater_variant_name(int region,int variant);
int qlcg_headwater_landform_build(const MacroWorld *w,int cell,HeadwaterPlan *plan);
void qlcg_headwater_layout(const MacroWorld *w,HeadwaterPlan *plan);
double qlcg_headwater_landform_height(const MacroWorld *w,const HeadwaterPlan *plan,double wx,double wz,double base,int detail);
double qlcg_headwater_map_height(const MacroWorld *w,double wx,double wz,double base);
double qlcg_headwater_mass_weight(const MacroWorld *w,int cell,double wx,double wz);
MacroWaterfall qlcg_headwater_fall_plan(const HeadwaterPlan *plan,int branch,double length);
double qlcg_headwater_backdrop(const MacroWorld *w,const HeadwaterPlan *plan,double wx,double wz,double base);
const HeadwaterGeometryView *qlcg_headwater_geometry(const MacroWorld *w,int cell);
#endif
