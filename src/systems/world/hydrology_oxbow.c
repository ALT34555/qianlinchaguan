/* 牛轭湖规划、目录与采样 */
#include "hydrology_oxbow.h"
#include "hydrology_river_internal.h"
#include "hydrology_lakes.h"
#include "hydrology_fields.h"
#include "surface_macro.h"
#include "surface_internal.h"
#include <stdlib.h>

struct OxbowCatalog {
    int *lookup,count,capacity;
    OxbowPlan *plans;
};
static double oxbow_offset(const float *stations,int count,double along) {
    double at=along*(count-1);
    int i=(int)fmin(count-2,at);
    double t=qlcg_smooth01d(at-i);
    return qlcg_mixd(stations[i],stations[i+1],t);
}

static Point oxbow_arc(const OxbowPlan *p,double angle,double radius) {
    double x=radius*p->aspect*cos(angle),z=radius*sin(angle);
    return (Point){p->x+x*p->axis_x-z*p->axis_z,p->z+x*p->axis_z+z*p->axis_x};
}

static double oxbow_downstream(const OxbowPlan *p,double angle) {
    return p->downstream_side*sin(angle)/sin(fmin(p->half_angle,QLCG_RIVER_PI*.5));
}

static void finish_oxbow(const MacroWorld *w,OxbowPlan *p,uint32_t seed,int side,double room) {
    static const struct { double width,depth,lobes,roughness; } shore[5]={
        {.91,1.14,.12,.26},
        {1,1,.22,.14},
        {1.12,.67,.29,.08},
        {1.21,.76,.25,.06},
        {1.02,.91,.17,.34}
    };
    Point apex=oxbow_arc(p,0,p->radius);
    Point world=qlcg_frame_coordinates(&p->frame,apex.x,apex.z);
    int material=qlcg_world_natural_chunk_material(w,(int)floor(world.x/CHUNK_SIZE),
                                                     (int)floor(world.z/CHUNK_SIZE));
    p->material=material>=1&&material<=5?material:2;
    p->downstream_side=side;
    int kind=p->material-1;
    p->depth*=shore[kind].depth;
    double radial_limit=fmin(.062*p->radius,.43*p->half_width);
    for (int i=1;i<OXBOW_SHAPE_STATIONS-1;i++)
        p->radius_offset[i]=(float)((2*qlcg_river_random_next(&seed)-1)*radial_limit);

    double bank_extension=0;
    for (int bank=0;bank<2;bank++) {
        float lobes[OXBOW_SHAPE_STATIONS];
        for (int i=0;i<OXBOW_SHAPE_STATIONS;i++) lobes[i]=(float)(2*qlcg_river_random_next(&seed)-1);
        float *offsets=bank?p->outer_bank:p->inner_bank;
        for (int i=0;i<OXBOW_BANK_STATIONS;i++) {
            double along=i/(double)(OXBOW_BANK_STATIONS-1);
            double down=oxbow_downstream(p,p->half_angle*(2*along-1));
            double coarse=oxbow_offset(lobes,OXBOW_SHAPE_STATIONS,along);
            double detail=2*qlcg_river_random_next(&seed)-1;
            double width=shore[kind].width+.38*down+shore[kind].lobes*coarse+shore[kind].roughness*detail;
            offsets[i]=(float)((qlcg_clampd(width,.50,1.80)-1)*p->half_width);
            bank_extension=fmax(bank_extension,offsets[i]);
        }
    }
    double envelope=radial_limit*fmax(1,p->aspect)+bank_extension;
    double strength=fmin(1,.9*room/envelope);
    for (int i=1;i<OXBOW_SHAPE_STATIONS-1;i++) p->radius_offset[i]*=(float)strength;
    for (int i=0;i<OXBOW_BANK_STATIONS;i++) {
        p->inner_bank[i]*=(float)strength;
        p->outer_bank[i]*=(float)strength;
    }
    p->extent+=envelope*strength;
}

static int place_oxbow(const MacroWorld *w,int cell,OxbowPlan *p,double *room) {
    Point origin=qlcg_river_cell_center(w,cell),anchor=qlcg_frame_coordinates(&p->frame,p->x,p->z);
    double C=w->circumference,half=C/w->columns*.5;
    double scale=.98*cos((fabs(origin.z)+half)/C*2*QLCG_RIVER_PI);
    double x=anchor.x-origin.x;
    x-=floor(x/C+.5)*C;
    if (fabs(x)*scale+p->extent+4>=half*scale ||
        fabs(anchor.z-origin.z)+p->extent+4>=half) return 0;
    double spare=fmin((half-fabs(x))*scale,half-fabs(anchor.z-origin.z))-p->extent-4;
    RiverDetailCandidates candidates;
    if (!qlcg_river_detail_candidates(w,cell,&candidates)) return 0;
    for (int i=0;i<candidates.count;i++) {
        RiverCurve *c=qlcg_river_curve_at(w,candidates.edges[i]);
        if (!c) return 0;
        Point q=qlcg_frame_local(&c->frame,anchor.x,anchor.z);
        double distance,t;
        int segment=qlcg_river_index_nearest(&c->spatial,c->points,q.x,q.z,&distance,&t);
        if (segment<0) continue;
        double clearance=c->detail_clear+24;
        if (c->cell==cell)
            clearance=fmax(clearance,2.2*qlcg_mixd(c->points[segment].width,c->points[segment+1].width,t));
        if (distance<=p->extent+clearance) return 0;
        spare=fmin(spare,distance-clearance-p->extent);
    }
    double low=INFINITY,high=-INFINITY;
    for (int i=0;i<OXBOW_SHAPE_STATIONS;i++) {
        double t=2.0*i/(OXBOW_SHAPE_STATIONS-1)-1;
        Point q=oxbow_arc(p,p->half_angle*t,p->radius);
        Point g=qlcg_frame_coordinates(&p->frame,q.x,q.z);
        double h=qlcg_sample_natural_height(w,(float)g.x,(float)g.z);
        if (!isfinite(h)) return 0;
        low=fmin(low,h); high=fmax(high,h);
    }
    if (low<3 || high-low>fmax(5.5,2.1*p->depth)) return 0;
    p->level=low-.42;
    *room=fmax(0,spare);
    return 1;
}

static int build_oxbow(const MacroWorld *w,int cell,OxbowPlan *p) {
    int receiver=w->receiver[cell];
    double span=w->circumference/w->columns;
    double slope=fmax(0,w->spill[cell]-w->spill[receiver])/(span*fmax(.01,w->height_scale));
    if (slope>.009 || w->uplift[cell]>.64 || w->water[receiver]==1 ||
        w->water[receiver]==2 || qlcg_lake_overlaps_cell(w,cell)) return 0;
    for (int k=0;k<8;k++) {
        int water=w->water[w->neighbors[cell*8+k]];
        if (water==1 || water==2) return 0;
    }
    RiverCurve *c=qlcg_river_curve_at(w,cell);
    if (!c) return 0;
    uint32_t seed=w->opt.seed^((uint32_t)cell*0x6d2b79f5u)^0xc47f293du;
    double total=c->points[c->count-1].arc,arc=total*(.15+.17*qlcg_river_random_next(&seed));
    double a0=arc-fmin(32,arc*.25),a1=arc+fmin(32,(total-arc)*.25);
    Point a=qlcg_river_curve_position(c,a0),b=qlcg_river_curve_position(c,a1),anchor=qlcg_river_curve_position(c,arc);
    double length=hypot(b.x-a.x,b.z-a.z);
    if (length<1) return 0;
    double tx=(b.x-a.x)/length,tz=(b.z-a.z)/length;
    int hi=qlcg_river_curve_arc_end(c,arc);
    double width=fmax(c->points[hi?hi-1:0].width,c->points[hi].width);
    double clearance=fmax(2.28*width,c->detail_clear+36);
    p->frame=c->frame;
    p->type=(int)(qlcg_river_random_next(&seed)*3);
    p->variant=(int)(qlcg_river_random_next(&seed)*4);
    double variant=p->variant/3.0;
    static const double radii[3][2]={{44,80},{82,158},{126,232}};
    static const double angles[3][2]={{150,166},{83,106},{37,58}};
    static const double widths[3][2]={{9,17},{12,24},{8,16}};
    static const double depths[3][2]={{2.5,3.9},{2,3.5},{1.5,2.8}};
    static const double aspect[3]={1,1.14,.89},tips[3]={.26,.39,.48};
    int type=p->type;
    p->radius=qlcg_mixd(radii[type][0],radii[type][1],variant);
    p->half_angle=qlcg_mixd(angles[type][0],angles[type][1],variant)*QLCG_RIVER_PI/180;
    p->half_width=qlcg_mixd(widths[type][0],widths[type][1],variant);
    p->depth=qlcg_mixd(depths[type][0],depths[type][1],variant);
    p->aspect=aspect[type]; p->tip=tips[type];
    uint32_t shape_seed=seed^0x60e2d799u;
    double aspect_limit=fmax(1,p->aspect);
    p->extent=p->radius*aspect_limit+p->half_width+14;
    double side=qlcg_river_random_next(&seed)<.5?-1:1;
    for (int k=0;k<2;k++,side=-side) {
        double offset=clearance+p->extent+26;
        p->x=anchor.x-tz*side*offset; p->z=anchor.z+tx*side*offset;
        p->axis_x=tz*side; p->axis_z=-tx*side;
        double room;
        if (!place_oxbow(w,cell,p,&room)) continue;
        finish_oxbow(w,p,shape_seed,(int)side,room);
        return 1;
    }
    return 0;
}

void qlcg_oxbow_generate(MacroWorld *w,int cell) {
    if (cell<0 || cell>=w->cell_count || w->water[cell]!=3 || !qlcg_river_main_width(w,cell) ||
        w->receiver[cell]<0 || w->receiver[cell]>=w->cell_count) return;
    uint32_t seed=w->opt.seed^((uint32_t)cell*0x85ebca6bu)^0x0db04a61u;
    if (qlcg_river_random_next(&seed)>=.03) return;
    if(!w->oxbow_catalog) {
        w->oxbow_catalog=calloc(1,sizeof(*w->oxbow_catalog)); if(!w->oxbow_catalog) return;
        w->oxbow_catalog->lookup=calloc((size_t)w->cell_count,sizeof(int));
        if(!w->oxbow_catalog->lookup) { free(w->oxbow_catalog); w->oxbow_catalog=NULL; return; }
    }
    struct OxbowCatalog *c=w->oxbow_catalog; if(c->lookup[cell]) return;
    c->lookup[cell]=-1; OxbowPlan plan={0}; RiverBuffer record={0}; int32_t valid=0;
    if(qlcg_river_archive_get(w,RIVER_RECORD_OXBOW,cell,&record)) {
        qlcg_river_values(&record,&valid,(sizeof(valid))/4,4);
        if(valid) qlcg_river_OxbowPlan(&record,&plan);
        if(record.failed||record.at!=record.size) return;
    } else {
        valid=build_oxbow(w,cell,&plan);
        qlcg_river_values(&record,&valid,(sizeof(valid))/4,4);
        if(valid) qlcg_river_OxbowPlan(&record,&plan);
        int stored=qlcg_river_archive_put(w,RIVER_RECORD_OXBOW,cell,&record);
        qlcg_river_buffer_free(&record); if(!stored) return;
    }
    if(!valid) return;
    if(c->count==c->capacity) {
        int capacity=c->capacity?c->capacity*2:16;
        OxbowPlan *p=realloc(c->plans,(size_t)capacity*sizeof(*p)); if(!p) return;
        c->plans=p; c->capacity=capacity;
    }
    c->plans[c->count]=plan; c->lookup[cell]=++c->count;
}

const OxbowPlan *qlcg_oxbow_plan(const MacroWorld *w,int cell) {
    if(!w->oxbow_catalog||cell<0||cell>=w->cell_count) return NULL;
    int i=w->oxbow_catalog->lookup[cell];
    return i>0?w->oxbow_catalog->plans+i-1:NULL;
}

void qlcg_oxbow_free(MacroWorld *w) {
    if(!w->oxbow_catalog) return;
    free(w->oxbow_catalog->lookup); free(w->oxbow_catalog->plans);
    free(w->oxbow_catalog); w->oxbow_catalog=NULL;
}

void qlcg_oxbow_surface(const MacroWorld *w,double wx,double wz,RiverSurfaceSample *out) {
    if (out->kind!=WATER_KIND_DRY) return;
    const OxbowPlan *p=qlcg_oxbow_plan(w,qlcg_macro_cell_at(w,wx,wz));
    if (!p) return;
    Point q=qlcg_frame_local(&p->frame,wx,wz);
    double dx=q.x-p->x,dz=q.z-p->z;
    if (fabs(dx)>p->extent || fabs(dz)>p->extent) return;
    double x=dx*p->axis_x+dz*p->axis_z,z=-dx*p->axis_z+dz*p->axis_x;
    double u=x/p->aspect;
    double a=qlcg_clampd(atan2(z,u),-p->half_angle,p->half_angle);
    double along=(a/p->half_angle+1)*.5;
    double radius=p->radius+oxbow_offset(p->radius_offset,OXBOW_SHAPE_STATIONS,along);
    double taper=qlcg_smooth01d((p->half_angle-fabs(a))/p->tip);
    double inner=(p->half_width+oxbow_offset(p->inner_bank,OXBOW_BANK_STATIONS,along))*taper;
    double outer=(p->half_width+oxbow_offset(p->outer_bank,OXBOW_BANK_STATIONS,along))*taper;
    double width=u*u+z*z>radius*radius?outer:inner;
    Point arc=oxbow_arc(p,a,radius);
    double edge=hypot(q.x-arc.x,q.z-arc.z)-width;
    if (edge>=14) return;
    double rim=p->level+.30;
    if (edge<0) {
        double depth=p->depth*(1+.36*oxbow_downstream(p,a));
        double basin=(depth+.30)*qlcg_smooth01d(fmin(inner,outer)/1.33);
        out->height=(float)(rim-basin*qlcg_smooth01d(-edge/fmax(1e-6,width*.75)));
        if (out->height<p->level-.025) {
            out->kind=WATER_KIND_LAKE;
            out->level=(float)p->level;
            out->decorative=1;
            out->width=(float)(inner+outer);
        }
    } else {
        double berm=rim+.46*qlcg_smooth01d(edge/6);
        out->height=(float)qlcg_mixd(berm,out->height,qlcg_smooth01d((edge-6)/8));
    }
    out->fresh=1;
    out->sediment=(float)fmax(out->sediment,.48*(1-qlcg_smooth01d(fabs(edge)/8)));
    out->damp=(float)fmax(out->damp,.80*(1-qlcg_smooth01d(fmax(0,edge)/14)));
}
