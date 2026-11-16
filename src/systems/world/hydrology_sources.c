#include "hydrology_oxbow.h"
#include "hydrology_river.h"
#include "hydrology_source_fields.h"
/* 源流、泉眼与湿点的惰性目录 */
#include "hydrology_sources.h"
#include "surface_internal.h"
#include "surface_detail.h"
#include "hydrology_lakes.h"
#include "surface_macro.h"
#include "gen/hydrology_balance.h"
#include "hydrology_archive.h"
#include "hydrology_edit.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SOURCE_PAGE_SIZE 256
static const double PI=3.14159265358979323846;
static double clamp(double x,double a,double b) { return fmax(a,fmin(b,x)); }
static double smooth(double x) { x=clamp(x,0,1); return x*x*(3-2*x); }
static double mix(double a,double b,double t) { return a+(b-a)*t; }
static double random_next(uint32_t *seed) {
    uint32_t t=(*seed+=0x6d2b79f5u);
    t=(t^(t>>15))*(t|1u); t^=t+(t^(t>>7))*(t|61u);
    return (t^(t>>14))/4294967296.0;
}
struct SourceRiverCache {
    uint32_t revision;
    int page_count;
    SourceRiverNetwork ***pages;
};
typedef struct { RiverVectorPoint p; int edge,head,source; double tx,tz; } Target;
typedef struct { int cell,count; RiverVectorPoint points[161]; } MainEdge;
static double main_width(const MacroWorld *w,int i) {
    return w->main_river_width?fmax(w->main_river_width[i],w->river_width[i]):w->river_width[i];
}
static int wet_count(const MacroWorld *w,int cell) {
    double runoff=qlcg_runoff_mm(w->rainfall[cell],w->evaporation[cell]);
    double expected=SOURCE_RIVER_LIMIT*(-expm1(-runoff/300.0));
    uint32_t seed=w->opt.seed^((uint32_t)(cell+1)*0x64656e73u);
    int count=(int)floor(expected);
    return count+(random_next(&seed)<expected-count);
}
int qlcg_source_candidate(const MacroWorld *w,int cell) {
    if(cell<0||cell>=w->cell_count||!w->rainfall||!w->areas||!w->river_width) return 0;
    RiverBuffer stored={0}; if(qlcg_river_archive_get(w,RIVER_RECORD_SOURCE,cell,&stored)) return 1;
    if(w->water[cell]==2) return 1;
    if(w->circumference/w->columns<640||w->height[cell]<=0||w->water[cell]==1||w->water[cell]==2) return 0;
    if(w->rainfall[cell]<=0) return 0;
    const MacroCellPlan *p=qlcg_macro_cell_plan(w,cell);
    return p&&p->source.valid;
}
static void network_free(SourceRiverNetwork *n) {
    if(!n) return;
    for(int k=0;k<n->edge_count;k++) {
        qlcg_river_index_free(&n->edges[k].spatial); free(n->edges[k].points); free(n->edges[k].craft_cells);
        free(n->edges[k].valley_ground);
    }
    for(int k=0;k<n->landform_count;k++) {
        qlcg_river_index_free(&n->landform_edges[k].spatial);free(n->landform_edges[k].points);free(n->landform_edges[k].valley_ground);
    }
    free(n->landform_edges);
    free(n->edges); free(n->sources);
    free(n);
}
void qlcg_source_rivers_free(MacroWorld *w) {
    if(!w->source_rivers) return;
    for(int k=0;k<w->source_rivers->page_count;k++) {
        SourceRiverNetwork **page=w->source_rivers->pages[k]; if(!page) continue;
        for(int i=0;i<SOURCE_PAGE_SIZE;i++) network_free(page[i]);
        free(page);
    }
    free(w->source_rivers->pages);
    free(w->source_rivers); w->source_rivers=NULL;
}
static int network_record(SourceRiverNetwork *n,RiverBuffer *b) {
    int32_t counts[2]={n->source_count,n->edge_count};
    qlcg_river_values(b,counts,(sizeof(counts))/4,4);
    if(b->failed||counts[0]<0||counts[0]>4096||counts[1]<0||counts[1]>counts[0]) return 0;
    n->source_count=counts[0]; n->edge_count=counts[1];
    if(b->reading) {
        n->sources=calloc((size_t)fmax(SOURCE_RIVER_LIMIT,counts[0]+1),sizeof(*n->sources));
        n->edges=calloc((size_t)fmax(SOURCE_RIVER_LIMIT,counts[1]+1),sizeof(*n->edges));
        if(!n->sources||!n->edges) { n->edge_count=0; return 0; }
    }
    for(int k=0;k<n->source_count;k++) qlcg_river_RiverSource(b,&n->sources[k]);
    for(int k=0;k<n->source_count;k++) {
        const RiverSource *s=n->sources+k;
        if(!isfinite(s->x)||!isfinite(s->z)||!isfinite(s->level)||!isfinite(s->half_width)||s->half_width<0||s->half_width>16) return 0;
    }
    for(int k=0;k<n->edge_count;k++) {
        SourceRiverEdge *e=n->edges+k;
        int32_t m[4]={e->count,e->source,e->target,e->target_kind};
        qlcg_river_values(b,m,(sizeof(m))/4,4);
        if(b->failed||m[0]<2||m[0]>HYDROLOGY_PATH_POINTS||m[1]<0||m[1]>=n->source_count||m[3]<0||m[3]>SOURCE_TARGET_CONTINUATION) return 0;
        e->count=m[0]; e->source=m[1]; e->target=m[2]; e->target_kind=m[3];
        qlcg_river_values(b,&e->profile.influence,(sizeof(double))/8,8);
        qlcg_river_RiverJunction(b,&e->join);
        qlcg_river_values(b,&e->join_x,(sizeof(double))/8,8); qlcg_river_values(b,&e->join_z,(sizeof(double))/8,8);
        if(b->reading) { e->points=calloc(e->count,sizeof(*e->points)); if(!e->points) return 0; }
        for(int j=0;j<e->count;j++) {
            RiverVectorPoint *p=e->points+j;
            double v[8]={p->x,p->z,p->arc,p->width,p->level,p->depth,p->fall,p->slope};
            qlcg_river_values(b,v,(sizeof(v))/8,8);
            if(b->reading) {
                for(int d=0;d<8;d++) if(!isfinite(v[d])) return 0;
                if(v[2]<0||v[3]<=0||v[3]>16||v[5]<0||v[5]>2.5||v[6]<0||v[6]>16||
                   (j&&v[2]<=e->points[j-1].arc)) return 0;
                SurfacePoint q=qlcg_frame_coordinates(&n->frame,v[0],v[1]);
                *p=(RiverVectorPoint){q.x,q.z,v[0],v[1],v[2],v[3],0,v[4],v[5],v[6],v[7]};
            }
        }
        if(b->reading) {
            if(!isfinite(e->profile.influence)||e->profile.influence<=0||e->profile.influence>4096) return 0;
            e->profile.count=2; e->profile.path=e->points; e->profile.path_count=e->count;
            if(!qlcg_river_index_build(&e->spatial,e->points,e->count,n->geometry_ready||n->sources[e->source].planned>=SOURCE_REGION_CHANNEL?fmax(544,e->profile.influence):e->profile.influence)) return 0;
        }
    }
    int crafted=0; for(int k=0;k<n->edge_count;k++) if(n->edges[k].craft_id) crafted=1;
    uint32_t next_tag=0;
    if(b->reading&&b->at<=b->size&&b->size-b->at>=4) { RiverBuffer peek=*b;qlcg_river_values(&peek,&next_tag,1,4); }
    if((!b->reading&&crafted)||(b->reading&&next_tag==0x31454352)) {
        uint32_t tag=0x31454352; qlcg_river_values(b,&tag,(sizeof(tag))/4,4); if(tag!=0x31454352) return 0;
        for(int k=0;k<n->edge_count;k++) {
            SourceRiverEdge *e=n->edges+k;
            qlcg_river_values(b,&e->craft_id,(sizeof(e->craft_id))/8,8);
            qlcg_river_values(b,&e->craft_owner,(sizeof(int))/4,4); qlcg_river_values(b,&e->craft_order,(sizeof(int))/4,4);
            qlcg_river_values(b,&e->craft_count,(sizeof(int))/4,4);
            if(b->failed||e->craft_count<0||e->craft_count>HYDROLOGY_PATH_POINTS*2||e->craft_order<0) return 0;
            if(b->reading&&e->craft_count) { e->craft_cells=malloc((size_t)e->craft_count*sizeof(int)); if(!e->craft_cells) return 0; }
            if(e->craft_count) qlcg_river_values(b,e->craft_cells,((size_t)e->craft_count*sizeof(int))/4,4);
        }
    }
    if((!b->reading&&n->valley_ready)||(b->reading&&b->at<b->size)) {
        uint32_t tag=0x31465748;qlcg_river_values(b,&tag,1,4);if(tag!=0x31465748)return 0;
        for(int k=0;k<n->edge_count;k++) {
            SourceRiverEdge *e=n->edges+k;uint32_t valid=e->valley_ground!=NULL;qlcg_river_values(b,&valid,1,4);
            if(b->failed||valid>1)return 0;
            if(valid&&b->reading) { e->valley_ground=malloc((size_t)e->count*sizeof(float));if(!e->valley_ground)return 0; }
            if(valid)qlcg_river_values(b,e->valley_ground,(size_t)e->count,4);
            if(valid)for(int j=0;j<e->count;j++)if(!isfinite(e->valley_ground[j]))return 0;
        }
        n->valley_ready=1;
    }
    return !b->failed;
}
static int network_store(const MacroWorld *w,SourceRiverNetwork *n) {
    RiverBuffer b={0};
    int ok=network_record(n,&b)&&qlcg_river_archive_put((MacroWorld *)w,RIVER_RECORD_SOURCE,n->cell,&b);
    qlcg_river_buffer_free(&b); return ok;
}
int qlcg_river_sources_natural(const MacroWorld *w,int cell,RiverBuffer *record) {
    SourceRiverNetwork *n=calloc(1,sizeof(*n));if(!n) return -1;
    n->cell=cell;n->frame=qlcg_surface_frame(w,cell);
    int result=network_record(n,record)&&record->at==record->size?1:-1;
    for(int k=0;result>0&&k<n->edge_count;k++) if(n->edges[k].craft_id) result=-2;
    network_free(n);return result;
}
int qlcg_source_detach_prepare(const SourceRiverNetwork *old,const unsigned char *detached,RiverBuffer *out) {
    RiverBuffer copy={0}; SourceRiverNetwork *n=calloc(1,sizeof(*n));
    if(!n) return 0;
    n->cell=old->cell; n->frame=old->frame;
    int ok=network_record((SourceRiverNetwork *)old,&copy);
    RiverBuffer read={copy.data,copy.size,0,0,1,0};
    if(ok) ok=network_record(n,&read);
    for(int k=0;ok&&k<n->edge_count;k++) if(detached[k]) {
        n->edges[k].target_kind=SOURCE_TARGET_POOL; n->edges[k].target=n->cell;
        n->edges[k].join=(RiverJunction){0};
    }
    if(ok) ok=network_record(n,out);
    qlcg_river_buffer_free(&copy); network_free(n); return ok;
}

static RiverSurfaceSample sample(const MacroWorld *w,SourceRiverNetwork *n,double x,double z,double *temp) {
    RiverSurfaceSample s={0}; s.height=NAN; s.level=-1e9f;
    SurfacePoint g=qlcg_frame_coordinates(&n->frame,x,z);
    if(qlcg_world_cell_at_wxz(w,(float)g.x,(float)g.z)!=n->cell) return s;
    n->probes++;
    double natural=qlcg_sample_natural_height(w,(float)g.x,(float)g.z);
    LakeShoreField lf=qlcg_lake_field(w,g.x,g.z);
    s.height=qlcg_lake_height(&lf,(float)natural);
    if(s.height<0) { s.kind=WATER_KIND_SEA; s.level=0; }
    else if(lf.lake&&lf.d<0&&lf.level>s.height+.02) { s.kind=WATER_KIND_LAKE; s.level=lf.level; }
    qlcg_river_surface(w,(float)g.x,(float)g.z,s.height,&s);
    qlcg_oxbow_surface(w,g.x,g.z,&s);
    if(temp) {
        SurfaceMacroContext c=qlcg_macro_context(w,g.x,g.z);
        *temp=qlcg_read_macro(w,&c,w->temperature);
    }
    return s;
}
static int spaced(const SourceRiverNetwork *n,double x,double z,double spacing) {
    for(int k=0;k<n->source_count;k++) if(hypot(x-n->sources[k].x,z-n->sources[k].z)<spacing) return 0;
    return 1;
}
static void dimensions_at(const MacroWorld *w,const SourceRiverNetwork *n,double x,double z,int snow,RiverSource *s) {
    SurfacePoint g=qlcg_frame_coordinates(&n->frame,x,z);
    SurfaceMacroContext c=qlcg_macro_context(w,g.x,g.z);
    qlcg_source_dimensions(qlcg_read_macro(w,&c,w->rainfall),qlcg_read_macro(w,&c,w->evaporation),w->columns,snow,s);
}
static int planned_source(const MacroWorld *w,SourceRiverNetwork *n,const SourcePlan *p,RiverSource *s) {
    if(!p->valid) return 0;
    const HeadwaterPlan *h=qlcg_headwater_plan(w,n->cell);
    if(h) {
        s->x=h->x;s->z=h->z;s->level=h->source_level;s->planned=2;
        s->plan_kind=h->kind==HEAD_STEPS?SRC_PLAN_STEP:SRC_PLAN_SPRING;s->direction_x=h->dx;s->direction_z=h->dz;
        dimensions_at(w,n,h->x,h->z,h->kind>=HEAD_SNOW,s);
        s->discharge=fmax(.05,s->discharge);s->half_width=fmax(2.8,s->half_width);
        s->min_half_width=fmin(.5,s->min_half_width);return 1;
    }
    RiverSurfaceSample q=sample(w,n,p->x,p->z,NULL);
    if(q.kind||!isfinite(q.height)||q.height<=0) return 0;
    s->x=p->x; s->z=p->z; s->level=q.height-.8;
    s->planned=1; s->plan_kind=p->kind;
    s->direction_x=p->dx; s->direction_z=p->dz;
    dimensions_at(w,n,p->x,p->z,p->snow,s);
    return 1;
}
static int intersect(SourceRiverEdge *edge,const MainEdge *main,int main_count,
                         const SourceRiverNetwork *n,Target *hit,int geometry) {
    for(int j=1;j<edge->count;j++) {
        RiverVectorPoint a=edge->points[j-1],b=edge->points[j];
        double vx=b.x-a.x,vz=b.z-a.z,best=INFINITY;
        double min_x=fmin(a.x,b.x)-1e-5,max_x=fmax(a.x,b.x)+1e-5;
        double min_z=fmin(a.z,b.z)-1e-5,max_z=fmax(a.z,b.z)+1e-5;
        for(int e=0;e<main_count+n->edge_count;e++) {
            int decorative=e>=main_count;
            if(decorative&&edge->craft_id&&n->edges[e-main_count].craft_id==edge->craft_id) continue;
            if(decorative&&n->edges[e-main_count].target_kind==SOURCE_TARGET_POOL&&n->edges[e-main_count].target<0) continue;
            const RiverVectorPoint *p=decorative?n->edges[e-main_count].points:main[e].points;
            int count=decorative?n->edges[e-main_count].count:main[e].count;
            for(int k=1;k<count;k++) {
                const RiverVectorPoint *c=p+k-1,*d=p+k;
                if((c->x>max_x&&d->x>max_x)||(c->x<min_x&&d->x<min_x)||
                   (c->z>max_z&&d->z>max_z)||(c->z<min_z&&d->z<min_z)) continue;
                double ux=d->x-c->x,uz=d->z-c->z,den=vx*uz-vz*ux;
                if(fabs(den)<1e-9) continue;
                double px=c->x-a.x,pz=c->z-a.z,t=(px*uz-pz*ux)/den,u=(px*vz-pz*vx)/den;
                if(t< -1e-7||t>1+1e-7||u< -1e-7||u>1+1e-7||t>=best) continue;
                best=clamp(t,0,1); u=clamp(u,0,1);
                memset(hit,0,sizeof(*hit)); hit->p.x=a.x+vx*best; hit->p.z=a.z+vz*best;
                hit->p.level=geometry?0:decorative?qlcg_source_profile_value(&n->edges[e-main_count].profile,mix(c->arc,d->arc,u),NULL,NULL):
                    mix(c->level,d->level,u);
                hit->p.depth=mix(c->depth,d->depth,u);
                hit->p.width=mix(c->width,d->width,u); hit->source=decorative;
                hit->edge=decorative?n->edges[e-main_count].source:main[e].cell;
                double length=fmax(1e-9,hypot(ux,uz)); hit->tx=ux/length; hit->tz=uz/length;
            }
        }
        if(isfinite(best)) { edge->points[j]=hit->p; edge->count=j+1; return 1; }
    }
    return 0;
}
static int intersection(SourceRiverEdge *edge,const MainEdge *main,int main_count,const SourceRiverNetwork *n,Target *hit) {
    return intersect(edge,main,main_count,n,hit,0);
}
static int join_distance(const SourceRiverEdge *edge,const Target *hit,double *distance) {
    RiverVectorPoint a=edge->points[edge->count>=3?edge->count-3:0],b=edge->points[edge->count-1];
    double dx=b.x-a.x,dz=b.z-a.z,length=fmax(1e-9,hypot(dx,dz));
    if((dx*hit->tx+dz*hit->tz)/length<.22) return 0;
    double crossing=fabs(dx*hit->tz-dz*hit->tx)/length;
    *distance=hit->p.width/fmax(.25,crossing); return 1;
}
static void configure_spring(const MacroWorld *w,SourceRiverNetwork *n,RiverSource *s,const SourceRiverEdge *e,int k) {
    const RiverVectorPoint *a=e->points,*b=a+1;
    double length=fmax(1e-9,hypot(b->x-a->x,b->z-a->z));
    s->direction_x=(b->x-a->x)/length; s->direction_z=(b->z-a->z)/length; s->surface_level=a->level;
    RiverSurfaceSample behind=sample(w,n,s->x-s->direction_x*4,s->z-s->direction_z*4,NULL);
    RiverSurfaceSample ahead=sample(w,n,s->x+s->direction_x*4,s->z+s->direction_z*4,NULL);
    RiverSurfaceSample left=sample(w,n,s->x-s->direction_z*4,s->z+s->direction_x*4,NULL);
    RiverSurfaceSample right=sample(w,n,s->x+s->direction_z*4,s->z-s->direction_x*4,NULL);
    double slope=isfinite(behind.height)&&isfinite(ahead.height)?fabs(behind.height-ahead.height)/8:0;
    double across=isfinite(left.height)&&isfinite(right.height)?fabs(left.height-right.height)/8:0;
    s->damp_only=hypot(slope,across)>.12;
    uint32_t hash=(uint32_t)(int32_t)floor(s->x*8+.5)*374761393u^
        (uint32_t)(int32_t)floor(s->z*8+.5)*668265263u^(w->opt.seed^(uint32_t)n->cell^(uint32_t)k);
    hash=(hash^(hash>>13))*1274126177u; hash^=hash>>16;
    s->spring_style=s->snowmelt?2:slope>.07?1:0;
    s->mirror=hash&8?-1:1;
    s->spring_radius=fmax(s->snowmelt?1.5:3,fmin(12,a->width*.5+2))*(.9+(hash%101)/500.0);
    if(s->planned==2) {
        const HeadwaterPlan *h=qlcg_headwater_plan(w,n->cell);
        if(h) { s->damp_only=0;s->spring_radius=(h->pool_radius-3)/(1.7*qlcg_source_form(s->spring_form)->pool_scale); }
    }
}
static void configure_pool(const MacroWorld *w,SourceRiverNetwork *n,RiverSource *s) {
    qlcg_source_pool_shape(s);
    s->pool_level=s->level;
    s->damp_only=s->discharge<=0;
    const double offsets[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    double rim=INFINITY;
    for(int k=0;k<4;k++) {
        double x=offsets[k][0]*s->pool_rx,z=offsets[k][1]*s->pool_rz;
        RiverSurfaceSample q=sample(w,n,s->x+x*s->direction_x-z*s->direction_z,
            s->z+x*s->direction_z+z*s->direction_x,NULL);
        if(!isfinite(q.height)||q.kind) s->damp_only=1;
        else rim=fmin(rim,q.height);
    }
    if(isfinite(rim)) s->pool_level=fmin(s->level,rim-.15);
    if(s->level-s->pool_level>fmax(1.2,s->pool_depth*.8)) { s->pool=2; s->damp_only=1; s->pool_level=s->level; }
}
static SourceRiverEdge *new_edge(SourceRiverNetwork *n) {
    if(n->edge_count>=SOURCE_RIVER_LIMIT) return NULL;
    SourceRiverEdge *e=n->edges+n->edge_count;
    if(!e->points) e->points=calloc(HYDROLOGY_PATH_POINTS,sizeof(*e->points));
    return e->points?e:NULL;
}
static SourceRiverNetwork *network_pack(SourceRiverNetwork *n) {
    for(int k=n->edge_count;k<SOURCE_RIVER_LIMIT;k++) {
        free(n->edges[k].points); qlcg_river_index_free(&n->edges[k].spatial);
        free(n->edges[k].valley_ground);
    }
    for(int k=0;k<n->edge_count;k++) {
        SourceRiverEdge *e=n->edges+k;
        RiverVectorPoint *p=realloc(e->points,(size_t)e->count*sizeof(*p)); if(p) e->points=p;
        e->profile.path=e->points;
        RiverBinEntry *bins=realloc(e->spatial.entries,(size_t)e->spatial.count*sizeof(*bins));
        if(bins) { e->spatial.entries=bins; e->spatial.capacity=e->spatial.count; }
    }
    if(!n->edge_count) { free(n->edges); n->edges=NULL; }
    else { SourceRiverEdge *p=realloc(n->edges,(size_t)n->edge_count*sizeof(*p)); if(p) n->edges=p; }
    if(!n->source_count) { free(n->sources); n->sources=NULL; }
    else { RiverSource *p=realloc(n->sources,(size_t)n->source_count*sizeof(*p)); if(p) n->sources=p; }
    return n;
}
static void configure_join(SourceRiverEdge *e,const Target *hit,uint32_t seed) {
    qlcg_junction_configure(&e->join,seed,hit->tx,hit->tz);
    e->join_x=e->points[e->count-1].x; e->join_z=e->points[e->count-1].z;
    e->join.radius=fmin(128,fmax(16,e->points[e->count-1].width*6));
}
static int read_main_edge(const MacroWorld *w,const SurfaceFrame *frame,int cell,MainEdge *edge) {
    RiverPathPoint path[161];
    int count=qlcg_river_path(w,cell,path,161);
    if(count<2) return 0;
    edge->cell=cell; edge->count=count;
    for(int j=0;j<count;j++) {
        SurfacePoint p=qlcg_frame_local(frame,path[j].wx,path[j].wz);
        edge->points[j]=(RiverVectorPoint){path[j].wx,path[j].wz,p.x,p.z,path[j].arc,path[j].half_width,0,path[j].level,path[j].depth};
    }
    return count;
}
static int shore_edge(const MacroWorld *w,SourceRiverNetwork *n,RiverSource *s,int id,
                      const MainEdge *main,int main_count) {
    SurfacePoint center=qlcg_frame_coordinates(&n->frame,0,0);
    LakeShoreField lake=qlcg_lake_field(w,center.x,center.z);
    if(!lake.lake||lake.d>=0||lake.bed>=0) {
        SurfacePoint from=qlcg_frame_coordinates(&n->frame,s->x,s->z);
        if(!qlcg_lake_channel_target(w,n->cell,from.x,from.z,&center.x,&center.z) ||
            qlcg_world_cell_at_wxz(w,(float)center.x,(float)center.z)!=n->cell) return 0;
        lake=qlcg_lake_field(w,center.x,center.z);
    }
    SurfacePoint target=qlcg_frame_local(&n->frame,center.x,center.z);
    double dx=target.x-s->x,dz=target.z-s->z,length=hypot(dx,dz);
    if(!lake.lake||lake.d>=0||lake.bed>=0||s->level<=lake.level+.8||length<64||s->discharge<=0) return 0;
    double lo=0,hi=1;
    for(int k=0;k<12&&(hi-lo)*length>2;k++) {
        double t=(lo+hi)*.5;
        SurfacePoint g=qlcg_frame_coordinates(&n->frame,s->x+dx*t,s->z+dz*t);
        LakeShoreField q=qlcg_lake_field(w,g.x,g.z);
        if(q.lake&&q.d<0&&q.bed<0&&q.d*q.bed>.02) hi=t; else lo=t;
    }
    double t=fmin(1,hi+fmax(4,s->half_width)/length);
    SourceRiverEdge *e=new_edge(n); if(!e) return 0;
    e->count=qlcg_source_template(s->x,s->z,s->x+dx*t,s->z+dz*t,w->opt.seed^(uint32_t)n->cell^(uint32_t)id,0,e->points);
    int wet=0;
    for(int k=1;k<e->count;k++) {
        SurfacePoint g=qlcg_frame_coordinates(&n->frame,e->points[k].x,e->points[k].z);
        if(qlcg_world_cell_at_wxz(w,(float)g.x,(float)g.z)!=n->cell) return 0;
        LakeShoreField q=qlcg_lake_field(w,g.x,g.z);
        if(q.lake&&q.d<0&&q.bed<0&&q.d*q.bed>.02) {
            e->count=k+1; e->points[k].level=q.level; e->points[k].width=s->half_width*2.5;
            wet=1; break;
        }
    }
    if(!wet) return 0;
    Target hit={0}; int target_kind=SOURCE_TARGET_LAKE,receiver=n->cell;
    double mouth=s->half_width*2;
    if(intersection(e,main,main_count,n,&hit)) {
        if(!join_distance(e,&hit,&mouth)) { n->rejected++; return 0; }
        target_kind=hit.source; receiver=hit.edge;
    }
    int count=qlcg_source_profile(w,&n->frame,n->cell,s,e->points,e->count,target_kind,mouth,&e->profile);
    if(count<2) return 0; e->count=count;
    if(target_kind!=SOURCE_TARGET_LAKE) configure_join(e,&hit,s->seed);
    if(!qlcg_river_index_build(&e->spatial,e->points,e->count,e->profile.influence)) { qlcg_river_index_free(&e->spatial); return 0; }
    e->source=id; e->target=receiver; e->target_kind=target_kind; s->connected=1; s->level=e->points[0].level;
    configure_spring(w,n,s,e,id); n->edge_count++; return 1;
}
static void lake_sources(const MacroWorld *w,SourceRiverNetwork *n) {
    int cell=n->cell,interior=1;
    for(int k=0;k<8;k++) {
        int i=w->neighbors[cell*8+k];
        if(w->water[i]!=2||w->lake_id[i]!=w->lake_id[cell]) interior=0;
    }
    if(interior) return;
    RiverDetailCandidates candidates;
    if(!qlcg_river_detail_candidates(w,cell,&candidates)) return;
    MainEdge *main=NULL;
    int main_count=0;
    if(candidates.count) {
        main=malloc((size_t)candidates.count*sizeof(*main));
        if(!main) return;
        for(int k=0;k<candidates.count;k++)
            if(read_main_edge(w,&n->frame,candidates.edges[k],main+main_count)) main_count++;
    }
    double size=w->circumference/(double)w->columns;
    int count=(int)fmax(8,wet_count(w,cell));
    uint32_t rng=w->opt.seed^((uint32_t)(cell+1)*0x315acf73u);
    for(int k=0;k<count;k++) for(int attempt=0;attempt<8;attempt++) {
        double x=(random_next(&rng)-.5)*size*.84,z=(random_next(&rng)-.5)*size*.84;
        if(!spaced(n,x,z,size*.07)) continue;
        RiverSurfaceSample s=sample(w,n,x,z,NULL);
        if(!isfinite(s.height)||s.kind==WATER_KIND_SEA||s.height<0) continue;
        random_next(&rng);
        if(!s.kind) {
            int id=n->source_count++;
            RiverSource *source=n->sources+id;
            source->x=x; source->z=z; source->level=s.height-.8;
            source->planned=1; source->plan_kind=SRC_PLAN_LAKE_SHORE;
            dimensions_at(w,n,x,z,0,source);
            qlcg_source_form_choose(w,n,source,id);
            if(!shore_edge(w,n,source,id,main,main_count)) configure_pool(w,n,source);
        }
        break;
    }
    free(main);
}
static void mountain_springs(const MacroWorld *w,SourceRiverNetwork *n) {
    HeadwaterPlan h=*qlcg_headwater_plan(w,n->cell);
    RiverSource pool=n->sources[0];
    for(int k=0;k<h.springs&&n->source_count<SOURCE_RIVER_LIMIT;k++) {
        double angle=-1.12+2.24*k/(h.springs-1)+h.skew*.2;
        double reach=h.radius*(.61+.06*((h.seed>>(k*3))&7)/7.0);
        if(h.design) { angle=h.shape.angles[k];reach=h.radius*h.shape.reaches[k]; }
        double along=-cos(angle)*reach,side=sin(angle)*reach;
        RiverSource source={0};
        source.x=pool.x+h.dx*along-h.dz*side;source.z=pool.z+h.dz*along+h.dx*side;
        SurfacePoint g=qlcg_frame_coordinates(&n->frame,source.x,source.z);
        if(qlcg_world_cell_at_wxz(w,(float)g.x,(float)g.z)!=n->cell)continue;
        source.level=qlcg_sample_natural_height(w,(float)g.x,(float)g.z)-.6;
        if(source.level<=pool.level+.8)continue;
        source.planned=3;source.plan_kind=SRC_PLAN_HANGING;
        dimensions_at(w,n,source.x,source.z,h.kind>=HEAD_SNOW,&source);
        source.discharge=fmax(.05,source.discharge);source.half_width=3.4;
        if(h.design)source.half_width=2.8+1.4*((h.seed>>(k*4))&15)/15.0;
        source.depth=fmax(.65,source.depth);source.min_half_width=.5;
        int id=n->source_count;
        qlcg_source_form_choose(w,n,&source,id);
        SourceRiverEdge *e=new_edge(n);if(!e)break;
        double join_x=pool.x,join_z=pool.z;
        if(h.design) {
            double side=sin(angle)*h.pool_radius*.18,back=-h.pool_radius*.25;
            join_x+=h.dx*back-h.dz*side;join_z+=h.dz*back+h.dx*side;
        }
        e->count=qlcg_source_template(source.x,source.z,join_x,join_z,h.seed^(uint32_t)k,0,e->points);
        if(e->count<2)continue;
        e->points[e->count-1].level=pool.level;
        int count=qlcg_source_profile(w,&n->frame,n->cell,&source,e->points,e->count,1,h.pool_radius*2+16,&e->profile);
        if(count<2) { n->rejected++;continue; }
        e->count=count;e->source=id;e->target=0;e->target_kind=1;
        Target target={0};target.tx=pool.direction_x;target.tz=pool.direction_z;
        configure_join(e,&target,source.seed);
        if(!qlcg_river_index_build(&e->spatial,e->points,e->count,e->profile.influence))continue;
        source.connected=1;source.level=e->points[0].level;
        configure_spring(w,n,&source,e,id);
        source.damp_only=0;source.spring_radius=h.design?3.2+2.8*((h.seed>>(k*3))&7)/7.0:4.5;
        n->sources[n->source_count++]=source;n->edge_count++;
    }
}
static Target region_receiver(const SourceRiverNetwork *n,int parent,double fraction) {
    const SourceRiverEdge *e=n->edges+parent;
    int j=(int)(fraction*(e->count-1));j=(int)clamp(j,0,e->count-2);
    Target t={0};t.p=e->points[j];t.p.level=qlcg_source_profile_value(&e->profile,t.p.arc,NULL,NULL);
    t.source=1;t.edge=e->source;
    double dx=e->points[j+1].x-t.p.x,dz=e->points[j+1].z-t.p.z,length=fmax(1e-9,hypot(dx,dz));
    t.tx=dx/length;t.tz=dz/length;return t;
}
static int region_channel(const MacroWorld *w,SourceRiverNetwork *n,const HeadwaterPlan *h,
                           RiverSource source,Target target,int order) {
    int id=n->source_count;SourceRiverEdge *e=new_edge(n);if(!e)return 0;
    qlcg_source_form_choose(w,n,&source,id);
    if(source.planned==SOURCE_REGION_SPRING)source.spring_form=20+(source.seed%4);
    if(source.planned==SOURCE_REGION_LAKE||source.planned==SOURCE_REGION_MARSH)source.spring_form=16+(source.seed%8);
    e->count=qlcg_source_template(source.x,source.z,target.p.x,target.p.z,h->seed^(uint32_t)(order*193),0,e->points);
    if(e->count<2)return 0;
    Target crossing={0};if(n->edge_count&&intersection(e,NULL,0,n,&crossing))target=crossing;
    if(order)source.level=fmax(source.level,target.p.level+1);
    e->points[e->count-1].level=target.p.level;
    if(h->region==HEAD_REGION_LAKE)source.pool_rx=h->pool_radius;
    double mouth=h->region==HEAD_REGION_LAKE?h->pool_radius*2.6:target.p.width*2+12;
    int count=qlcg_source_profile(w,&n->frame,n->cell,&source,e->points,e->count,1,mouth,&e->profile);
    if(count<2) { n->rejected++;return 0; }
    e->count=count;e->source=id;e->target=target.edge;e->target_kind=target.source;
    configure_join(e,&target,source.seed);
    if(!qlcg_river_index_build(&e->spatial,e->points,e->count,source.planned>=SOURCE_REGION_CHANNEL?fmax(544,e->profile.influence):e->profile.influence))return 0;
    source.connected=1;source.level=source.surface_level=e->points[0].level;
    double dx=e->points[1].x-source.x,dz=e->points[1].z-source.z,length=fmax(1e-9,hypot(dx,dz));
    source.direction_x=dx/length;source.direction_z=dz/length;
    source.damp_only=source.planned==SOURCE_REGION_CHANNEL;source.spring_radius=4+(source.seed%5);
    if(source.planned==SOURCE_REGION_LAKE||source.planned==SOURCE_REGION_MARSH) {
        qlcg_source_pool_shape(&source);source.pool=source.planned==SOURCE_REGION_LAKE?3:1;
        source.pool_rx=source.planned==SOURCE_REGION_LAKE?fmin(h->pool_radius,e->points[e->count-1].arc*.12):clamp(h->radius*.025,12,100);
        if(source.planned==SOURCE_REGION_MARSH)source.pool_rx=fmin(source.pool_rx,e->points[e->count-1].arc*.25);
        source.pool_rz=source.pool_rx*(.55+.30*((source.seed>>8)&255)/255.0);
        source.pool_yaw=atan2(source.direction_z,source.direction_x);source.pool_level=source.level;
        source.pool_depth=source.planned==SOURCE_REGION_LAKE?2.5:.4;
    }
    n->sources[n->source_count++]=source;n->edge_count++;return 1;
}
static void region_network(const MacroWorld *w,SourceRiverNetwork *n,const HeadwaterPlan *h,Target origin) {
    static const double widths[HEAD_REGION_COUNT]={6,4,5,11,3.2,8,4};
    RiverSource root={0};root.x=h->x;root.z=h->z;root.level=h->source_level;
    root.planned=h->region==HEAD_REGION_SPRING?SOURCE_REGION_SPRING:
        h->region==HEAD_REGION_LAKE?SOURCE_REGION_LAKE:SOURCE_REGION_CHANNEL;
    root.plan_kind=h->region==HEAD_REGION_GORGE?SRC_PLAN_HANGING:SRC_PLAN_GENTLE;
    dimensions_at(w,n,root.x,root.z,h->region==HEAD_REGION_SNOW,&root);
    root.discharge=fmax(.05,root.discharge);root.half_width=widths[h->region];
    root.min_half_width=.5;root.depth=h->region==HEAD_REGION_MARSH?.45:1.2;
    if(!region_channel(w,n,h,root,origin,0))return;
    uint32_t rng=h->seed^0x77617472u;
    for(int k=1;k<h->springs&&n->source_count<SOURCE_RIVER_LIMIT;k++) {
        int parent=0;
        if((h->region==HEAD_REGION_NETWORK&&k>=5)||(h->region==HEAD_REGION_MARSH&&k>=4))
            parent=1+(k-(h->region==HEAD_REGION_NETWORK?5:4))%3;
        if(h->region==HEAD_REGION_GORGE&&k==3)parent=1;
        if(parent>=n->edge_count)parent=0;
        double t=parent?.25+.20*random_next(&rng):.16+.18*((k-1)%4);
        if(h->region==HEAD_REGION_LAKE)t=0;
        Target target=region_receiver(n,parent,t);
        double along=-h->radius*(parent?.10+.08*random_next(&rng):.14+.13*random_next(&rng));
        double side=(k&1?1:-1)*h->radius*(parent?.10+.09*random_next(&rng):.18+.17*random_next(&rng));
        RiverSource source={0};source.x=target.p.x+target.tx*along-target.tz*side;
        source.z=target.p.z+target.tz*along+target.tx*side;
        SurfacePoint g=qlcg_frame_coordinates(&n->frame,source.x,source.z);
        if(qlcg_world_cell_at_wxz(w,(float)g.x,(float)g.z)!=n->cell)continue;
        source.level=qlcg_sample_natural_height(w,(float)g.x,(float)g.z)-.6;
        source.planned=h->region==HEAD_REGION_SPRING?SOURCE_REGION_SPRING:
            h->region==HEAD_REGION_MARSH&&k>=4?SOURCE_REGION_MARSH:SOURCE_REGION_CHANNEL;
        source.level=fmax(source.level,target.p.level+fmax(1,h->radius*.003));
        source.plan_kind=root.plan_kind;
        dimensions_at(w,n,source.x,source.z,h->region==HEAD_REGION_SNOW,&source);
        source.discharge=fmax(.05,source.discharge);source.half_width=fmax(2.8,widths[h->region]*(.55+.30*random_next(&rng)));
        source.min_half_width=.5;source.depth=root.depth;
        region_channel(w,n,h,source,target,k);
    }
}
static SourceRiverNetwork *network_build(const MacroWorld *w,int cell) {
    SourceRiverNetwork *n=calloc(1,sizeof(*n)); if(!n) return NULL;
    n->sources=calloc(SOURCE_RIVER_LIMIT,sizeof(*n->sources));
    n->edges=calloc(SOURCE_RIVER_LIMIT,sizeof(*n->edges));
    if(!n->sources||!n->edges) { network_free(n); return NULL; }
    n->cell=cell; n->frame=qlcg_surface_frame(w,cell);
    const MacroCellPlan *cached=qlcg_macro_cell_plan(w,cell);
    MacroCellPlan plan=cached?*cached:(MacroCellPlan){0};
    if(w->water[cell]==2) { lake_sources(w,n); return network_pack(n); }
    double size=w->circumference/(double)w->columns;
    int count=wet_count(w,cell);
    const HeadwaterPlan *headwater=qlcg_headwater_plan(w,cell);
    int springs=headwater?headwater->springs:0;
    count=(int)fmin(count,SOURCE_RIVER_LIMIT-springs);
    if(plan.head&&count<1) count=1;
    if(!count) return network_pack(n);
    if(!qlcg_macro_cell_has_water(w,cell)) {
        if(planned_source(w,n,&plan.source,n->sources)) {
            n->source_count=1; qlcg_source_form_choose(w,n,n->sources,0); configure_pool(w,n,n->sources);
        }
        return network_pack(n);
    }
    MainEdge *main=calloc(9,sizeof(*main)); Target *targets=calloc(400,sizeof(*targets));
    if(!main||!targets) { free(main); free(targets); network_free(n); return NULL; }
    int main_count=0,target_count=0,head=plan.head,head_edge=-1; Target origin={0}; int has_origin=0;
    for(int k=-1;k<8;k++) {
        int i=k<0?cell:w->neighbors[cell*8+k],duplicate=0;
        for(int j=0;j<main_count;j++) if(main[j].cell==i) duplicate=1;
        if(duplicate||!main_width(w,i)) continue;
        MainEdge *edge=main+main_count;
        int count=read_main_edge(w,&n->frame,i,edge); if(!count) continue;
        main_count++;
        if(i==cell&&head) {
            origin.p=edge->points[0]; origin.edge=i; origin.head=1; has_origin=1; head_edge=main_count-1;
            double dx=edge->points[1].x-origin.p.x,dz=edge->points[1].z-origin.p.z,span=fmax(1,hypot(dx,dz));
            origin.tx=dx/span; origin.tz=dz/span;
        }
        for(int j=0;j<count&&target_count<400;j+=4) {
            RiverVectorPoint a=edge->points[j>=2?j-2:0],b=edge->points[j+2<count?j+2:count-1];
            double dx=b.x-a.x,dz=b.z-a.z,length=hypot(dx,dz); if(!length) continue;
            targets[target_count++]=(Target){edge->points[j],i,0,0,dx/length,dz/length};
        }
    }
    double spacing=head?fmax(96,fmin(1200,size*.025)):fmax(64,size*.12);
    headwater=qlcg_headwater_plan(w,cell);
    if(headwater&&headwater->design==2&&has_origin) {
        HeadwaterPlan region=*headwater;region_network(w,n,&region,origin);
        free(main);free(targets);return network_pack(n);
    }
    uint32_t rng=w->opt.seed^((uint32_t)(cell+1)*0x45d9f3bu);
    for(int k=0;k<count;k++) {
        RiverSource source={0}; Target preferred={0}; int found=0,has_preferred=0;
        if(!k) found=planned_source(w,n,&plan.source,&source);
        if(found&&source.planned==2&&has_origin) { preferred=origin;has_preferred=1; }
        for(int attempt=0;!found&&attempt<8;attempt++) {
            Target entry={0}; int entry_tangent=0;
            if(head_edge>=0) {
                MainEdge *edge=main+head_edge;
                int j=1+(k%6)*4+attempt%3; if(j>edge->count-2) j=edge->count-2;
                entry.p=edge->points[j]; entry.edge=cell; entry.head=1;
                double dx=edge->points[j+1].x-edge->points[j].x,dz=edge->points[j+1].z-edge->points[j].z;
                double span=fmax(1,hypot(dx,dz)); entry.tx=dx/span; entry.tz=dz/span; entry_tangent=1;
            }
            else if(target_count) { entry=targets[(int)(random_next(&rng)*target_count)]; entry_tangent=1; }
            else break;
            double side=random_next(&rng)<.5?-1:1,angle=.56+random_next(&rng)*.48;
            double length=fmin(size*.4,fmax(96,spacing*(.6+random_next(&rng)*.8)));
            if(head) length=fmin(size*.32,fmax(192,spacing*(1.2+random_next(&rng)*.8+attempt*.25)));
            double tx=entry_tangent?entry.tx:random_next(&rng)-.5,tz=entry_tangent?entry.tz:random_next(&rng)-.5;
            double tl=hypot(tx,tz); if(!tl) tl=1;
            double x=entry.p.x-tx/tl*length*cos(angle)-tz/tl*side*length*sin(angle);
            double z=entry.p.z-tz/tl*length*cos(angle)+tx/tl*side*length*sin(angle);
            if(!spaced(n,x,z,spacing)) continue;
            RiverSurfaceSample s=sample(w,n,x,z,NULL);
            if(s.kind||!isfinite(s.height)||s.height<0) continue;
            source.x=x; source.z=z; source.level=s.height-.8;
            dimensions_at(w,n,x,z,0,&source); random_next(&rng);
            preferred=entry; has_preferred=entry_tangent; found=1;
        }
        if(!found) continue;
        int id=n->source_count; n->sources[id]=source; RiverSource *s=n->sources+id; n->source_count++;
        qlcg_source_form_choose(w,n,s,id);
        if(s->discharge<=0) continue;
        Target chosen[3]={0}; double distances[3]={INFINITY,INFINITY,INFINITY}; int chosen_count=0;
        if(has_preferred) { chosen[0]=preferred; chosen_count=1; }
        for(int j=-1;j<target_count+n->edge_count*49;j++) {
            Target q={0};
            if(j<0) { if(!has_origin) continue; q=origin; }
            else if(j<target_count) q=targets[j];
            else {
                int ej=(j-target_count)/49,pj=((j-target_count)%49+1)*4;
                if(ej>=n->edge_count||pj>=n->edges[ej].count) continue;
                q.p=n->edges[ej].points[pj]; q.source=1; q.edge=n->edges[ej].source;
            }
            double distance=hypot(q.p.x-s->x,q.p.z-s->z);
            if(q.p.level>=s->level-.8||distance<64) continue;
            if(q.head) distance=-1;
            int begin=has_preferred?1:0;
            for(int pos=begin;pos<3;pos++) if(distance<distances[pos]) {
                for(int d=2;d>pos;d--) { chosen[d]=chosen[d-1]; distances[d]=distances[d-1]; }
                chosen[pos]=q; distances[pos]=distance; break;
            }
        }
        for(int j=has_preferred?1:0;j<3;j++) if(isfinite(distances[j])) chosen_count=j+1;
        if(s->planned==2&&has_origin) { chosen[0]=chosen[1]=chosen[2]=origin;chosen_count=3; }
        for(int attempt=0;attempt<chosen_count;attempt++) {
            SourceRiverEdge *edge=new_edge(n); Target hit={0}; if(!edge) break;
            double mouth=0;
            edge->count=qlcg_source_template(s->x,s->z,chosen[attempt].p.x,chosen[attempt].p.z,
                w->opt.seed^(uint32_t)cell^(uint32_t)k,attempt,edge->points);
            int valid=intersection(edge,main,main_count,n,&hit);
            for(int j=0;valid&&j<edge->count;j++) {
                SurfacePoint g=qlcg_frame_coordinates(&n->frame,edge->points[j].x,edge->points[j].z);
                if(qlcg_world_cell_at_wxz(w,(float)g.x,(float)g.z)!=cell) valid=0;
                RiverSurfaceSample lake={0}; lake.height=1e9f; lake.level=-1e9f;
                qlcg_oxbow_surface(w,g.x,g.z,&lake);
                if(valid&&lake.kind==WATER_KIND_LAKE) {
                    hit.p=edge->points[j]; hit.p.level=lake.level; hit.p.width=fmin(16,lake.width*.5);
                    hit.source=SOURCE_TARGET_LAKE; hit.edge=cell;
                    edge->points[j]=hit.p; edge->count=j+1; break;
                }
            }
            if(valid) {
                if(hit.source==SOURCE_TARGET_LAKE) mouth=s->half_width*2;
                else valid=join_distance(edge,&hit,&mouth);
            }
            if(valid) {
                int count=qlcg_source_profile(w,&n->frame,cell,s,edge->points,edge->count,hit.source,mouth,&edge->profile);
                valid=count>=2; if(valid) edge->count=count;
            }
            if(!valid) { n->rejected++; continue; }
            if(hit.source!=SOURCE_TARGET_LAKE) configure_join(edge,&hit,s->seed);
            edge->source=id; edge->target=hit.edge; edge->target_kind=hit.source;
            if(!qlcg_river_index_build(&edge->spatial,edge->points,edge->count,s->planned>=SOURCE_REGION_CHANNEL?fmax(544,edge->profile.influence):edge->profile.influence)) {
                qlcg_river_index_free(&edge->spatial); continue;
            }
            s->connected=1; s->level=edge->points[0].level;
            configure_spring(w,n,s,edge,k); n->edge_count++; break;
        }
        if(s->planned==2&&!s->connected&&has_origin) {
            qlcg_headwater_compact(w,cell);
            s->x=origin.p.x;s->z=origin.p.z;s->level=s->surface_level=origin.p.level;
            s->connected=1;s->damp_only=0;
            const HeadwaterPlan *h=qlcg_headwater_plan(w,n->cell);
            s->spring_radius=((h?h->pool_radius:12)-3)/(1.7*qlcg_source_form(s->spring_form)->pool_scale);
        }
    }
    if(springs&&n->source_count&&n->sources[0].connected) mountain_springs(w,n);
    for(int k=0;k<n->source_count;k++)
        if(!n->sources[k].connected&&(n->sources[k].planned||!n->sources[k].snowmelt)) configure_pool(w,n,n->sources+k);
    free(main); free(targets); return network_pack(n);
}


static void prepare_receiver_widths(const MacroWorld *w,SourceRiverNetwork *n) {
    if(qlcg_river_archive_terrain(w)<5) return;
    for(int k=n->edge_count-1;k>=0;k--) {
        SourceRiverEdge *incoming=n->edges+k;
        if(incoming->craft_id||incoming->join.radius<=0) continue;
        const RiverVectorPoint *end=incoming->points+incoming->count-1;
        double arc,receiver_width=0;
        if(incoming->target_kind==0) {
            if(!qlcg_river_join_station(w,incoming->target,end->wx,end->wz,&arc)) continue;
        } else if(incoming->target_kind==1) {
            const SourceRiverEdge *receiver=NULL;
            for(int j=k-1;j>=0;j--)
                if(n->edges[j].source==incoming->target) { receiver=n->edges+j; break; }
            if(!receiver||receiver->craft_id) continue;
            double distance,t;
            int near=qlcg_river_index_nearest(&receiver->spatial,receiver->points,end->x,end->z,&distance,&t);
            if(near<0) continue;
            arc=mix(receiver->points[near].arc,receiver->points[near+1].arc,t);
            receiver_width=mix(receiver->points[near].width,receiver->points[near+1].width,t);
        } else continue;
        incoming->receiver_arc=arc;
        double arriving_width=qlcg_source_receiver_width(n,1,incoming->source,end->arc,end->width);
        incoming->receiver_width_floor=fmax(arriving_width,receiver_width);
        n->has_width_floors=1;
    }
}

static SourceRiverNetwork **network_slot(const MacroWorld *w,int cell) {
    MacroWorld *mutable=(MacroWorld *)w;
    if(w->source_rivers&&w->source_rivers->revision!=w->surface_revision)qlcg_source_rivers_free(mutable);
    if(!w->source_rivers) {
        mutable->source_rivers=calloc(1,sizeof(*w->source_rivers));if(!w->source_rivers)return NULL;
        mutable->source_rivers->revision=w->surface_revision;
        mutable->source_rivers->page_count=(w->cell_count+SOURCE_PAGE_SIZE-1)/SOURCE_PAGE_SIZE;
        mutable->source_rivers->pages=calloc(mutable->source_rivers->page_count,sizeof(*mutable->source_rivers->pages));
        if(!mutable->source_rivers->pages) { free(mutable->source_rivers);mutable->source_rivers=NULL;return NULL; }
    }
    int page=cell/SOURCE_PAGE_SIZE;
    if(!w->source_rivers->pages[page])w->source_rivers->pages[page]=calloc(SOURCE_PAGE_SIZE,sizeof(**w->source_rivers->pages));
    return w->source_rivers->pages[page]?w->source_rivers->pages[page]+cell%SOURCE_PAGE_SIZE:NULL;
}
static void geometry_view(const MacroWorld *w,SourceRiverNetwork *n) {
    SourceRiverEdge *edges=n->landform_edges?n->landform_edges:n->edges;
    int count=n->landform_edges?n->landform_count:n->edge_count;
    n->geometry.count=count<HEAD_BRANCH_LIMIT?count:HEAD_BRANCH_LIMIT;
    for(int k=0;k<n->geometry.count;k++) {
        n->geometry.points[k]=edges[k].points;n->geometry.sizes[k]=edges[k].count;
        n->geometry.indices[k]=&edges[k].spatial;
        n->geometry.ground[k]=edges[k].valley_ground;
    }
    if(n->geometry_ready&&qlcg_river_archive_terrain(w)>=11) {
        const HeadwaterPlan *stored=qlcg_headwater_plan(w,n->cell);
        if(stored&&stored->design==3) {
            HeadwaterPlan plan=*stored;
            for(int k=0;k<n->geometry.count;k++)if(n->geometry.backdrop[k]<=0) {
                const RiverVectorPoint *a=n->geometry.points[k];
                n->geometry.backdrop[k]=qlcg_headwater_backdrop(w,&plan,a->wx,a->wz,qlcg_natural_base_height(w,a->wx,a->wz));
            }
        }
    }
}
static uint64_t geometry_hash(const SourceRiverNetwork *n) {
    uint64_t hash=1469598103934665603ull^(uint64_t)n->edge_count;
    for(int k=0;k<n->edge_count;k++) {
        const SourceRiverEdge *e=n->edges+k;hash=((hash^(uint64_t)e->count)*1099511628211ull)^e->craft_id;
        for(int j=0;j<e->count;j++) {
            uint64_t x,z;memcpy(&x,&e->points[j].x,8);memcpy(&z,&e->points[j].z,8);
            hash=((hash^x)*1099511628211ull^z)*1099511628211ull;
        }
    }
    return hash;
}
static double fall_offset(const MacroWaterfall *fall,double arc) {
    if(fall->state!=2||arc>=fall->end)return 0;
    return qlcg_macro_fall_level(fall,arc,0,fall->top);
}
static int valley_ground(const MacroWorld *w,const HeadwaterPlan *h,SourceRiverEdge *e,double end,int restore,double *backdrop) {
    e->valley_ground=malloc((size_t)e->count*sizeof(*e->valley_ground));if(!e->valley_ground)return 0;
    double previous=(float)end;
    double grade=fmax(h->grade,2/fmax(1,e->points[e->count-1].arc));
    MacroWaterfall fall={0};
    if(qlcg_river_archive_terrain(w)>=10)fall=qlcg_headwater_fall_plan(h,e->source,e->points[e->count-1].arc);
    double source_base=0;
    for(int j=e->count-1;j>=0;j--) {
        RiverVectorPoint *q=e->points+j;
        if(j==e->count-1) { e->valley_ground[j]=(float)end;continue; }
        double base=restore?qlcg_natural_base_height(w,q->wx,q->wz):q->level;
        if(!j)source_base=base;
        double exit=smooth(hypot(q->x,q->z)/fmax(64,h->span*.18));
        double lift=h->lift*qlcg_headwater_mass_weight(w,h->cell,q->wx,q->wz)*exit;
        double step=e->points[j+1].arc-q->arc;
        double above=fall_offset(&fall,q->arc),below=fall_offset(&fall,e->points[j+1].arc);
        e->valley_ground[j]=(float)(fmax(base+lift,previous-below+grade*step)+above);previous=e->valley_ground[j];
    }
    if(qlcg_river_archive_terrain(w)>=11) {
        const RiverVectorPoint *a=e->points;
        *backdrop=qlcg_headwater_backdrop(w,h,a->wx,a->wz,source_base);
        double extra=fmax(0,*backdrop-e->valley_ground[0]);
        double lead=fmin(e->points[e->count-1].arc*.5,clamp(h->valley_width*2,128,384));
        for(int j=0;j<e->count-1;j++)e->valley_ground[j]+=(float)(extra*(1-smooth(e->points[j].arc/lead)));
    }
    return 1;
}
static int frozen_geometry(const MacroWorld *w,SourceRiverNetwork *n,const HeadwaterPlan *h) {
    if(!h||h->design!=3||!h->shape.geometry_ready)return 1;
    int ground=qlcg_river_archive_terrain(w)<9||n->valley_ready;
    if(qlcg_river_archive_terrain(w)>=9)for(int k=0;k<n->edge_count;k++)if(!n->edges[k].valley_ground)ground=0;
    if(ground&&geometry_hash(n)==h->shape.geometry_hash)return 1;
    n->landform_edges=calloc(h->springs,sizeof(*n->landform_edges));if(!n->landform_edges)return 0;
    n->landform_count=h->springs;
    for(int k=0;k<h->springs;k++) {
        const HeadwaterValley *b=h->shape.branches+k;SourceRiverEdge *e=n->landform_edges+k;
        e->source=k;
        e->points=calloc(HYDROLOGY_PATH_POINTS,sizeof(*e->points));if(!e->points)return 0;
        int count=qlcg_source_template(b->ax,b->az,b->target_x,b->target_z,h->seed^(uint32_t)(k*193),0,e->points);
        if(b->point_count<2||b->point_count>count)return 0;
        e->count=b->point_count;e->points[e->count-1].x=b->bx;e->points[e->count-1].z=b->bz;
        for(int j=0;j<e->count;j++) {
            RiverVectorPoint *q=e->points+j;
            if(j)q->arc=q[-1].arc+hypot(q->x-q[-1].x,q->z-q[-1].z);
            SurfacePoint g=qlcg_frame_coordinates(&n->frame,q->x,q->z);q->wx=g.x;q->wz=g.z;
        }
        if(qlcg_river_archive_terrain(w)>=9&&!valley_ground(w,h,e,b->end,1,n->geometry.backdrop+k))return 0;
        if(!qlcg_river_index_build(&e->spatial,e->points,e->count,544))return 0;
    }
    return 1;
}
static double valley_level(const HeadwaterValley *b,double t) {
    return b->end+(b->start-b->end)*(1-t*t);
}
const HeadwaterGeometryView *qlcg_headwater_geometry(const MacroWorld *w,int cell) {
    const HeadwaterPlan *stored=qlcg_headwater_plan(w,cell);
    if(!stored||stored->design!=3)return NULL;
    HeadwaterPlan h=*stored;SourceRiverNetwork **slot=network_slot(w,cell);if(!slot)return NULL;
    if(*slot) { if((*slot)->building&&!(*slot)->geometry_ready)return NULL;geometry_view(w,*slot);return &(*slot)->geometry; }
    RiverBuffer record={0};
    if(qlcg_river_archive_get(w,RIVER_RECORD_SOURCE,cell,&record)) {
        const SourceRiverNetwork *saved=qlcg_source_network(w,cell,0);
        return saved?&saved->geometry:NULL;
    }
    SourceRiverNetwork *n=calloc(1,sizeof(*n));if(!n)return NULL;
    n->cell=cell;n->frame=qlcg_surface_frame(w,cell);n->building=1;n->valley_ready=qlcg_river_archive_terrain(w)>=9;
    n->sources=calloc(SOURCE_RIVER_LIMIT,sizeof(*n->sources));n->edges=calloc(SOURCE_RIVER_LIMIT,sizeof(*n->edges));
    if(!n->sources||!n->edges) { network_free(n);return NULL; }
    *slot=n;
    if(h.shape.geometry_ready) {
        int ok=frozen_geometry(w,n,&h);
        if(ok) {
            free(n->edges);n->edges=n->landform_edges;n->landform_edges=NULL;n->landform_count=0;
            n->source_count=n->edge_count=h.springs;
            for(int k=0;k<h.springs;k++) {
                const HeadwaterValley *b=h.shape.branches+k;SourceRiverEdge *e=n->edges+k;
                e->source=k;e->target=k?b->parent:cell;e->target_kind=k?1:0;
                n->sources[k].x=b->ax;n->sources[k].z=b->az;n->sources[k].level=b->start-.6;
            }
            ok=geometry_hash(n)==h.shape.geometry_hash;
        }
        if(!ok) { *slot=NULL;network_free(n);return NULL; }
        n->building=0;n->geometry_ready=1;geometry_view(w,n);return &n->geometry;
    }
    h.outlet_ground=qlcg_natural_base_height(w,h.center_x,h.center_z);h.outlet_level=h.outlet_ground-.8;
    h.run=fmin(1536,h.span*.12);
    double downstream;int connection=qlcg_river_head_constraint(w,cell,&h.run,&downstream);
    if(connection)h.outlet_level=fmax(h.outlet_level,downstream+.05);
    if(connection==2)h.outlet_level=downstream;
    qlcg_headwater_layout(w,&h);
    RiverPathPoint main[161];int main_count=qlcg_river_path(w,cell,main,161);
    int ok=main_count>1;
    for(int k=0;ok&&k<h.springs;k++) {
        HeadwaterValley *b=h.shape.branches+k;Target target={0};
        if(!k) {
            SurfacePoint q=qlcg_frame_local(&n->frame,main[0].wx,main[0].wz);
            target.p.x=q.x;target.p.z=q.z;target.edge=cell;target.p.level=main[0].level;
        } else {
            const SourceRiverEdge *parent=n->edges+b->parent;
            int j=(int)clamp(b->fraction*(parent->count-1),0,parent->count-2);
            target.p=parent->points[j];target.edge=b->parent;target.source=1;
        }
        b->bx=target.p.x;b->bz=target.p.z;
        b->target_x=b->bx;b->target_z=b->bz;
        SourceRiverEdge *e=n->edges+k;e->points=calloc(HYDROLOGY_PATH_POINTS,sizeof(*e->points));
        if(!e->points) { ok=0;break; }
        e->count=qlcg_source_template(b->ax,b->az,b->bx,b->bz,h.seed^(uint32_t)(k*193),0,e->points);
        if(e->count<2) { free(e->points);e->points=NULL;ok=0;break; }
        Target crossing={0};
        if(k&&intersect(e,NULL,0,n,&crossing,1)) {
            target=crossing;b->parent=target.edge;b->bx=target.p.x;b->bz=target.p.z;
        }
        b->point_count=e->count;
        double bend=0,axis_x=b->bx-b->ax,axis_z=b->bz-b->az,axis_length=fmax(1,hypot(axis_x,axis_z));
        for(int j=0;j<e->count;j++) {
            RiverVectorPoint *q=e->points+j;
            if(j)q->arc=q[-1].arc+hypot(q->x-q[-1].x,q->z-q[-1].z);
            SurfacePoint g=qlcg_frame_coordinates(&n->frame,q->x,q->z);q->wx=g.x;q->wz=g.z;
            q->level=qlcg_natural_base_height(w,g.x,g.z);
            bend=fmax(bend,fabs((q->x-b->ax)*axis_z-(q->z-b->az)*axis_x)/axis_length);
        }
        b->width=(h.valley_width+bend)/.65;
        double length=e->points[e->count-1].arc;
        e->source=k;e->target=target.edge;e->target_kind=target.source;
        if(k) {
            const SourceRiverEdge *parent=n->edges+b->parent;double distance,t;
            int near=qlcg_river_index_nearest(&parent->spatial,parent->points,b->bx,b->bz,&distance,&t);
            if(near<0) { ok=0;break; }
            b->fraction=mix(parent->points[near].arc,parent->points[near+1].arc,t)/parent->points[parent->count-1].arc;
            b->end=n->valley_ready?mix(parent->valley_ground[near],parent->valley_ground[near+1],t):
                valley_level(h.shape.branches+b->parent,b->fraction);
        } else b->end=fmax(h.outlet_level+.8,e->points[e->count-1].level);
        if(n->valley_ready) {
            if(!valley_ground(w,&h,e,b->end,0,n->geometry.backdrop+k)) { ok=0;break; }
            b->start=e->valley_ground[0];b->end=e->valley_ground[e->count-1];
        } else {
            double rise=fmax(2,h.grade*length);
            for(int j=0;j<e->count-1;j++) {
                const RiverVectorPoint *q=e->points+j;double t=q->arc/fmax(1,length),shape=1-t*t;
                double exit=smooth(hypot(q->x,q->z)/fmax(64,h.span*.18));
                double lift=h.lift*qlcg_headwater_mass_weight(w,cell,q->wx,q->wz)*exit;
                rise=fmax(rise,(q->level+lift-b->end)/fmax(.00001,shape)+2);
            }
            b->start=b->end+rise;
        }
        if(!qlcg_river_index_build(&e->spatial,e->points,e->count,544)) { ok=0;break; }
        n->sources[k].x=b->ax;n->sources[k].z=b->az;n->sources[k].level=b->start-.6;
        n->source_count++;n->edge_count++;
        if(length<64||length>32768)ok=0;
    }
    if(ok) {
        h.shape.geometry_ready=1;h.shape.geometry_hash=geometry_hash(n);
        h.source_ground=h.shape.branches[0].start;h.source_level=h.source_ground-.6;
        stored=qlcg_headwater_plan(w,cell);
        if(!stored)ok=0;
        else if(!stored->shape.geometry_ready) {
            ok=qlcg_headwater_store(w,&h);if(ok)*(HeadwaterPlan *)stored=h;
        }
    }
    if(!ok) { *slot=NULL;n->edge_count=h.springs;network_free(n);return NULL; }
    network_pack(n);n->building=0;n->geometry_ready=1;geometry_view(w,n);return &n->geometry;
}
static void supported_profile(SourceRiverEdge *e,const RiverSource *s,double end,double mouth) {
    double total=e->points[e->count-1].arc,start=e->points[0].level,previous=start;
    double delta=start-(e->valley_ground[0]-.6);
    double hold=s->planned==SOURCE_REGION_LAKE?fmin(s->pool_rx,total*.12)*2.6:0;
    double blend=fmin(total*.20,fmax(32,mouth));
    for(int j=0;j<e->count;j++) {
        RiverVectorPoint *p=e->points+j;
        double level=e->valley_ground[j]-.6+delta*(1-smooth(p->arc/fmax(32,total*.12)));
        if(hold>0)level=mix(start,level,smooth((p->arc-hold)/fmax(32,hold*.4)));
        level=mix(level,end,smooth((p->arc-total+blend)/blend));
        p->level=fmin(previous,fmax(end,level));previous=p->level;
    }
    e->points[e->count-1].level=end;
    qlcg_source_profile_finish(&e->profile,e->points,e->count,qlcg_source_form(s->spring_form)->drops,0);
}
static int headwater_profiles(const MacroWorld *w,SourceRiverNetwork *n) {
    HeadwaterPlan h=*qlcg_headwater_plan(w,n->cell);n->building=1;
    static const double widths[7]={6,4,5,11,3.2,8,4};
    for(int k=0;k<n->edge_count;k++) {
        SourceRiverEdge *e=n->edges+k;RiverSource *s=n->sources+k;
        s->planned=h.region==HEAD_REGION_SPRING?SOURCE_REGION_SPRING:
            h.region==HEAD_REGION_LAKE&&!k?SOURCE_REGION_LAKE:
            h.region==HEAD_REGION_MARSH&&k>=3?SOURCE_REGION_MARSH:SOURCE_REGION_CHANNEL;
        s->plan_kind=h.region==HEAD_REGION_GORGE?SRC_PLAN_HANGING:SRC_PLAN_GENTLE;
        dimensions_at(w,n,s->x,s->z,h.region==HEAD_REGION_SNOW,s);
        s->discharge=fmax(.05,s->discharge);s->half_width=widths[h.region]*(k?.7:1);
        s->min_half_width=.5;s->depth=h.region==HEAD_REGION_MARSH?.45:1.2;
        qlcg_source_form_choose(w,n,s,k);
        if(s->planned==SOURCE_REGION_SPRING)s->spring_form=20+(s->seed%4);
        if(s->planned==SOURCE_REGION_LAKE||s->planned==SOURCE_REGION_MARSH)s->spring_form=16+(s->seed%8);
        Target target={0};target.edge=e->target;target.source=e->target_kind;
        RiverVectorPoint *end=e->points+e->count-1;
        if(k) {
            const SourceRiverEdge *parent=n->edges+e->target;double distance,t;
            int near=qlcg_river_index_nearest(&parent->spatial,parent->points,end->x,end->z,&distance,&t);
            if(near<0) { n->building=0;return 0; }
            double arc=mix(parent->points[near].arc,parent->points[near+1].arc,t);
            target.p=*end;target.p.level=qlcg_source_profile_value(&parent->profile,arc,NULL,NULL);
            target.p.width=mix(parent->points[near].width,parent->points[near+1].width,t);
            target.tx=parent->points[near+1].x-parent->points[near].x;
            target.tz=parent->points[near+1].z-parent->points[near].z;
        } else {
            RiverPathPoint main[161];int count=qlcg_river_path(w,n->cell,main,161);
            if(count<2) { n->building=0;return 0; }
            target.p=*end;target.p.level=main[0].level;target.p.width=main[0].half_width;
            SurfacePoint next=qlcg_frame_local(&n->frame,main[1].wx,main[1].wz);
            target.tx=next.x-end->x;target.tz=next.z-end->z;
        }
        double direction=fmax(1e-9,hypot(target.tx,target.tz));target.tx/=direction;target.tz/=direction;
        SurfacePoint start=qlcg_frame_coordinates(&n->frame,s->x,s->z);
        s->level=qlcg_sample_natural_height(w,(float)start.x,(float)start.z)-.6;
        if(k)s->level=fmax(s->level,target.p.level+1);
        end->level=target.p.level;
        if(s->planned==SOURCE_REGION_LAKE)s->pool_rx=h.pool_radius;
        double mouth=target.p.width*2+12;
        int count=qlcg_source_profile(w,&n->frame,n->cell,s,e->points,e->count,1,mouth,&e->profile);
        if(count<2) { n->building=0;return 0; }
        e->count=count;
        if(qlcg_river_archive_terrain(w)>=10&&e->valley_ground)supported_profile(e,s,target.p.level,mouth);
        configure_join(e,&target,s->seed);
        s->connected=1;s->level=s->surface_level=e->points[0].level;s->spring_radius=4+(s->seed%5);
        double dx=e->points[1].x-s->x,dz=e->points[1].z-s->z,len=fmax(1e-9,hypot(dx,dz));
        s->direction_x=dx/len;s->direction_z=dz/len;s->damp_only=s->planned==SOURCE_REGION_CHANNEL;
        if(s->planned==SOURCE_REGION_LAKE||s->planned==SOURCE_REGION_MARSH) {
            qlcg_source_pool_shape(s);s->pool=s->planned==SOURCE_REGION_LAKE?3:1;
            s->pool_rx=s->planned==SOURCE_REGION_LAKE?fmin(h.pool_radius,e->points[count-1].arc*.12):fmin(100,e->points[count-1].arc*.20);
            s->pool_rz=s->pool_rx*(h.variant==0?.6:.8);s->pool_yaw=atan2(s->direction_z,s->direction_x);
            s->pool_level=s->level;s->pool_depth=s->planned==SOURCE_REGION_LAKE?2.5:.4;
        }
    }
    n->building=0;n->profile_ready=1;prepare_receiver_widths(w,n);geometry_view(w,n);
    return network_store(w,n);
}
const SourceRiverNetwork *qlcg_source_network(const MacroWorld *w,int cell,int build) {
    if(cell<0||cell>=w->cell_count) return NULL;
    MacroWorld *mutable=(MacroWorld *)w;
    RiverBuffer stored={0};
    int saved=qlcg_river_archive_get(w,RIVER_RECORD_SOURCE,cell,&stored);
    if(w->source_rivers&&w->source_rivers->revision!=w->surface_revision) qlcg_source_rivers_free(mutable);
    if(!w->source_rivers) {
        if(!saved&&(!build||!qlcg_source_candidate(w,cell))) return NULL;
        mutable->source_rivers=calloc(1,sizeof(*w->source_rivers)); if(!w->source_rivers) return NULL;
        mutable->source_rivers->revision=w->surface_revision;
        mutable->source_rivers->page_count=(w->cell_count+SOURCE_PAGE_SIZE-1)/SOURCE_PAGE_SIZE;
        mutable->source_rivers->pages=calloc(mutable->source_rivers->page_count,sizeof(*mutable->source_rivers->pages));
        if(!mutable->source_rivers->pages) { free(mutable->source_rivers); mutable->source_rivers=NULL; return NULL; }
    }
    struct SourceRiverCache *cache=w->source_rivers;
    int page=cell/SOURCE_PAGE_SIZE,slot=cell%SOURCE_PAGE_SIZE;
    if(cache->pages[page]&&cache->pages[page][slot]) {
        SourceRiverNetwork *ready=cache->pages[page][slot];
        if(ready->geometry_ready&&!ready->profile_ready) {
            if(!build||ready->building||!headwater_profiles(w,ready))return NULL;
        }
        return ready;
    }
    if(!saved&&(!build||!qlcg_source_candidate(w,cell))) return NULL;
    if(!cache->pages[page]) cache->pages[page]=calloc(SOURCE_PAGE_SIZE,sizeof(**cache->pages));
    if(!cache->pages[page]) return NULL;
    if(!saved&&qlcg_river_archive_terrain(w)>=8) {
        const HeadwaterPlan *head=qlcg_headwater_plan(w,cell);
        if(head&&head->design==3) {
            if(!qlcg_headwater_geometry(w,cell))return NULL;
            return qlcg_source_network(w,cell,build);
        }
    }
    SourceRiverNetwork *n=NULL;
    if(saved) {
        n=calloc(1,sizeof(*n)); if(!n) return NULL;
        n->cell=cell; n->frame=qlcg_surface_frame(w,cell);
        const HeadwaterPlan *head=qlcg_headwater_plan(w,cell);n->geometry_ready=head&&head->design==3;
        if(!network_record(n,&stored)||stored.at!=stored.size||!frozen_geometry(w,n,head)) { network_free(n); return NULL; }
    } else {
        n=network_build(w,cell); if(!n) return NULL;
        if(!network_store(w,n)) { network_free(n); return NULL; }
    }
    prepare_receiver_widths(w,n);
    n->profile_ready=1;geometry_view(w,n);
    cache->pages[page][slot]=n; return n;
}
static int endpoint_target(const MacroWorld *w,SourceRiverNetwork *n,const SourceRiverEdge *edge,
                            const MainEdge *main,int main_count,Target *hit) {
    RiverVectorPoint end=edge->points[edge->count-1]; double best=INFINITY;
    RiverSurfaceSample water=sample(w,n,end.x,end.z,NULL);
    for(int e=0;e<main_count+n->edge_count;e++) {
        int source=e>=main_count;
        const RiverVectorPoint *p=source?n->edges[e-main_count].points:main[e].points;
        int count=source?n->edges[e-main_count].count:main[e].count;
        if(!source&&water.kind!=WATER_KIND_RIVER&&water.kind!=WATER_KIND_FALL) continue;
        for(int k=1;k<count;k++) {
            const RiverVectorPoint *a=p+k-1,*b=p+k;
            double dx=b->x-a->x,dz=b->z-a->z,len=hypot(dx,dz); if(len<1e-6) continue;
            double t=clamp(((end.x-a->x)*dx+(end.z-a->z)*dz)/(len*len),0,1);
            double radius=mix(a->width,b->width,t),distance=hypot(end.x-a->x-dx*t,end.z-a->z-dz*t);
            if(distance>radius+.01||distance/fmax(1,radius)>=best) continue;
            best=distance/fmax(1,radius); hit->p=end; hit->p.width=radius;
            hit->p.level=source?qlcg_source_profile_value(&n->edges[e-main_count].profile,mix(a->arc,b->arc,t),NULL,NULL):water.level;
            hit->p.depth=mix(a->depth,b->depth,t); hit->source=source;
            hit->edge=source?n->edges[e-main_count].source:main[e].cell; hit->tx=dx/len; hit->tz=dz/len;
        }
    }
    return isfinite(best);
}
int qlcg_river_edit_source(MacroWorld *w,const RiverEdit *edit,int remove) {
    if(!w||!edit||edit->cell<0||edit->cell>=w->cell_count||edit->count<1||edit->count>HYDROLOGY_PATH_POINTS) return RIVER_EDIT_INVALID;
    if(!remove) for(int k=0;k<edit->count;k++) {
        const RiverPathPoint *p=edit->nodes+k;
        if(!isfinite(p->wx)||!isfinite(p->wz)||!isfinite(p->level)||!isfinite(p->half_width)) return RIVER_EDIT_INVALID;
        if(qlcg_world_cell_at_wxz(w,(float)p->wx,(float)p->wz)!=edit->cell) return RIVER_EDIT_OUTSIDE;
    }
    const SourceRiverNetwork *old=qlcg_source_network(w,edit->cell,1);
    int id=edit->source;
    if(id>=0&&(!old||id>=old->source_count)) return RIVER_EDIT_INVALID;
    if(id<0&&(remove||(old&&old->source_count>=SOURCE_RIVER_LIMIT))) return RIVER_EDIT_INVALID;
    if(old) for(int k=0;k<old->edge_count;k++)
        if(old->edges[k].target_kind==1&&old->edges[k].target==id) return RIVER_EDIT_ATTACHED;
    SourceRiverNetwork *n=calloc(1,sizeof(*n)); if(!n) return RIVER_EDIT_MEMORY;
    n->cell=edit->cell; n->frame=qlcg_surface_frame(w,n->cell);
    RiverBuffer copy={0}; SourceRiverNetwork empty={0};
    int ok=network_record(old?(SourceRiverNetwork *)old:&empty,&copy);
    RiverBuffer read={copy.data,copy.size,0,0,1,0};
    if(!ok||!network_record(n,&read)) { qlcg_river_buffer_free(&copy); network_free(n); return RIVER_EDIT_MEMORY; }
    qlcg_river_buffer_free(&copy);
    for(int k=0;k<n->edge_count;k++) if(n->edges[k].source==id) {
        qlcg_river_index_free(&n->edges[k].spatial); free(n->edges[k].points); free(n->edges[k].craft_cells);
        free(n->edges[k].valley_ground);
        memmove(n->edges+k,n->edges+k+1,(size_t)(n->edge_count-k-1)*sizeof(*n->edges));
        memset(n->edges+--n->edge_count,0,sizeof(*n->edges)); break;
    }
    int status=RIVER_EDIT_OK;
    if(remove) {
        memmove(n->sources+id,n->sources+id+1,(size_t)(n->source_count-id-1)*sizeof(*n->sources)); n->source_count--;
        for(int k=0;k<n->edge_count;k++) {
            if(n->edges[k].source>id) n->edges[k].source--;
            if(n->edges[k].target_kind==1&&n->edges[k].target>id) n->edges[k].target--;
        }
    } else {
        int fresh=id<0; if(fresh) id=n->source_count++;
        RiverSource *s=n->sources+id;
        SurfacePoint p=qlcg_frame_local(&n->frame,edit->nodes[0].wx,edit->nodes[0].wz);
        RiverSurfaceSample ground=sample(w,n,p.x,p.z,NULL);
        if(!isfinite(ground.height)||ground.kind||ground.height<0) status=RIVER_EDIT_TARGET;
        if(edit->nodes[0].level>ground.height+.01) status=RIVER_EDIT_UPHILL;
        s->x=p.x; s->z=p.z; s->level=edit->nodes[0].level; s->connected=0;
        if(fresh) { dimensions_at(w,n,s->x,s->z,0,s); qlcg_source_form_choose(w,n,s,id); }
        if(s->discharge<=0) s->discharge=.03;
        if(edit->count==1) {
            if(!status) configure_pool(w,n,s);
        } else if(!status) {
            SourceRiverEdge edge={0};
            edge.points=calloc(HYDROLOGY_PATH_POINTS,sizeof(*edge.points));
            RiverVectorPoint *controls=calloc(edit->count,sizeof(*controls));
            if(!edge.points||!controls) status=RIVER_EDIT_MEMORY;
            for(int k=0;!status&&k<edit->count;k++) {
                RiverPathPoint a=edit->nodes[k]; SurfacePoint q=qlcg_frame_local(&n->frame,a.wx,a.wz);
                if(qlcg_world_cell_at_wxz(w,(float)a.wx,(float)a.wz)!=n->cell) { status=RIVER_EDIT_OUTSIDE; break; }
                controls[k]=(RiverVectorPoint){a.wx,a.wz,q.x,q.z,0,fmax(.5,a.half_width),0,a.level,a.depth};
            }
            if(!status) status=qlcg_river_edit_validate(controls,edit->count);
            for(int k=0;!status&&k<edit->count;k++) {
                if(!k) { edge.points[edge.count++]=controls[0]; continue; }
                RiverVectorPoint a=controls[k-1],b=controls[k];
                double span=hypot(b.x-a.x,b.z-a.z); int steps=(int)ceil(span/32);
                if(steps<1||steps>HYDROLOGY_PATH_POINTS-edge.count) { status=RIVER_EDIT_OUTSIDE; break; }
                for(int j=1;j<=steps;j++) {
                    double t=j/(double)steps; RiverVectorPoint q=b;
                    q.x=mix(a.x,b.x,t); q.z=mix(a.z,b.z,t); q.level=mix(a.level,b.level,t);
                    edge.points[edge.count++]=q;
                }
            }
            MainEdge *main=calloc(25,sizeof(*main)); int main_count=0;
            if(!main) status=RIVER_EDIT_MEMORY;
            RiverDetailCandidates list={0}; if(!status) qlcg_river_detail_candidates(w,n->cell,&list);
            for(int k=0;!status&&k<list.count;k++) if(read_main_edge(w,&n->frame,list.edges[k],main+main_count)) main_count++;
            Target hit={0}; double mouth=0; int target=0;
            if(!status) target=intersection(&edge,main,main_count,n,&hit);
            if(!status&&!target&&endpoint_target(w,n,&edge,main,main_count,&hit)) {
                edge.points[edge.count-1]=hit.p; target=1;
            }
            if(!status&&!target) {
                RiverPathPoint end=edit->nodes[edit->count-1]; LakeShoreField lake=qlcg_lake_field(w,end.wx,end.wz);
                RiverSurfaceSample oxbow={0}; oxbow.height=1e9f; qlcg_oxbow_surface(w,end.wx,end.wz,&oxbow);
                if((lake.lake&&lake.d<0&&lake.bed<0)||oxbow.kind==WATER_KIND_LAKE) {
                    hit.source=SOURCE_TARGET_LAKE; hit.edge=n->cell;
                    edge.points[edge.count-1].level=lake.lake&&lake.d<0?lake.level:oxbow.level;
                    mouth=s->half_width*2; target=1;
                }
            }
            if(!status&&!target) status=RIVER_EDIT_TARGET;
            if(!status&&hit.source!=SOURCE_TARGET_LAKE&&!join_distance(&edge,&hit,&mouth)) status=RIVER_EDIT_TARGET;
            if(!status) {
                int count=qlcg_source_profile(w,&n->frame,n->cell,s,edge.points,edge.count,hit.source,mouth,&edge.profile);
                if(count<2) status=RIVER_EDIT_UPHILL;
                else { edge.count=count; status=qlcg_river_edit_validate(edge.points,count); }
            }
            if(!status) {
                if(hit.source!=SOURCE_TARGET_LAKE) configure_join(&edge,&hit,s->seed);
                edge.source=id; edge.target=hit.edge; edge.target_kind=hit.source;
                if(!qlcg_river_index_build(&edge.spatial,edge.points,edge.count,edge.profile.influence)) status=RIVER_EDIT_MEMORY;
                else {
                    s->connected=1; s->level=edge.points[0].level; configure_spring(w,n,s,&edge,id);
                    n->edges[n->edge_count++]=edge; edge.points=NULL; edge.spatial=(RiverVectorIndex){0};
                }
            }
            free(controls); free(main); free(edge.points); qlcg_river_index_free(&edge.spatial);
        }
    }
    if(!status&&!network_store(w,n)) status=RIVER_EDIT_MEMORY;
    network_free(n);
    if(!status) w->surface_revision++;
    return status;
}

#include "hydrology_source_craft.inc"
