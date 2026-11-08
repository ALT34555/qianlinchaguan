#include "river_fields.h"
/* 源流、泉眼与湿点的惰性目录 */
#include "source_rivers.h"
#include "surface_internal.h"
#include "surface_detail.h"
#include "surface_lakes.h"
#include "surface_macro.h"
#include "gen/gen_water.h"
#include "river_archive.h"
#include "river_edit.h"
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
    }
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
        if(b->failed||m[0]<2||m[0]>SOURCE_POINT_LIMIT||m[1]<0||m[1]>=n->source_count||m[3]<0||m[3]>SOURCE_TARGET_CONTINUATION) return 0;
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
            if(!qlcg_river_index_build(&e->spatial,e->points,e->count,e->profile.influence)) return 0;
        }
    }
    int crafted=0; for(int k=0;k<n->edge_count;k++) if(n->edges[k].craft_id) crafted=1;
    if((!b->reading&&crafted)||(b->reading&&b->at<b->size)) {
        uint32_t tag=0x31454352; qlcg_river_values(b,&tag,(sizeof(tag))/4,4); if(tag!=0x31454352) return 0;
        for(int k=0;k<n->edge_count;k++) {
            SourceRiverEdge *e=n->edges+k;
            qlcg_river_values(b,&e->craft_id,(sizeof(e->craft_id))/8,8);
            qlcg_river_values(b,&e->craft_owner,(sizeof(int))/4,4); qlcg_river_values(b,&e->craft_order,(sizeof(int))/4,4);
            qlcg_river_values(b,&e->craft_count,(sizeof(int))/4,4);
            if(b->failed||e->craft_count<0||e->craft_count>SOURCE_POINT_LIMIT*2||e->craft_order<0) return 0;
            if(b->reading&&e->craft_count) { e->craft_cells=malloc((size_t)e->craft_count*sizeof(int)); if(!e->craft_cells) return 0; }
            if(e->craft_count) qlcg_river_values(b,e->craft_cells,((size_t)e->craft_count*sizeof(int))/4,4);
        }
    }
    return !b->failed;
}
static int network_store(const MacroWorld *w,SourceRiverNetwork *n) {
    RiverBuffer b={0};
    int ok=network_record(n,&b)&&qlcg_river_archive_put((MacroWorld *)w,RIVER_RECORD_SOURCE,n->cell,&b);
    qlcg_river_buffer_free(&b); return ok;
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
    RiverSurfaceSample q=sample(w,n,p->x,p->z,NULL);
    if(q.kind||!isfinite(q.height)||q.height<=0) return 0;
    s->x=p->x; s->z=p->z; s->level=q.height-.8;
    s->planned=1; s->plan_kind=p->kind;
    s->direction_x=p->dx; s->direction_z=p->dz;
    dimensions_at(w,n,p->x,p->z,p->snow,s);
    return 1;
}
static int intersection(SourceRiverEdge *edge,const MainEdge *main,int main_count,
                         const SourceRiverNetwork *n,Target *hit) {
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
                hit->p.level=decorative?qlcg_source_profile_value(&n->edges[e-main_count].profile,mix(c->arc,d->arc,u),NULL,NULL):
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
    if(!e->points) e->points=calloc(SOURCE_POINT_LIMIT,sizeof(*e->points));
    return e->points?e:NULL;
}
static SourceRiverNetwork *network_pack(SourceRiverNetwork *n) {
    for(int k=n->edge_count;k<SOURCE_RIVER_LIMIT;k++) {
        free(n->edges[k].points); qlcg_river_index_free(&n->edges[k].spatial);
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
    uint32_t rng=w->opt.seed^((uint32_t)(cell+1)*0x45d9f3bu);
    for(int k=0;k<count;k++) {
        RiverSource source={0}; Target preferred={0}; int found=0,has_preferred=0;
        if(!k) found=planned_source(w,n,&plan.source,&source);
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
            if(!qlcg_river_index_build(&edge->spatial,edge->points,edge->count,edge->profile.influence)) {
                qlcg_river_index_free(&edge->spatial); continue;
            }
            s->connected=1; s->level=edge->points[0].level;
            configure_spring(w,n,s,edge,k); n->edge_count++; break;
        }
    }
    for(int k=0;k<n->source_count;k++)
        if(!n->sources[k].connected&&(n->sources[k].planned||!n->sources[k].snowmelt)) configure_pool(w,n,n->sources+k);
    free(main); free(targets); return network_pack(n);
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
    if(cache->pages[page]&&cache->pages[page][slot]) return cache->pages[page][slot];
    if(!saved&&(!build||!qlcg_source_candidate(w,cell))) return NULL;
    if(!cache->pages[page]) cache->pages[page]=calloc(SOURCE_PAGE_SIZE,sizeof(**cache->pages));
    if(!cache->pages[page]) return NULL;
    SourceRiverNetwork *n=NULL;
    if(saved) {
        n=calloc(1,sizeof(*n)); if(!n) return NULL;
        n->cell=cell; n->frame=qlcg_surface_frame(w,cell);
        if(!network_record(n,&stored)||stored.at!=stored.size) { network_free(n); return NULL; }
    } else {
        n=network_build(w,cell); if(!n) return NULL;
        if(!network_store(w,n)) { network_free(n); return NULL; }
    }
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
    if(!w||!edit||edit->cell<0||edit->cell>=w->cell_count||edit->count<1||edit->count>SOURCE_POINT_LIMIT) return RIVER_EDIT_INVALID;
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
            edge.points=calloc(SOURCE_POINT_LIMIT,sizeof(*edge.points));
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
                if(steps<1||steps>SOURCE_POINT_LIMIT-edge.count) { status=RIVER_EDIT_OUTSIDE; break; }
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

#include "source_craft.inc"

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
void qlcg_source_surface_mode(const MacroWorld *w,float wx,float wz,float natural,
                              RiverSurfaceSample *out,int build) {
    if(out->kind==WATER_KIND_SEA||out->kind==WATER_KIND_LAKE) return;
    int main_water=(out->kind==WATER_KIND_RIVER||out->kind==WATER_KIND_FALL)&&!out->decorative&&out->level>out->height+.02f;
    if(main_water) return;
    int cell=qlcg_world_cell_at_wxz(w,wx,wz);
    const SourceRiverNetwork *n=qlcg_source_network(w,cell,build); if(!n) return;
    SurfacePoint p=qlcg_frame_local(&n->frame,wx,wz); double best=INFINITY,height=out->height,support=0;
    for(int k=0;k<n->edge_count;k++) {
        const SourceRiverEdge *e=n->edges+k; double distance,t;
        int near=qlcg_river_index_nearest(&e->spatial,e->points,p.x,p.z,&distance,&t); if(near<0) continue;
        if(distance>=e->profile.influence) continue;
        const RiverVectorPoint *a=e->points+near,*b=a+1;
        double radius=mix(a->width,b->width,t),bed=natural,grade=0; int fall=0;
        double water=qlcg_source_profile_value(&e->profile,mix(a->arc,b->arc,t),&grade,&fall);
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
        int wet=qlcg_source_section_span(distance,radius,depth,natural,water,e->profile.influence,&bed,&water);
        if(mouth&&distance<radius) {
            double detail=depth*(.18*relief.bar+.10*relief.shelf-.40*relief.scour-.16*relief.branch);
            detail=fmin(detail,fmax(0,water-.05-bed));
            bed+=detail*(1-smooth(distance/radius))*qlcg_junction_weight(&e->join,mouth_distance);
            wet=bed<water-.025;
        }
        double weight=1-smooth((distance-radius)/fmax(1,e->profile.influence-radius));
        if(wet) height=out->decorative&&out->level>out->height?fmin(height,bed):bed;
        else if(out->level<=out->height&&weight>support&&fabs(bed-natural)>1e-6) {
            height=bed; support=weight;
            out->bank_level=(float)fmin(water,bed);
            out->has_bank_level=1;
        }
        if(wet&&distance/fmax(.1,radius)<best) {
            best=distance/fmax(.1,radius);
            source_water(out,height,water,radius*2,n->sources[e->source].discharge,fall?WATER_KIND_FALL:WATER_KIND_RIVER);
            out->grade=(float)grade; out->channel_depth=(float)depth;
            SurfacePoint da=point_direction(w,e,near),db=point_direction(w,e,near+1);
            double dx=mix(da.x,db.x,t),dz=mix(da.z,db.z,t);
            double span=fmax(1e-9,hypot(dx,dz));
            out->east=(float)(dx/span); out->north=(float)(-dz/span);
            out->bank_distance=(float)fmax(0,radius-distance); out->lateral=(float)clamp(distance/fmax(1e-6,radius),0,1);
        }
    }
    out->height=(float)height;
    for(int k=0;k<n->edge_count;k++) if(n->edges[k].target_kind==SOURCE_TARGET_POOL) {
        RiverSource pool; qlcg_source_terminal_pool(n,n->edges+k,&pool);
        RiverSurfaceSample lake=*out; lake.kind=WATER_KIND_DRY; lake.level=-1e9f;
        qlcg_source_form_surface_mode(&pool,BASIN_TERMINAL,p.x-pool.x,p.z-pool.z,fmax(natural,pool.level),&lake);
        if(lake.kind==WATER_KIND_LAKE||lake.kind==WATER_KIND_STATIC) *out=lake;
    }
    for(int k=0;k<n->source_count;k++) {
        const RiverSource *s=n->sources+k;
        if(s->planned<0) continue;
        qlcg_source_form_surface_mode(s,s->connected?BASIN_HEAD:BASIN_SPRING,
                                      p.x-s->x,p.z-s->z,natural,out);
    }
}

void qlcg_source_surface(const MacroWorld *w,float wx,float wz,float natural,RiverSurfaceSample *out) {
    qlcg_source_surface_mode(w,wx,wz,natural,out,1);
}
