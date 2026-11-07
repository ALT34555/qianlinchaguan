#ifndef QLCG_SOURCE_RIVERS_H
#define QLCG_SOURCE_RIVERS_H
#include "surface_rivers.h"
#include "surface_frame.h"
#include "river_vectors.h"
#include "river_junction.h"

#define SOURCE_RIVER_LIMIT 12
#define SOURCE_POINT_LIMIT 1025
#define SOURCE_TARGET_LAKE 2
#define SOURCE_TARGET_POOL 3
#define SOURCE_TARGET_CONTINUATION 4
#define SOURCE_FORM_COUNT 24
#define SOURCE_LEGACY_FORM_COUNT 16
#define SOURCE_POOL_FORM_BASE 16
#define SOURCE_POOL_FORM_COUNT 8
#define SOURCE_PROFILE_LIMIT 14

typedef enum { BASIN_HEAD, BASIN_SPRING, BASIN_TERMINAL } BasinMode;

typedef struct {
    const char *name;
    double rx,rz,shift,skew,lobe,depth,rise,bar;
    int drops,pool;
    int pooled;
    double pool_scale;
} SourceForm;

typedef struct {
    double across, depth, relief, roughness, sediment;
} SourceMaterial;

typedef struct {
    int count,mode[SOURCE_PROFILE_LIMIT-1];
    double arc[SOURCE_PROFILE_LIMIT],level[SOURCE_PROFILE_LIMIT],tangent[SOURCE_PROFILE_LIMIT];
    double begin[SOURCE_PROFILE_LIMIT-1],run[SOURCE_PROFILE_LIMIT-1],influence,mouth,transition;
    const RiverVectorPoint *path;
    int path_count,drops;
} SourceProfile;

typedef struct {
    double x,z,level,half_width,min_half_width,depth,discharge;
    double direction_x,direction_z,spring_radius,surface_level;
    double pool_rx,pool_rz,pool_yaw,pool_level,pool_depth,pool_phase;
    int connected,snowmelt,spring_form,spring_style,mirror,pool,damp_only;
    int planned,plan_kind;
    uint32_t seed;
    int material;
    SourceMaterial finish;
} RiverSource;
typedef struct {
    int count,source,target,target_kind;
    RiverVectorPoint *points;
    RiverVectorIndex spatial;
    SourceProfile profile;
    RiverJunction join;
    double join_x,join_z;
    uint64_t craft_id;
    int craft_owner,craft_order,craft_count,*craft_cells;
} SourceRiverEdge;
typedef struct {
    int cell,source_count,edge_count,rejected,probes;
    unsigned long long used;
    SurfaceFrame frame;
    RiverSource *sources;
    SourceRiverEdge *edges;
    int material_chunks;
    int material_x[SOURCE_RIVER_LIMIT],material_z[SOURCE_RIVER_LIMIT];
    uint8_t material_id[SOURCE_RIVER_LIMIT];
} SourceRiverNetwork;

int qlcg_source_candidate(const MacroWorld *w,int cell);
const SourceRiverNetwork *qlcg_source_network(const MacroWorld *w,int cell,int build);
void qlcg_source_surface(const MacroWorld *w,float wx,float wz,float natural,RiverSurfaceSample *out);
void qlcg_source_rivers_free(MacroWorld *w);
void qlcg_source_rocks(RegionData *reg,const MacroWorld *w);
void qlcg_source_dimensions(double rain,double evaporation,int columns,int snow,RiverSource *out);
int qlcg_source_template(double ax,double az,double bx,double bz,uint32_t seed,int attempt,RiverVectorPoint *out);
int qlcg_source_profile(const MacroWorld *w,const SurfaceFrame *frame,int cell,
                         RiverSource *source,RiverVectorPoint *points,int count,int target_source,double join_distance,
                         SourceProfile *profile);
double qlcg_source_profile_value(const SourceProfile *profile,double arc,double *grade,int *fall);
int qlcg_source_section(double distance,double radius,double depth,double natural,double level,
                         double *height,double *water);
int qlcg_source_section_span(double distance,double radius,double depth,double natural,double level,
                              double span,double *height,double *water);
const SourceForm *qlcg_source_form(int kind);
void qlcg_source_material_set(RiverSource *source,int material);
void qlcg_source_pool_shape(RiverSource *source);
void qlcg_source_form_choose(const MacroWorld *w,SourceRiverNetwork *network,RiverSource *source,int id);
void qlcg_source_form_surface(const RiverSource *source,double x,double z,double natural,RiverSurfaceSample *out);
void qlcg_source_form_surface_mode(const RiverSource *source,BasinMode mode,double x,double z,
                                   double natural,RiverSurfaceSample *out);
double qlcg_source_pool_chance(double moisture);
double qlcg_source_pool_radius(const RiverSource *source);
int qlcg_source_basin(const RiverSource *source,BasinMode mode,double x,double z,
                      double natural,RiverSurfaceSample *out);
void qlcg_source_terminal_pool(const SourceRiverNetwork *network,const SourceRiverEdge *edge,RiverSource *pool);

void qlcg_source_surface_mode(const MacroWorld *w,float wx,float wz,float natural,
                              RiverSurfaceSample *out,int build);

#endif
