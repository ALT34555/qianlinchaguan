/* 源流断面、汇口与点级采样 */
#include "hydrology_sources.h"
#include "hydrology_archive.h"
#include "surface_detail.h"
#include <math.h>

static double clamp(double x,double a,double b) { return fmax(a,fmin(b,x)); }
static double smooth(double x) { x=clamp(x,0,1); return x*x*(3-2*x); }
static double mix(double a,double b,double t) { return a+(b-a)*t; }

double qlcg_source_receiver_width(const SourceRiverNetwork *n,int kind,int target,double arc,double width) {
    if(!n||!n->has_width_floors) return width;
    double result=width;
    for(int k=0;k<n->edge_count;k++) {
        const SourceRiverEdge *incoming=n->edges+k;
        double floor=incoming->receiver_width_floor;
        if(incoming->target_kind!=kind||incoming->target!=target||floor<=result) continue;
        double lead=fmax(8,3*floor);
        double weight=smooth((arc-incoming->receiver_arc+lead)/lead);
        result=fmax(result,mix(width,floor,weight));
    }
    return result;
}

static double source_width_at(const void *context,int kind,int target,double arc,double width) {
    return qlcg_source_receiver_width(context,kind,target,arc,width);
}
HydrologyWidthQuery qlcg_source_width_query(const SourceRiverNetwork *network) {
    return (HydrologyWidthQuery){network,network&&network->has_width_floors?source_width_at:NULL};
}

void qlcg_source_terminal_pool(const SourceRiverNetwork *n,const SourceRiverEdge *e,RiverSource *s) {
    const RiverVectorPoint *p=e->points+e->count-1,*a=p-1;
    *s=n->sources[e->source]; s->x=p->x; s->z=p->z; s->level=p->level;
    double length=fmax(.01,hypot(p->x-a->x,p->z-a->z));
    s->direction_x=(p->x-a->x)/length; s->direction_z=(p->z-a->z)/length;
    s->connected=0; s->damp_only=0; s->spring_form=9; s->mirror=1;
    qlcg_source_material_set(s,2); qlcg_source_pool_shape(s);
    s->pool_rx=fmax(12,fmin(48,p->width*3)); s->pool_rz=s->pool_rx*.8;
    s->pool_level=p->level; s->pool_depth=fmax(.8,fmin(2.5,p->depth));
}

static void source_water(RiverSurfaceSample *s,double height,double level,double width,double discharge,int kind) {
    s->height=(float)height; s->level=(float)level; s->kind=kind; s->decorative=1;
    s->fresh=1;
    s->channel_depth=0;
    if(kind!=WATER_KIND_RIVER) s->east=s->north=0;
    s->grade=0; s->width=(float)width; s->discharge=(float)discharge;
}
static SurfacePoint point_direction(const MacroWorld *w,const SourceRiverEdge *e,int k) {
    const RiverVectorPoint *p=e->points+k,*a=e->points+(k?k-1:k),*b=e->points+(k+1<e->count?k+1:k);
    double ax=p->wx-a->wx,az=p->wz-a->wz,bx=b->wx-p->wx,bz=b->wz-p->wz;
    ax-=floor(ax/w->circumference+.5)*w->circumference;
    bx-=floor(bx/w->circumference+.5)*w->circumference;
    double al=fmax(1e-9,hypot(ax,az)),bl=fmax(1e-9,hypot(bx,bz));
    double x=ax/al+bx/bl,z=az/al+bz/bl,length=fmax(1e-9,hypot(x,z));
    return (SurfacePoint){x/length,z/length};
}
static double mouth_bed(double main_bed,double source_bed,double blend) {
    double overlap=fmax(0,blend-fabs(main_bed-source_bed));
    return fmin(main_bed,source_bed)-.25*overlap*overlap/fmax(1e-6,blend);
}
void qlcg_source_surface_network(const MacroWorld *w,const SourceRiverNetwork *n,float wx,float wz,float natural,
                                 RiverSurfaceSample *out) {
    if(!n||out->kind==WATER_KIND_SEA||out->kind==WATER_KIND_LAKE) return;
    int main_water=(out->kind==WATER_KIND_RIVER||out->kind==WATER_KIND_FALL)&&!out->decorative&&out->level>out->height+.02f;
    int modern=qlcg_river_archive_terrain(w)>=5,merge_main=0;
    if(main_water) {
        if(!modern) return;
        const HeadwaterPlan *h=qlcg_headwater_plan(w,n->cell);
        merge_main=!h||hypot(h->x,h->z)>1;
    }
    SurfacePoint p=qlcg_frame_local(&n->frame,wx,wz); double best=INFINITY,height=out->height,support=0;
    int channel_source=-1,v14=qlcg_river_archive_terrain(w)>=14,any_wet=0;
    double main_bed=out->height,dry_num=0,dry_den=0,bank_num=0;
    for(int k=0;k<n->edge_count;k++) {
        const SourceRiverEdge *e=n->edges+k; double distance,t;
        if(merge_main&&(e->target_kind!=0||e->join.radius<=0||
                       fabs(p.x-e->join_x)>=e->join.radius||fabs(p.z-e->join_z)>=e->join.radius)) continue;
        int near=qlcg_river_index_nearest(&e->spatial,e->points,p.x,p.z,&distance,&t); if(near<0) continue;
        if(distance>=e->profile.influence) continue;
        const RiverVectorPoint *a=e->points+near,*b=a+1;
        double arc=mix(a->arc,b->arc,t);
        double radius=mix(a->width,b->width,t),bed=natural,grade=0; int fall=0;
        radius=qlcg_source_receiver_width(n,1,e->source,arc,radius);
        double water=qlcg_source_profile_value(&e->profile,arc,&grade,&fall);
        if(!isfinite(water)) continue;
        double depth=mix(a->depth,b->depth,t);
        double mouth_distance=hypot(p.x-e->join_x,p.z-e->join_z);
        JunctionRelief relief={0};
        int mouth=e->join.radius>0&&mouth_distance<e->join.radius&&distance<radius*1.25;
        if(mouth) {
            qlcg_junction_relief(&e->join,p.x-e->join_x,p.z-e->join_z,&relief);
            double spread=(.06+.10*clamp(relief.shelf+relief.branch,0,1))*qlcg_junction_weight(&e->join,mouth_distance);
            radius=fmin(16,radius*(1+spread));
        }
        double supported=natural,section_water=water,sec_radius=radius,sec_depth=depth;
        if(v14&&n->sources[e->source].planned>=SOURCE_REGION_CHANNEL) {
            double smooth_water=water,smooth_dist=distance;
            if(qlcg_river_index_level(&e->spatial,e->points,p.x,p.z,24.0,e->profile.influence,&smooth_water,&smooth_dist)) {
                double core=smooth((distance-radius)/fmax(8.0,radius*2.0+16.0));
                section_water=mix(water,smooth_water,core);
                sec_radius=mix(radius,n->sources[e->source].half_width,core);
                sec_depth=mix(depth,n->sources[e->source].depth,core);
                if(natural<section_water+.65) {
                    double fill=1-smooth((distance-sec_radius*1.5)/fmax(1,e->profile.influence-sec_radius*1.5));
                    supported=mix(natural,section_water+.65,fill); bed=supported;
                }
            }
        } else if(n->sources[e->source].planned>=SOURCE_REGION_CHANNEL&&natural<water+.65) {
            double fill=1-smooth((distance-radius*1.5)/fmax(1,e->profile.influence-radius*1.5));
            supported=mix(natural,water+.65,fill);bed=supported;
        }
        int wet=qlcg_hydrology_section_span(distance,sec_radius,sec_depth,supported,section_water,e->profile.influence,&bed,&water);
        if(mouth&&distance<radius) {
            double detail=depth*(.18*relief.bar+.10*relief.shelf-.40*relief.scour-.16*relief.branch);
            detail=fmin(detail,fmax(0,water-.05-bed));
            bed+=detail*(1-smooth(distance/radius))*qlcg_junction_weight(&e->join,mouth_distance);
            wet=bed<water-.025;
        }
        if(merge_main) {
            if(mouth&&wet&&fabs(water-out->level)<.35) {
                double blend=fmin(.65,depth)*qlcg_junction_weight(&e->join,mouth_distance);
                height=fmin(height,mouth_bed(main_bed,bed,blend));
                out->bank_distance=fmaxf(out->bank_distance,(float)(radius-distance));
            }
            continue;
        }
        double weight=1-smooth((distance-sec_radius)/fmax(1,e->profile.influence-sec_radius));
        if(wet) { height=out->decorative&&out->level>out->height?fmin(height,bed):bed; any_wet=1; }
        else if(v14) {
            if(out->level<=out->height&&!any_wet&&fabs(bed-natural)>1e-6&&weight>0) {
                double mag=weight*fabs(bed-natural),ws=mag*mag;
                dry_num+=(bed-natural)*ws; dry_den+=ws;
                bank_num+=fmin(section_water,bed)*ws;
            }
        } else if(out->level<=out->height&&weight>support&&fabs(bed-natural)>1e-6) {
            height=bed; support=weight;
            out->bank_level=(float)fmin(water,bed);
            out->has_bank_level=1;
        }
        if(wet&&distance/fmax(.1,radius)<best) {
            channel_source=e->source;
            best=distance/fmax(.1,radius);
            source_water(out,height,water,radius*2,n->sources[e->source].discharge,fall?WATER_KIND_FALL:WATER_KIND_RIVER);
            out->grade=(float)grade; out->channel_depth=(float)depth;
            if(qlcg_river_archive_terrain(w)>=10&&fall&&grade>.7)out->waterfall_rock=1;
            SurfacePoint da=point_direction(w,e,near),db=point_direction(w,e,near+1);
            double dx=mix(da.x,db.x,t),dz=mix(da.z,db.z,t);
            double span=fmax(1e-9,hypot(dx,dz));
            out->east=(float)(dx/span); out->north=(float)(-dz/span);
            out->bank_distance=(float)fmax(0,radius-distance); out->lateral=(float)clamp(distance/fmax(1e-6,radius),0,1);
        }
    }
    if(v14&&!merge_main&&!any_wet&&out->level<=out->height&&dry_den>0) {
        height=natural+dry_num/dry_den;
        out->bank_level=(float)(bank_num/dry_den);
        out->has_bank_level=1;
    }
    out->height=(float)height;
    if(merge_main) return;
    for(int k=0;k<n->edge_count;k++) if(n->edges[k].target_kind==SOURCE_TARGET_POOL) {
        RiverSource pool; qlcg_source_terminal_pool(n,n->edges+k,&pool);
        RiverSurfaceSample lake=*out; lake.kind=WATER_KIND_DRY; lake.level=-1e9f;
        qlcg_source_form_surface_mode(&pool,BASIN_TERMINAL,p.x-pool.x,p.z-pool.z,fmax(natural,pool.level),&lake);
        if(lake.kind==WATER_KIND_LAKE||lake.kind==WATER_KIND_STATIC) *out=lake;
    }
    for(int k=0;k<n->source_count;k++) {
        const RiverSource *s=n->sources+k;
        if(s->planned<0||s->planned==SOURCE_REGION_CHANNEL) continue;
        if(s->planned==SOURCE_REGION_MARSH&&channel_source>=0&&channel_source!=k)continue;
        int basin=s->planned==SOURCE_REGION_LAKE||s->planned==SOURCE_REGION_MARSH;
        qlcg_source_form_surface_mode(s,basin?BASIN_SPRING:s->connected?BASIN_HEAD:BASIN_SPRING,
                                      p.x-s->x,p.z-s->z,natural,out);
    }
}




