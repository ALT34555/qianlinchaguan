#ifndef QLCG_HYDROLOGY_RIVER_INTERNAL_H
#define QLCG_HYDROLOGY_RIVER_INTERNAL_H
#include "hydrology_river.h"
#include "hydrology_profile.h"
#include "hydrology_junction.h"
#include "hydrology_waterfalls.h"
#include "hydrology_archive.h"
#include "../../core/math_util.h"

#define QLCG_RIVER_PI 3.14159265358979323846
enum { RIVER_CURVE_POINTS=161 };
enum { RIVER_CURVE_NATURAL, RIVER_CURVE_LEGACY, RIVER_CURVE_LOCAL_EDIT };

typedef SurfacePoint Point;
typedef SurfaceFrame Frame;
typedef RiverVectorPoint CurvePoint;
typedef struct { double left,right,top,bottom; } ReachBounds;
typedef struct {
    int cell,receiver,count,sea_mouth,profile_ready,profile_building,edit_mode,fixed_water;
    unsigned long long used;
    Frame frame;
    CurvePoint points[RIVER_CURVE_POINTS];
    double parameters[RIVER_CURVE_POINTS],saved_curvature[RIVER_CURVE_POINTS];
    unsigned char edited_segments[RIVER_CURVE_POINTS];
    double depth_a,depth_b;
    double head_run,head_level,head_end;
    double detail_clear,detail_fade,valley_span,origin_x;
    ReachBounds detail_bounds;
    RiverProfile profile,ground_profile;
    MacroWaterfall waterfall;
    RiverVectorIndex spatial;
    RiverMouthPoint inlet,outlet;
} RiverCurve;
typedef struct { int key; RiverDetailCandidates list; } RiverCellCache;
typedef struct { int key,valid,edges[3]; Frame frame; RiverJunction plan; } JunctionSlot;
typedef struct { const MacroWorld *w; const RiverCurve *c; } GroundContext;

static inline int qlcg_river_curve_is_legacy(const RiverCurve *c) {
    return c->edit_mode==RIVER_CURVE_LEGACY;
}

static inline Point qlcg_river_cell_center(const MacroWorld *w,int i) {
    return (Point){((i%w->columns+.5)/w->columns-.5)*w->circumference,
                   ((i/w->columns+.5)/w->rows-.5)*qlcg_world_depth(w)};
}

static inline double qlcg_river_main_width(const MacroWorld *w,int i) {
    return w->main_river_width?fmax(w->main_river_width[i],w->river_width[i]):w->river_width[i];
}

static inline double qlcg_river_random_next(uint32_t *seed) {
    uint32_t t=(*seed+=0x6d2b79f5u);
    t=(t^(t>>15))*(t|1u); t^=t+(t^(t>>7))*(t|61u);
    return (t^(t>>14))/4294967296.0;
}

static inline int qlcg_river_curve_arc_end(const RiverCurve *c,double arc) {
    int lo=0,hi=c->count-1;
    while (lo<hi) { int mid=(lo+hi)/2; if (c->points[mid].arc<arc) lo=mid+1; else hi=mid; }
    return lo;
}

static inline double qlcg_river_curve_profile_arc(const RiverCurve *c,double arc) {
    if(c->edit_mode!=RIVER_CURVE_LOCAL_EDIT) return arc;
    int hi=qlcg_river_curve_arc_end(c,arc),lo=hi?hi-1:0;
    double t=qlcg_clampd((arc-c->points[lo].arc)/fmax(1e-9,c->points[hi].arc-c->points[lo].arc),0,1);
    return qlcg_mixd(c->parameters[lo],c->parameters[hi],t);
}

static inline double qlcg_river_curve_geometry_arc(const RiverCurve *c,double parameter) {
    if(c->edit_mode!=RIVER_CURVE_LOCAL_EDIT) return parameter;
    int lo=0,hi=c->count-1;
    while(hi-lo>1) { int mid=(lo+hi)/2; if(c->parameters[mid]>parameter) hi=mid; else lo=mid; }
    double t=qlcg_clampd((parameter-c->parameters[lo])/fmax(1e-9,c->parameters[hi]-c->parameters[lo]),0,1);
    return qlcg_mixd(c->points[lo].arc,c->points[hi].arc,t);
}

static inline Point qlcg_river_curve_position(const RiverCurve *c,double arc) {
    int lo=qlcg_river_curve_arc_end(c,arc);
    const CurvePoint *a=&c->points[lo?lo-1:0],*b=&c->points[lo];
    double span=b->arc-a->arc,t=span>0?qlcg_clampd((arc-a->arc)/span,0,1):0;
    return (Point){qlcg_mixd(a->x,b->x,t),qlcg_mixd(a->z,b->z,t)};
}

static inline double qlcg_river_curve_depth(const RiverCurve *c,double arc) {
    double length=c->edit_mode==RIVER_CURVE_LOCAL_EDIT?c->profile.arc:c->points[c->count-1].arc,t=length>0?qlcg_river_curve_profile_arc(c,arc)/length:0;
    return qlcg_mixd(c->depth_a,c->depth_b,t*t*(3-2*t));
}

static inline double qlcg_river_curve_level(const RiverCurve *c,double arc) {
    if(c->head_run>0&&arc<c->head_run)
        return qlcg_mixd(c->head_level,c->head_end,qlcg_smooth01d(arc/c->head_run));
    double parameter=qlcg_river_curve_profile_arc(c,arc),stored_level=qlcg_river_profile_level(&c->profile,parameter);
    if(c->fixed_water&&c->edit_mode==RIVER_CURVE_NATURAL)return stored_level;
    if(!c->ground_profile.count) return stored_level;
    double ground=qlcg_river_profile_level(&c->ground_profile,parameter);
    double depth=fmin(3,fmax(.6,qlcg_river_curve_depth(c,arc)*.5));
    return qlcg_mixd(qlcg_clampd(stored_level,ground-depth,ground+.15),stored_level,(qlcg_river_curve_is_legacy(c)?1.0:0.0));
}

static inline double qlcg_river_curve_head_width(const RiverCurve *c,double arc,double width) {
    return c->head_run>0?qlcg_mixd(fmin(2.8,width),width,qlcg_smooth01d(arc/c->head_run)):width;
}

RiverCurve *qlcg_river_curve_at(const MacroWorld *w,int cell);
RiverCurve *qlcg_river_curve_build(const MacroWorld *w,int cell);
void qlcg_river_curve_free(RiverCurve *curve);
int qlcg_river_curve_finish(const MacroWorld *w,RiverCurve *curve,int restored);
double qlcg_river_width_limit(const MacroWorld *w,double wz);
int qlcg_river_curve_record(const MacroWorld *w,RiverCurve *curve,RiverBuffer *buffer);
int qlcg_river_curve_store(const MacroWorld *w,RiverCurve *curve);
int qlcg_river_ensure_profile(const MacroWorld *w,RiverCurve *curve);
RiverGround qlcg_river_curve_ground(void *context,double arc);
double qlcg_river_head_receiver_level(const MacroWorld *w,const RiverCurve *curve,double arc);
double qlcg_river_crossfall_level(const MacroWorld *w,RiverCurve *curve,Point point,Point direction,
                                 double arc,double radius,double offset,double natural,double level,
                                 double depth,double blend,double *grade);
RiverCellCache *qlcg_river_candidate_slot(const MacroWorld *w,int cell);
JunctionSlot *qlcg_river_junction_slot(const MacroWorld *w,int cell);
RiverCurve *qlcg_river_preview(const MacroWorld *w);
RiverCurve *qlcg_river_preview_swap(const MacroWorld *w,RiverCurve *preview);
const JunctionSlot *qlcg_river_junction_at(const MacroWorld *w,int cell);
int qlcg_river_junction_edge(const JunctionSlot *junction,int cell);
void qlcg_river_decorate_junction(const RiverJunction *plan,Point point,double weight,double core,RiverSurfaceSample *out);
#endif
