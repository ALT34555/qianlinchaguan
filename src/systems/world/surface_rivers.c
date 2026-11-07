/* 主河曲线、贴地剖面与河岸断面 */
#include "surface_rivers.h"
#include "surface_internal.h"
#include "surface_frame.h"
#include "surface_lakes.h"
#include "surface_islands.h"
#include "surface_coast.h"
#include "surface_macro.h"
#include "surface_detail.h"
#include "core/config.h"
#include "river_profile.h"
#include "river_junction.h"
#include "river_vectors.h"
#include "river_archive.h"
#include "river_edit.h"
#include "macro_waterfalls.h"
#include "../../core/math_util.h"
#include "gen/gen_river.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CURVE_LIMIT 32
#define RIVER_CELL_CACHE 128
#define SURFACE_RIVER_MAX_WIDTH 6000.0
static const double PI=3.14159265358979323846;
static double clampd(double x,double a,double b) { return fmax(a,fmin(b,x)); }
static double mix(double a,double b,double t) { return a+(b-a)*t; }
static double smooth(double t) { t=clampd(t,0,1); return t*t*(3-2*t); }
typedef SurfacePoint Point;
typedef SurfaceFrame Frame;
typedef RiverVectorPoint CurvePoint;
typedef struct { double left,right,top,bottom; } ReachBounds;
typedef struct {
    int cell,receiver,count,sea_mouth,profile_ready,profile_building,authored;
    unsigned long long used;
    Frame frame;
    CurvePoint points[161];
    double parameters[161],saved_curvature[161];
    double edit_begin[161],edit_end[161];
    unsigned char crafted[161];
    double depth_a,depth_b;
    double detail_clear,detail_fade,valley_span,origin_x;
    ReachBounds detail_bounds;
    RiverProfile profile,ground_profile;
    MacroWaterfall waterfall;
    RiverVectorIndex spatial;
    RiverMouthPoint inlet,outlet;
} RiverCurve;
typedef struct { int key; RiverDetailCandidates list; } RiverCellCache;
typedef struct { int key,valid,edges[3]; Frame frame; RiverJunction plan; } JunctionSlot;
struct OxbowCatalog {
    int *lookup,count,capacity;
    OxbowPlan *plans;
};
typedef struct {
    int cell;
    double arc,offset,radius,depth,level,curvature;
} JunctionBank;
struct RiverSurfaceCache {
    uint32_t revision;
    unsigned long long serial;
    RiverCurve *items[CURVE_LIMIT];
    RiverCurve *detail_preview;
    RiverCellCache cells[RIVER_CELL_CACHE];
    JunctionSlot junctions[RIVER_CELL_CACHE];
};

static Point center(const MacroWorld *w,int i) {
    return (Point){((i%w->columns+.5)/w->columns-.5)*w->circumference,
                   ((i/w->columns+.5)/w->rows-.5)*qlcg_world_depth(w)};
}
static double main_width(const MacroWorld *w,int i) {
    return w->main_river_width?fmax(w->main_river_width[i],w->river_width[i]):w->river_width[i];
}
static int incoming(const MacroWorld *w,int node,int original,int *count) {
    int best=-1;
    if (count) *count=0;
    for (int k=0;k<8;k++) {
        int i=w->neighbors[node*8+k];
        if (w->receiver[i]!=node) continue;
        if (count && main_width(w,i)>0) (*count)++;
        double width=original?w->river_width[i]:main_width(w,i);
        if (width>0 && (best<0 || w->catchment[i]>w->catchment[best] ||
            (w->catchment[i]==w->catchment[best] && i<best))) best=i;
    }
    return best;
}
int qlcg_is_river_head(const MacroWorld *w,int cell) {
    const MacroCellPlan *plan=qlcg_macro_cell_plan(w,cell);
    return plan&&plan->head;
}
static Point tangent(const MacroWorld *w,const Frame *f,int node,double length,int *count) {
    int prev=incoming(w,node,1,count),next=w->receiver[node];
    Point a=center(w,prev>=0?prev:node),b=center(w,next>=0?next:node);
    Point p=qlcg_frame_local(f,a.x,a.z),q=qlcg_frame_local(f,b.x,b.z);
    double scale=length*.85/fmax(1,hypot(q.x-p.x,q.z-p.z));
    return (Point){(q.x-p.x)*scale,(q.z-p.z)*scale};
}
static double random_next(uint32_t *seed) {
    uint32_t t=(*seed+=0x6d2b79f5u);
    t=(t^(t>>15))*(t|1u); t^=t+(t^(t>>7))*(t|61u);
    return (t^(t>>14))/4294967296.0;
}
static double receiver_level(const MacroWorld *w,int cell) {
    if (w->water[cell]==1) return 0;
    int id=w->lake_id?w->lake_id[cell]:-1;
    if (id>=0 && id<w->lake_count) return w->lakes[id].level;
    return w->level?w->level[cell]:w->height[cell];
}
static int mouth_wet(const MacroWorld *w,const RiverMouthPoint *mouth,double wx,double wz) {
    if (mouth->kind==WATER_KIND_LAKE) {
        if (!w->level && !w->lake_templates) return w->water[qlcg_macro_cell_at(w,wx,wz)]==2;
        LakeShoreField f=qlcg_lake_field(w,wx,wz);
        return f.lake && fabs(f.level-mouth->level)<.02 && f.d<0 && f.bed<0 && f.d*f.bed>.02;
    }
    SurfaceMacroContext context=qlcg_macro_context(w,wx,wz);
    double h=qlcg_read_macro(w,&context,w->height),island;
    if (qlcg_island_coast(w,wx,wz,h,&island)) h=island;
    else h=qlcg_coast_surface(w,&context,wx,wz,h,h).height;
    return h<0;
}
static void locate_mouth(const MacroWorld *w,RiverCurve *c,RiverMouthPoint *mouth,
                           const CurvePoint *a,const CurvePoint *b,int wet_a) {
    double lo=0,hi=1;
    for (int k=0;k<9 && (hi-lo)*(b->arc-a->arc)>.5;k++) {
        double t=(lo+hi)*.5;
        Point p=qlcg_frame_coordinates(&c->frame,mix(a->x,b->x,t),mix(a->z,b->z,t));
        if (mouth_wet(w,mouth,p.x,p.z)==wet_a) lo=t; else hi=t;
    }
    double t=(lo+hi)*.5;
    Point p=qlcg_frame_coordinates(&c->frame,mix(a->x,b->x,t),mix(a->z,b->z,t));
    mouth->wx=p.x; mouth->wz=p.z; mouth->arc=mix(a->arc,b->arc,t);
    mouth->half_width=mix(a->width,b->width,t);
}
static void configure_crafted(RiverCurve *c) {
    for(int first=1;first<c->count;first++) {
        if(!c->crafted[first]) continue;
        int last=first; while(last+1<c->count&&c->crafted[last+1]) last++;
        for(int k=first;k<=last;k++) { c->edit_begin[k]=c->parameters[first-1]; c->edit_end[k]=c->parameters[last]; }
        first=last;
    }
}
static int finish_curve(const MacroWorld *w,RiverCurve *c,int restored) {
    double min_width=INFINITY,max_width=0;
    ReachBounds b={INFINITY,-INFINITY,INFINITY,-INFINITY};
    c->origin_x=c->points[0].wx;
    double C=c->frame.C;
    int in_wet=0,out_wet=0;
    for (int i=0;i<c->count;i++) {
        CurvePoint *p=&c->points[i];
        const CurvePoint *before=&c->points[i?i-1:i],*next=&c->points[i+1<c->count?i+1:i];
        double ax=p->x-before->x,az=p->z-before->z,bx=next->x-p->x,bz=next->z-p->z;
        double span=hypot(ax,az);
        p->arc=(i?before->arc:0)+span;
        if (!restored && c->inlet.kind) {
            int wet=mouth_wet(w,&c->inlet,p->wx,p->wz);
            if (!i && wet) c->inlet=(RiverMouthPoint){p->wx,p->wz,0,c->inlet.level,0,p->width,c->inlet.kind};
            else if (i && !in_wet && wet && !isfinite(c->inlet.arc))
                locate_mouth(w,c,&c->inlet,before,p,in_wet);
            in_wet=wet;
        }
        if (!restored && c->outlet.kind) {
            int wet=mouth_wet(w,&c->outlet,p->wx,p->wz);
            if (i && out_wet && !wet) locate_mouth(w,c,&c->outlet,before,p,out_wet);
            out_wet=wet;
        }
        p->curvature=c->authored==2&&restored?c->saved_curvature[i]:clampd(atan2(ax*bz-az*bx,ax*bx+az*bz)*p->width/fmax(1,span),-1,1);
        double x=p->wx-c->origin_x;
        x-=floor(x/C+.5)*C;
        b.left=fmin(b.left,x); b.right=fmax(b.right,x);
        b.top=fmin(b.top,p->wz); b.bottom=fmax(b.bottom,p->wz);
        min_width=fmin(min_width,p->width); max_width=fmax(max_width,p->width);
    }
    RiverMouthPoint *mouths[2]={&c->inlet,&c->outlet};
    if (c->outlet.kind && !isfinite(c->outlet.arc) && out_wet) {
        CurvePoint *p=c->points+c->count-1;
        c->outlet=(RiverMouthPoint){p->wx,p->wz,p->arc,c->outlet.level,0,p->width,c->outlet.kind};
    }
    for (int k=0;k<2;k++) {
        RiverMouthPoint *m=mouths[k];
        if (isfinite(m->arc)) m->blend=fmin(c->points[c->count-1].arc*.35,fmax(64,4*m->half_width));
    }
    c->detail_clear=fmax(max_width+48,1.15*max_width+8)+CHUNK_SIZE*1.42+2;
    c->detail_fade=fmax(64,.6*max_width);
    double pad=c->detail_clear+c->detail_fade;
    if (!c->sea_mouth) {
        c->valley_span=clampd(380+.42*max_width,400,1400);
        pad=fmax(pad,c->detail_clear+12+c->valley_span);
    }
    double lat=(fmax(fabs(b.top),fabs(b.bottom))+pad)*2*PI/C;
    double lon_pad=lat<PI*.5?fmin(C*.5,pad/cos(lat)):C*.5;
    c->detail_bounds=(ReachBounds){b.left-lon_pad,b.right+lon_pad,b.top-pad,b.bottom+pad};
    double extra=fmax(74,pad-2*min_width);
    c->spatial.bin_size=512;
    if (max_width>512 || pad>768)
        while (c->spatial.bin_size<max_width+extra*.5) c->spatial.bin_size*=2;
    if(c->authored==2) configure_crafted(c);
    return qlcg_river_index_build(&c->spatial,c->points,c->count,extra);
}
static void curve_free(RiverCurve *c) {
    if (!c) return;
    qlcg_river_profile_free(&c->ground_profile); qlcg_river_profile_free(&c->profile); qlcg_river_index_free(&c->spatial); free(c);
}
static int profile_record(RiverProfile *p,RiverBuffer *b) {
    int32_t count=0,*indices=NULL; float *values=NULL;
    if(!b->reading) {
        indices=malloc((size_t)p->count*sizeof(*indices)); values=malloc((size_t)p->count*sizeof(*values));
        if(!indices||!values) { free(indices); free(values); return 0; }
        int start=0; double lower=-INFINITY,upper=INFINITY;
        indices[count]=0; values[count++]=p->levels[0];
        for(int i=1;i<p->count;i++) {
            double slope=((double)p->levels[i]-p->levels[start])/(i-start);
            if(i>start+1&&(slope<lower||slope>upper)) {
                start=i-1; indices[count]=start; values[count++]=p->levels[start]; lower=-INFINITY; upper=INFINITY;
            }
            lower=fmax(lower,((double)p->levels[i]-p->levels[start]-.0005)/(i-start));
            upper=fmin(upper,((double)p->levels[i]-p->levels[start]+.0005)/(i-start));
        }
        indices[count]=p->count-1; values[count++]=p->levels[p->count-1];
        if(count*2>=p->count) count=0;
        for(int k=1;k<count;k++) for(int j=indices[k-1];j<=indices[k];j++) {
            float level=(float)mix(values[k-1],values[k],(j-indices[k-1])/(double)(indices[k]-indices[k-1]));
            if(fabs((double)level-p->levels[j])>.0005) { count=0; break; }
        }
    }
    qlcg_river_io(b,&count,sizeof(count));
    if(b->failed||count<0||count>p->count||count==1) { free(indices); free(values); return 0; }
    if(!count) qlcg_river_io(b,p->levels,(size_t)p->count*sizeof(float));
    else {
        if(b->reading) { indices=malloc((size_t)count*sizeof(*indices)); values=malloc((size_t)count*sizeof(*values)); }
        if(!indices||!values) { free(indices); free(values); return 0; }
        qlcg_river_io(b,indices,(size_t)count*sizeof(*indices)); qlcg_river_io(b,values,(size_t)count*sizeof(*values));
        if(b->failed||indices[0]!=0||indices[count-1]!=p->count-1) { free(indices); free(values); return 0; }
        for(int k=1;k<count;k++) {
            if(indices[k]<=indices[k-1]||indices[k]>=p->count||!isfinite(values[k])||!isfinite(values[k-1])) { free(indices); free(values); return 0; }
            for(int j=indices[k-1];j<=indices[k];j++)
                p->levels[j]=(float)mix(values[k-1],values[k],(j-indices[k-1])/(double)(indices[k]-indices[k-1]));
        }
    }
    free(indices); free(values); return !b->failed;
}
static int curve_record(const MacroWorld *w,RiverCurve *c,RiverBuffer *b) {
    int32_t meta[3]={c->count,c->receiver,c->profile.count};
    qlcg_river_io(b,meta,sizeof(meta));
    if(b->failed||meta[0]<2||meta[0]>161||meta[1]<0||meta[1]>=w->cell_count||meta[2]<0||meta[2]==1||meta[2]>1000000) return 0;
    c->count=meta[0]; c->receiver=meta[1];
    qlcg_river_io(b,&c->depth_a,sizeof(double)); qlcg_river_io(b,&c->depth_b,sizeof(double));
    qlcg_river_io(b,&c->inlet,sizeof(c->inlet)); qlcg_river_io(b,&c->outlet,sizeof(c->outlet));
    for(int k=0;k<c->count;k++) {
        CurvePoint *p=c->points+k;
        double v[5]={p->wx,p->wz,p->x,p->z,p->width}; qlcg_river_io(b,v,sizeof(v));
        if(b->reading) {
            for(int j=0;j<5;j++) if(!isfinite(v[j])) return 0;
            if(v[4]<=0||v[4]>3000) return 0;
            *p=(CurvePoint){v[0],v[1],v[2],v[3],0,v[4],0};
        }
    }
    c->profile.count=meta[2];
    if(meta[2]) {
        if(b->reading) { c->profile.levels=malloc((size_t)meta[2]*sizeof(float)); if(!c->profile.levels) return 0; }
        qlcg_river_io(b,&c->profile.arc,sizeof(double));
        if(!profile_record(&c->profile,b)) return 0;
        if(b->reading) for(int k=0;k<meta[2];k++) if(!isfinite(c->profile.levels[k])) return 0;
        c->profile_ready=1;
    }
    if(!b->reading || b->at<b->size) {
        int32_t tag=0x57463131,state=c->waterfall.state,style=c->waterfall.style;
        double v[5]={c->waterfall.begin,c->waterfall.end,c->waterfall.top,c->waterfall.bottom,c->waterfall.width};
        qlcg_river_io(b,&tag,sizeof(tag)); qlcg_river_io(b,&state,sizeof(state));
        qlcg_river_io(b,&style,sizeof(style)); qlcg_river_io(b,v,sizeof(v));
        if(tag!=0x57463131 || state<0 || state>2 || style<0 || style>7) return 0;
        for(int j=0;j<5;j++) if(!isfinite(v[j])) return 0;
        if(state==2 && (v[0]<0 || v[1]<=v[0] || v[1]>c->profile.arc || v[2]<=v[3] || v[4]<=0)) return 0;
        c->waterfall=(MacroWaterfall){state,style,v[0],v[1],v[2],v[3],v[4]};
    }
    if((!b->reading && c->ground_profile.count) || (b->reading && b->at<b->size)) {
        int32_t tag=0x47503131;
        qlcg_river_io(b,&tag,sizeof(tag));
        if(tag!=0x47503131) {
            if(!b->reading||(tag!=0x31444552&&tag!=0x32444552)) return 0;
            b->at-=sizeof(tag);
        } else {
        if(!c->profile.count) return 0;
        if(b->reading) {
            c->ground_profile.count=c->profile.count;
            c->ground_profile.arc=c->profile.arc;
            c->ground_profile.levels=malloc((size_t)c->profile.count*sizeof(float));
            if(!c->ground_profile.levels) return 0;
        }
        if(!profile_record(&c->ground_profile,b)) return 0;
        for(int k=0;k<c->ground_profile.count;k++) if(!isfinite(c->ground_profile.levels[k])) return 0;
        }
    }
    if((!b->reading&&c->authored)||(b->reading&&b->at<b->size)) {
        int32_t tag=c->authored==2?0x32444552:0x31444552;
        qlcg_river_io(b,&tag,sizeof(tag));
        if(tag==0x31444552) {
            qlcg_river_io(b,&c->authored,sizeof(int)); if(c->authored!=1) return 0;
        } else if(tag==0x32444552) {
            c->authored=2;
            qlcg_river_io(b,c->parameters,(size_t)c->count*sizeof(double));
            qlcg_river_io(b,c->saved_curvature,(size_t)c->count*sizeof(double));
            qlcg_river_io(b,c->crafted,(size_t)c->count);
            for(int k=0;k<c->count;k++) if(!isfinite(c->parameters[k])||!isfinite(c->saved_curvature[k])||c->crafted[k]>1||
                c->parameters[k]<0||c->parameters[k]>c->profile.arc+.01||(k&&c->parameters[k]<=c->parameters[k-1])) return 0;
        } else return 0;
    }
    return !b->failed;
}
static int curve_store(const MacroWorld *w,RiverCurve *c) {
    RiverBuffer b={0};
    int ok=curve_record(w,c,&b)&&qlcg_river_archive_put((MacroWorld *)w,RIVER_RECORD_MAIN,c->cell,&b);
    qlcg_river_buffer_free(&b); return ok;
}
void qlcg_river_surface_free(MacroWorld *w) {
    if (!w->river_surface) return;
    for (int i=0;i<CURVE_LIMIT;i++) curve_free(w->river_surface->items[i]);
    free(w->river_surface); w->river_surface=NULL;
}
static double surface_width_limit(const MacroWorld *w,double wz) {
    double span=w->circumference/w->columns*(qlcg_world_flat(w)?1:cos(wz/w->circumference*2*PI));
    return clampd(.65*span,1024,SURFACE_RIVER_MAX_WIDTH);
}
static int curve_base(const MacroWorld *w,int cell,const Frame *frame,CurvePoint *points) {
    int r=w->receiver[cell];
    int sea_mouth=w->water[r]==1;
    Point origin=center(w,cell),end=center(w,r);
    int lake_start=qlcg_lake_connection(w,cell,1,&origin.x,&origin.z);
    if (w->water[r]==2) qlcg_lake_connection(w,r,0,&end.x,&end.z);
    Point begin=qlcg_frame_local(frame,origin.x,origin.z),target=qlcg_frame_local(frame,end.x,end.z);
    double length=hypot(target.x-begin.x,target.z-begin.z),chunks=w->circumference/(double)w->columns/64;
    int inflows;
    Point start_t=tangent(w,frame,cell,length,NULL),end_t=tangent(w,frame,r,length,&inflows);
    if (lake_start) start_t=(Point){(target.x-begin.x)*.65,(target.z-begin.z)*.65};
    double start_width=qlcg_river_discharge_width(w->flow[cell])/2;
    int head=!lake_start&&qlcg_is_river_head(w,cell);
    double head_run=fmin(length*.32,fmax(320,start_width*20));
    double end_width=start_width;
    if (!sea_mouth && inflows<2 && w->water[r]!=2 && main_width(w,r)>0)
        end_width=qlcg_river_discharge_width(w->flow[r])/2;
    double gentle=1-clampd(fabs((double)w->spill[cell]-w->spill[r])/fmax(1e-6,w->height_scale)/fmax(1,length)/.012,0,1);
    int bends=chunks<10?0:(int)fmin(3,fmax(1,floor((1+gentle*2)*clampd(sqrt(chunks/625),.35,1)+.5)));
    double amplitude=fmin(w->circumference/(double)w->columns*.1,length/(bends+1)*.3)*(.2+.8*gentle);
    double side=sin(frame->lon*frame->radius/260000+sin(frame->lat*13)*2+(w->opt.seed%997))<0?-1:1;
    double knots[5]={0}; uint32_t rng=w->opt.seed^((uint32_t)(cell+1)*0x7f4a7c15u);
    for (int k=1;k<=bends;k++) knots[k]=side*(k%2?1:-1)*amplitude*(.65+random_next(&rng)*.35);
    int steps=(int)fmin(160,fmax(24,floor(length/128+.5)));
    for (int k=0;k<=steps;k++) {
        double t=k/(double)steps,t2=t*t,t3=t2*t;
        int section=(int)fmin(bends,floor(t*(bends+1)));
        double u=clampd(t*(bends+1)-section,0,1),wave=mix(knots[section],knots[section+1],smooth(u));
        double x=begin.x*(2*t3-3*t2+1)+start_t.x*(t3-2*t2+t)+target.x*(-2*t3+3*t2)+end_t.x*(t3-t2)-(target.z-begin.z)/fmax(1,length)*wave;
        double z=begin.z*(2*t3-3*t2+1)+start_t.z*(t3-2*t2+t)+target.z*(-2*t3+3*t2)+end_t.z*(t3-t2)+(target.x-begin.x)/fmax(1,length)*wave;
        Point g=qlcg_frame_coordinates(frame,x,z);
        if (!k) g=origin;
        if (k==steps) g=end;
        Point p=qlcg_frame_local(frame,g.x,g.z);
        double width=mix(start_width,end_width,smooth(t));
        if(head) width=mix(fmin(6,start_width*.12),width,smooth(t*length/fmax(1,head_run)));
        if (width>512) width=fmin(surface_width_limit(w,g.z)/2,width);
        points[k]=(CurvePoint){g.x,g.z,p.x,p.z,0,width,0};
    }
    return steps+1;
}

typedef struct {
    double first,last,bend_a,bend_b,width_a,width_b;
} ReachShape;

static ReachShape reach_shape(const MacroWorld *w,int cell) {
    Frame frame=qlcg_surface_frame(w,cell);
    int prev=incoming(w,cell,1,NULL),next=w->receiver[cell];
    Point dst=center(w,next),forward=qlcg_frame_local(&frame,dst.x,dst.z);
    double length=hypot(forward.x,forward.z);
    double slope=fabs((double)w->spill[cell]-w->spill[next])/fmax(1e-6,w->height_scale)/fmax(1,length);
    double turn=0;
    if (prev>=0) {
        Point src=center(w,prev),back=qlcg_frame_local(&frame,src.x,src.z);
        turn=fabs(atan2(back.z*forward.x-back.x*forward.z,-back.x*forward.x-back.z*forward.z));
    }
    uint32_t seed=w->opt.seed^((uint32_t)(cell+1)*0x9e3779b9u);
    double first=.24+.16*random_next(&seed),last=.62+.16*random_next(&seed);
    double side=random_next(&seed)<.5?-1:1;
    if (slope>.012 || w->uplift[cell]>.6)
        return (ReachShape){first,last,.42*side,.12*side,-.20,.08};
    if (slope<.006 && turn>.42)
        return (ReachShape){first,last,side,-.40*side,.23,-.15};
    return (ReachShape){first,last,.75*side,-.25*side,-.14,.20};
}

static double reach_controls(const ReachShape *s,double t,double a,double b) {
    if (t<s->first) return a*smooth(t/s->first);
    if (t<s->last) return mix(a,b,smooth((t-s->first)/(s->last-s->first)));
    return b*smooth((1-t)/(1-s->last));
}

static Point reach_position(const Frame *frame,Point origin,double scale,const CurvePoint *p) {
    double dx=p->wx-origin.x;
    dx-=floor(dx/frame->C+.5)*frame->C;
    return (Point){dx*scale,p->wz-origin.z};
}

static int reach_clear(const MacroWorld *w,const RiverCurve *c,int cell,Point origin,
                       double scale,double size,ReachBounds bounds) {
    CurvePoint other[161];
    for (int k=-1;k<8;k++) {
        int edge=k<0?cell:w->neighbors[cell*8+k],r=w->receiver[edge];
        if (edge==c->cell || r<0 || r>=w->cell_count || !main_width(w,edge)) continue;
        Frame frame=qlcg_surface_frame(w,edge);
        int count=curve_base(w,edge,&frame,other);
        Point a=reach_position(&c->frame,origin,scale,&other[0]);
        for (int i=1;i<count;i++) {
            Point b=reach_position(&c->frame,origin,scale,&other[i]);
            double pad=1.25*fmax(other[i-1].width,other[i].width)+48+.05*size;
            if (fmin(a.x,b.x)-pad<=bounds.right && fmax(a.x,b.x)+pad>=bounds.left &&
                fmin(a.z,b.z)-pad<=bounds.bottom && fmax(a.z,b.z)+pad>=bounds.top) return 0;
            a=b;
        }
    }
    return 1;
}

static double reach_along(const CurvePoint *p,const CurvePoint *a,double dx,double dz) {
    return (p->x-a->x)*dx+(p->z-a->z)*dz;
}

static void shape_reach(const MacroWorld *w,RiverCurve *c,int cell,int first,int last) {
    int next=w->receiver[cell];
    if (last-first<6 || next<0 || next>=w->cell_count || w->water[cell]==1 ||
        qlcg_lake_overlaps_cell(w,cell)) return;
    for (int k=0;k<8;k++) {
        int water=w->water[w->neighbors[cell*8+k]];
        if (water==1 || water==2) return;
    }
    Point origin=center(w,cell);
    double size=w->circumference/w->columns;
    double scale=.98*(qlcg_world_flat(w)?1:cos((fabs(origin.z)+size*.5)/w->circumference*2*PI));
    if (scale<.25) return;
    const CurvePoint *a=&c->points[first],*b=&c->points[last];
    double dx=b->x-a->x,dz=b->z-a->z,length=hypot(dx,dz);
    if (length<256) return;
    dx/=length; dz/=length;
    for (int k=0;k<first;k++) if (reach_along(&c->points[k],a,dx,dz)>=0) return;
    for (int k=last+1;k<c->count;k++) if (reach_along(&c->points[k],a,dx,dz)<=length) return;

    ReachShape shape=reach_shape(w,cell);
    double limit=fmin(length*.08,size*scale*.05);
    CurvePoint candidate[161];
    Point start=reach_position(&c->frame,origin,scale,a),end=reach_position(&c->frame,origin,scale,b);
    double pad=fmax(a->width,b->width)+48;
    ReachBounds fixed={fmin(start.x,end.x)-pad,fmax(start.x,end.x)+pad,
                       fmin(start.z,end.z)-pad,fmax(start.z,end.z)+pad};
    for (int attempt=0;attempt<2;attempt++) {
        ReachBounds bounds=fixed;
        double prev=0,prev_offset=0,prev_heading=0,prev_span=0;
        int valid=1;
        for (int k=first+1;k<last;k++) {
            CurvePoint p=c->points[k];
            double along=reach_along(&p,a,dx,dz);
            if (along<=prev || along>=length) { valid=0; break; }
            double t=along/length;
            double offset=reach_controls(&shape,t,shape.bend_a,shape.bend_b)*limit;
            double base=reach_along(&p,a,-dz,dx);
            double blend=smooth(t/.22)*smooth((1-t)/.22);
            double move=clampd(offset-base,-limit,limit)*blend;
            p.x-=dz*move; p.z+=dx*move;
            p.width*=1+reach_controls(&shape,t,shape.width_a,shape.width_b)*(attempt?.5:1);
            Point g=qlcg_frame_coordinates(&c->frame,p.x,p.z);
            p.wx=g.x; p.wz=g.z;
            if (p.width>512) p.width=fmin(p.width,surface_width_limit(w,g.z)/2);
            Point local=reach_position(&c->frame,origin,scale,&p);
            double pad=p.width+48;
            double span=along-prev,heading=atan2(base+move-prev_offset,span);
            if (fabs(local.x)+pad>=size*scale*.5 || fabs(local.z)+pad>=size*.5 ||
                fabs(heading)>.61 || (prev_span>0 &&
                fabs(heading-prev_heading)*pad>fmin(prev_span,span))) { valid=0; break; }
            bounds.left=fmin(bounds.left,local.x-pad); bounds.right=fmax(bounds.right,local.x+pad);
            bounds.top=fmin(bounds.top,local.z-pad); bounds.bottom=fmax(bounds.bottom,local.z+pad);
            candidate[k]=p; prev=along; prev_offset=base+move;
            prev_heading=heading; prev_span=span;
        }
        double final_heading=atan2(-prev_offset,length-prev);
        if (valid && fabs(final_heading)<=.61 &&
            fabs(final_heading-prev_heading)*pad<=fmin(prev_span,length-prev) &&
            reach_clear(w,c,cell,origin,scale,size,bounds)) {
            memcpy(c->points+first+1,candidate+first+1,(size_t)(last-first-1)*sizeof(*candidate));
            return;
        }
        limit*=.5;
    }
}

static void shape_curve(const MacroWorld *w,RiverCurve *c) {
    int first=0;
    while (first<c->count) {
        const CurvePoint *start=&c->points[first];
        int cell=qlcg_macro_cell_at(w,start->wx,start->wz),end=first+1;
        while (end<c->count && qlcg_macro_cell_at(w,c->points[end].wx,c->points[end].wz)==cell) end++;
        if (cell==c->cell || cell==c->receiver) {
            int lock=(c->count+11)/12;
            int a=first==0?lock:first;
            int b=end==c->count?end-1-lock:end-1;
            if (b>a) shape_reach(w,c,cell,a,b);
        }
        first=end;
    }
}

static RiverCurve *curve_build(const MacroWorld *w,int cell) {
    int r=w->receiver[cell];
    if (r<0 || r>=w->cell_count || !main_width(w,cell)) return NULL;
    RiverCurve *c=calloc(1,sizeof(*c)); if (!c) return NULL;
    c->cell=cell; c->receiver=r; c->frame=qlcg_surface_frame(w,cell); c->sea_mouth=w->water[r]==1;
    RiverBuffer stored={0};
    if(qlcg_river_archive_get(w,RIVER_RECORD_MAIN,cell,&stored)) {
        if(!curve_record(w,c,&stored)||stored.at!=stored.size||!finish_curve(w,c,1)) { curve_free(c); return NULL; }
        return c;
    }
    c->count=curve_base(w,cell,&c->frame,c->points);
    shape_curve(w,c);
    c->depth_a=qlcg_is_river_head(w,cell)?.5:qlcg_macro_depth(w->flow[cell]);
    c->depth_b=qlcg_macro_depth(w->flow[r]);
    c->inlet.arc=c->outlet.arc=NAN;
    if (w->water[r]==1 || w->water[r]==2) {
        c->inlet.kind=w->water[r]==1?WATER_KIND_SEA:WATER_KIND_LAKE;
        c->inlet.level=receiver_level(w,r);
    }
    if (w->water[cell]==2) {
        c->outlet.kind=WATER_KIND_LAKE; c->outlet.level=receiver_level(w,cell);
    }
    if (w->level || w->lake_templates) {
        LakeShoreField first=qlcg_lake_field(w,c->points[0].wx,c->points[0].wz);
        if (first.lake && first.d<0 && first.bed<0 && first.d*first.bed>.02) {
            c->outlet.kind=WATER_KIND_LAKE; c->outlet.level=first.level;
        }
        CurvePoint *end=c->points+c->count-1;
        LakeShoreField last=qlcg_lake_field(w,end->wx,end->wz);
        if (!c->inlet.kind && last.lake && last.d<0 && last.bed<0 && last.d*last.bed>.02) {
            c->inlet.kind=WATER_KIND_LAKE; c->inlet.level=last.level;
        }
    }
    if (c->inlet.kind==WATER_KIND_LAKE && !mouth_wet(w,&c->inlet,c->points[c->count-1].wx,c->points[c->count-1].wz)) {
        Point p;
        if (qlcg_lake_channel_target(w,r,c->points[0].wx,c->points[0].wz,&p.x,&p.z)) {
            Point end=qlcg_frame_local(&c->frame,p.x,p.z);
            double dx=end.x-c->points[c->count-1].x,dz=end.z-c->points[c->count-1].z;
            for (int k=c->count/2;k<c->count;k++) {
                double t=smooth((k/(double)(c->count-1)-.5)*2);
                CurvePoint *v=c->points+k; v->x+=dx*t; v->z+=dz*t;
                Point q=qlcg_frame_coordinates(&c->frame,v->x,v->z); v->wx=q.x; v->wz=q.z;
            }
        }
    }
    if (!finish_curve(w,c,0)||!curve_store(w,c)) { curve_free(c); return NULL; }
    return c;
}
static struct RiverSurfaceCache *surface_cache_at(const MacroWorld *w) {
    MacroWorld *mutable=(MacroWorld *)w;
    if (w->river_surface && w->river_surface->revision!=w->surface_revision) qlcg_river_surface_free(mutable);
    if (!w->river_surface) {
        mutable->river_surface=calloc(1,sizeof(*w->river_surface));
        if (!w->river_surface) return NULL;
        mutable->river_surface->revision=w->surface_revision;
    }
    return w->river_surface;
}
static RiverCurve *curve_at(const MacroWorld *w,int cell) {
    struct RiverSurfaceCache *cache=surface_cache_at(w);
    if (!cache) return NULL;
    int slot=-1;
    for (int i=0;i<CURVE_LIMIT;i++) {
        RiverCurve *c=cache->items[i];
        if (c && c->cell==cell) { c->used=++cache->serial; return c; }
        if (!c) slot=i;
        else if (!c->profile_building &&
                 (slot<0 || (cache->items[slot] && c->used<cache->items[slot]->used))) slot=i;
    }
    if (slot<0) return NULL;
    RiverCurve *c=curve_build(w,cell); if (!c) return NULL;
    curve_free(cache->items[slot]); cache->items[slot]=c; c->used=++cache->serial; return c;
}

static int crosses_cell(const MacroWorld *w,int cell,const RiverCurve *c) {
    SurfacePoint origin=center(w,cell);
    double half=w->circumference/(double)w->columns/2,C=w->circumference;
    for (int k=1;k<c->count;k++) {
        const CurvePoint *a=&c->points[k-1],*b=&c->points[k];
        double ax=a->wx-origin.x,bx=b->wx-origin.x;
        ax-=floor(ax/C+.5)*C; bx-=floor(bx/C+.5)*C;
        double az=a->wz-origin.z,bz=b->wz-origin.z,dx=bx-ax,dz=bz-az,lo=0,hi=1;
        double p[4]={-dx,dx,-dz,dz},q[4]={ax+half,half-ax,az+half,half-az};
        int valid=1;
        for (int j=0;j<4;j++) {
            if (fabs(p[j])<1e-9) { if (q[j]<0) valid=0; }
            else { double t=q[j]/p[j]; if (p[j]<0) lo=fmax(lo,t); else hi=fmin(hi,t); }
        }
        if (valid && lo<=hi) return 1;
    }
    return 0;
}
int qlcg_macro_cell_has_water(const MacroWorld *w,int cell) {
    if (w->water[cell]==1 || w->water[cell]==2 || main_width(w,cell)>0) return 1;
    for (int k=0;k<8;k++) {
        int i=w->neighbors[cell*8+k];
        if (!main_width(w,i)) continue;
        RiverCurve *c=curve_at(w,i);
        if (c && crosses_cell(w,cell,c)) return 1;
    }
    int edges[25],count=qlcg_lake_river_candidates(w,cell,edges,25);
    for (int k=0;k<count;k++) {
        if (!main_width(w,edges[k])) continue;
        RiverCurve *c=curve_at(w,edges[k]);
        if (c && crosses_cell(w,cell,c)) return 1;
    }
    return 0;
}

static int append_river_candidate(const MacroWorld *w,Point origin,int edge,RiverDetailCandidates *out) {
    int r=w->receiver[edge];
    if (r<0 || r>=w->cell_count || !main_width(w,edge)) return 1;
    RiverCurve *c=curve_at(w,edge);
    if (!c) return 0;
    ReachBounds b=c->detail_bounds;
    double half=w->circumference/w->columns*.5,C=w->circumference;
    double lon=origin.x-c->origin_x;
    lon-=floor(lon/C+.5)*C;
    if (origin.z+half<b.top || origin.z-half>b.bottom ||
        lon+half<b.left || lon-half>b.right) return 1;
    for (int i=0;i<out->count;i++) if (out->edges[i]==edge) return 1;
    if (out->count<25) out->edges[out->count++]=edge;
    return 1;
}
int qlcg_river_detail_candidates(const MacroWorld *w,int cell,RiverDetailCandidates *out) {
    struct RiverSurfaceCache *cache=surface_cache_at(w);
    if (!cache) return 0;
    RiverCellCache *entry=&cache->cells[(cell^(cell>>8))&(RIVER_CELL_CACHE-1)];
    if (entry->key==cell+1) { *out=entry->list; return 1; }
    Point origin=center(w,cell);
    int x=cell%w->columns,z=cell/w->columns;
    *out=(RiverDetailCandidates){0};
    for (int k=-1;k<8;k++) {
        int edge=k<0?cell:w->neighbors[cell*8+k];
        if (!append_river_candidate(w,origin,edge,out)) return 0;
    }
    int edges[25],count=qlcg_lake_river_candidates(w,cell,edges,25);
    for (int k=0;k<count;k++) if (!append_river_candidate(w,origin,edges[k],out)) return 0;
    for (int dz=-2;dz<=2;dz++) for (int dx=-2;dx<=2;dx++) {
        if (abs(dx)<2 && abs(dz)<2) continue;
        if (!append_river_candidate(w,origin,qlcg_macro_index(w,x+dx,z+dz),out)) return 0;
    }
    entry->key=cell+1;
    entry->list=*out;
    return 1;
}

static int in_detail_bounds(const RiverCurve *c,double wx,double wz) {
    ReachBounds b=c->detail_bounds;
    double C=c->frame.C,lon=wx-c->origin_x;
    lon-=floor(lon/C+.5)*C;
    return wz>=b.top && wz<=b.bottom && lon>=b.left && lon<=b.right;
}
double qlcg_river_detail_gate(const MacroWorld *w,const RiverDetailCandidates *list,double wx,double wz,
                              RiverValleySample *valley) {
    double gate=1;
    *valley=(RiverValleySample){1,1};
    RiverCurve *preview=w->river_surface?w->river_surface->detail_preview:NULL;
    for (int i=preview?-1:0;i<list->count;i++) {
        if(i>=0 && preview && list->edges[i]==preview->cell) continue;
        RiverCurve *c=i<0?preview:curve_at(w,list->edges[i]);
        if (!c) return 0;
        if (!in_detail_bounds(c,wx,wz)) continue;
        Point p=qlcg_frame_local(&c->frame,wx,wz);
        double distance,t;
        if (qlcg_river_index_nearest(&c->spatial,c->points,p.x,p.z,&distance,&t)<0) continue;
        gate=fmin(gate,smooth((distance-c->detail_clear)/c->detail_fade));
        if (gate==0) break;
        double edge=distance-c->detail_clear-12;
        valley->protection=fmin(valley->protection,smooth(edge/48));
        if (c->valley_span>0 && w->water[c->cell]!=1 && w->water[c->cell]!=2 &&
            w->water[c->receiver]!=2)
            valley->position=fmin(valley->position,edge/c->valley_span);
    }
    return gate;
}
static int arc_end(const RiverCurve *c,double arc) {
    int lo=0,hi=c->count-1;
    while (lo<hi) { int mid=(lo+hi)/2; if (c->points[mid].arc<arc) lo=mid+1; else hi=mid; }
    return lo;
}
static double profile_arc_at(const RiverCurve *c,double arc) {
    if(c->authored!=2) return arc;
    int hi=arc_end(c,arc),lo=hi?hi-1:0;
    double t=clampd((arc-c->points[lo].arc)/fmax(1e-9,c->points[hi].arc-c->points[lo].arc),0,1);
    return mix(c->parameters[lo],c->parameters[hi],t);
}
static double geometry_arc_at(const RiverCurve *c,double parameter) {
    if(c->authored!=2) return parameter;
    int lo=0,hi=c->count-1;
    while(hi-lo>1) { int mid=(lo+hi)/2; if(c->parameters[mid]>parameter) hi=mid; else lo=mid; }
    double t=clampd((parameter-c->parameters[lo])/fmax(1e-9,c->parameters[hi]-c->parameters[lo]),0,1);
    return mix(c->points[lo].arc,c->points[hi].arc,t);
}
static int crafted_at(const RiverCurve *c,double arc) {
    return c->authored==1;
}
static double crafted_weight(const RiverCurve *c,double arc) {
    return c->authored==1?1:0;
}
static Point pos_at(const RiverCurve *c,double arc) {
    int lo=arc_end(c,arc);
    const CurvePoint *a=&c->points[lo?lo-1:0],*b=&c->points[lo];
    double span=b->arc-a->arc,t=span>0?clampd((arc-a->arc)/span,0,1):0;
    return (Point){mix(a->x,b->x,t),mix(a->z,b->z,t)};
}

static double oxbow_offset(const float *stations,int count,double along) {
    double at=along*(count-1);
    int i=(int)fmin(count-2,at);
    double t=smooth(at-i);
    return mix(stations[i],stations[i+1],t);
}

static Point oxbow_arc(const OxbowPlan *p,double angle,double radius) {
    double x=radius*p->aspect*cos(angle),z=radius*sin(angle);
    return (Point){p->x+x*p->axis_x-z*p->axis_z,p->z+x*p->axis_z+z*p->axis_x};
}

static double oxbow_downstream(const OxbowPlan *p,double angle) {
    return p->downstream_side*sin(angle)/sin(fmin(p->half_angle,PI*.5));
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
        p->radius_offset[i]=(float)((2*random_next(&seed)-1)*radial_limit);

    double bank_extension=0;
    for (int bank=0;bank<2;bank++) {
        float lobes[OXBOW_SHAPE_STATIONS];
        for (int i=0;i<OXBOW_SHAPE_STATIONS;i++) lobes[i]=(float)(2*random_next(&seed)-1);
        float *offsets=bank?p->outer_bank:p->inner_bank;
        for (int i=0;i<OXBOW_BANK_STATIONS;i++) {
            double along=i/(double)(OXBOW_BANK_STATIONS-1);
            double down=oxbow_downstream(p,p->half_angle*(2*along-1));
            double coarse=oxbow_offset(lobes,OXBOW_SHAPE_STATIONS,along);
            double detail=2*random_next(&seed)-1;
            double width=shore[kind].width+.38*down+shore[kind].lobes*coarse+shore[kind].roughness*detail;
            offsets[i]=(float)((clampd(width,.50,1.80)-1)*p->half_width);
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
    Point origin=center(w,cell),anchor=qlcg_frame_coordinates(&p->frame,p->x,p->z);
    double C=w->circumference,half=C/w->columns*.5;
    double scale=.98*cos((fabs(origin.z)+half)/C*2*PI);
    double x=anchor.x-origin.x;
    x-=floor(x/C+.5)*C;
    if (fabs(x)*scale+p->extent+4>=half*scale ||
        fabs(anchor.z-origin.z)+p->extent+4>=half) return 0;
    double spare=fmin((half-fabs(x))*scale,half-fabs(anchor.z-origin.z))-p->extent-4;
    RiverDetailCandidates candidates;
    if (!qlcg_river_detail_candidates(w,cell,&candidates)) return 0;
    for (int i=0;i<candidates.count;i++) {
        RiverCurve *c=curve_at(w,candidates.edges[i]);
        if (!c) return 0;
        Point q=qlcg_frame_local(&c->frame,anchor.x,anchor.z);
        double distance,t;
        int segment=qlcg_river_index_nearest(&c->spatial,c->points,q.x,q.z,&distance,&t);
        if (segment<0) continue;
        double clearance=c->detail_clear+24;
        if (c->cell==cell)
            clearance=fmax(clearance,2.2*mix(c->points[segment].width,c->points[segment+1].width,t));
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
    RiverCurve *c=curve_at(w,cell);
    if (!c) return 0;
    uint32_t seed=w->opt.seed^((uint32_t)cell*0x6d2b79f5u)^0xc47f293du;
    double total=c->points[c->count-1].arc,arc=total*(.15+.17*random_next(&seed));
    double a0=arc-fmin(32,arc*.25),a1=arc+fmin(32,(total-arc)*.25);
    Point a=pos_at(c,a0),b=pos_at(c,a1),anchor=pos_at(c,arc);
    double length=hypot(b.x-a.x,b.z-a.z);
    if (length<1) return 0;
    double tx=(b.x-a.x)/length,tz=(b.z-a.z)/length;
    int hi=arc_end(c,arc);
    double width=fmax(c->points[hi?hi-1:0].width,c->points[hi].width);
    double clearance=fmax(2.28*width,c->detail_clear+36);
    p->frame=c->frame;
    p->type=(int)(random_next(&seed)*3);
    p->variant=(int)(random_next(&seed)*4);
    double variant=p->variant/3.0;
    static const double radii[3][2]={{44,80},{82,158},{126,232}};
    static const double angles[3][2]={{150,166},{83,106},{37,58}};
    static const double widths[3][2]={{9,17},{12,24},{8,16}};
    static const double depths[3][2]={{2.5,3.9},{2,3.5},{1.5,2.8}};
    static const double aspect[3]={1,1.14,.89},tips[3]={.26,.39,.48};
    int type=p->type;
    p->radius=mix(radii[type][0],radii[type][1],variant);
    p->half_angle=mix(angles[type][0],angles[type][1],variant)*PI/180;
    p->half_width=mix(widths[type][0],widths[type][1],variant);
    p->depth=mix(depths[type][0],depths[type][1],variant);
    p->aspect=aspect[type]; p->tip=tips[type];
    uint32_t shape_seed=seed^0x60e2d799u;
    double aspect_limit=fmax(1,p->aspect);
    p->extent=p->radius*aspect_limit+p->half_width+14;
    double side=random_next(&seed)<.5?-1:1;
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
    if (cell<0 || cell>=w->cell_count || w->water[cell]!=3 || !main_width(w,cell) ||
        w->receiver[cell]<0 || w->receiver[cell]>=w->cell_count) return;
    uint32_t seed=w->opt.seed^((uint32_t)cell*0x85ebca6bu)^0x0db04a61u;
    if (random_next(&seed)>=.03) return;
    if(!w->oxbow_catalog) {
        w->oxbow_catalog=calloc(1,sizeof(*w->oxbow_catalog)); if(!w->oxbow_catalog) return;
        w->oxbow_catalog->lookup=calloc((size_t)w->cell_count,sizeof(int));
        if(!w->oxbow_catalog->lookup) { free(w->oxbow_catalog); w->oxbow_catalog=NULL; return; }
    }
    struct OxbowCatalog *c=w->oxbow_catalog; if(c->lookup[cell]) return;
    c->lookup[cell]=-1; OxbowPlan plan={0}; RiverBuffer record={0}; int32_t valid=0;
    if(qlcg_river_archive_get(w,RIVER_RECORD_OXBOW,cell,&record)) {
        qlcg_river_io(&record,&valid,sizeof(valid));
        if(valid) qlcg_river_io(&record,&plan,sizeof(plan));
        if(record.failed||record.at!=record.size) return;
    } else {
        valid=build_oxbow(w,cell,&plan);
        qlcg_river_io(&record,&valid,sizeof(valid));
        if(valid) qlcg_river_io(&record,&plan,sizeof(plan));
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
    double a=clampd(atan2(z,u),-p->half_angle,p->half_angle);
    double along=(a/p->half_angle+1)*.5;
    double radius=p->radius+oxbow_offset(p->radius_offset,OXBOW_SHAPE_STATIONS,along);
    double taper=smooth((p->half_angle-fabs(a))/p->tip);
    double inner=(p->half_width+oxbow_offset(p->inner_bank,OXBOW_BANK_STATIONS,along))*taper;
    double outer=(p->half_width+oxbow_offset(p->outer_bank,OXBOW_BANK_STATIONS,along))*taper;
    double width=u*u+z*z>radius*radius?outer:inner;
    Point arc=oxbow_arc(p,a,radius);
    double edge=hypot(q.x-arc.x,q.z-arc.z)-width;
    if (edge>=14) return;
    double rim=p->level+.30;
    if (edge<0) {
        double depth=p->depth*(1+.36*oxbow_downstream(p,a));
        double basin=(depth+.30)*smooth(fmin(inner,outer)/1.33);
        out->height=(float)(rim-basin*smooth(-edge/fmax(1e-6,width*.75)));
        if (out->height<p->level-.025) {
            out->kind=WATER_KIND_LAKE;
            out->level=(float)p->level;
            out->decorative=1;
            out->width=(float)(inner+outer);
        }
    } else {
        double berm=rim+.46*smooth(edge/6);
        out->height=(float)mix(berm,out->height,smooth((edge-6)/8));
    }
    out->fresh=1;
    out->sediment=(float)fmax(out->sediment,.48*(1-smooth(fabs(edge)/8)));
    out->damp=(float)fmax(out->damp,.80*(1-smooth(fmax(0,edge)/14)));
}
typedef struct { const MacroWorld *w; const RiverCurve *c; } GroundContext;
static RiverGround ground_at(void *context,double arc) {
    GroundContext *g=context; Point p=pos_at(g->c,arc),q=qlcg_frame_coordinates(&g->c->frame,p.x,p.z);
    double h=qlcg_sample_natural_height(g->w,q.x,q.z);
    int i=qlcg_world_cell_at_wxz(g->w,(float)q.x,(float)q.z);
    int sea=g->w->water[i]==1 || (g->c->sea_mouth && h<=0);
    double lake=NAN;
    if (g->w->water[i]==2 && g->w->lake_id && g->w->lake_id[i]>=0 && g->w->lake_id[i]<g->w->lake_count)
        lake=g->w->lakes[g->w->lake_id[i]].level;
    return (RiverGround){sea?fmax(0,h):h,lake,sea};
}
static double depth_at(const RiverCurve *c,double arc) {
    double length=c->authored==2?c->profile.arc:c->points[c->count-1].arc,t=length>0?profile_arc_at(c,arc)/length:0;
    return mix(c->depth_a,c->depth_b,t*t*(3-2*t));
}
static int ensure_ground(const MacroWorld *w,RiverCurve *c) {
    if(c->authored==1) return 1;
    if(c->ground_profile.count) return 1;
    if(c->profile_building) return 0;
    GroundContext context={w,c};c->profile_building=1;
    int ok=qlcg_river_profile_build(&c->ground_profile,c->profile.arc,ground_at,&context);
    c->profile_building=0;
    if(ok) ok=curve_store(w,c);
    if(!ok) qlcg_river_profile_free(&c->ground_profile);
    return ok;
}
static double river_level_at(const RiverCurve *c,double arc,double lateral) {
    double parameter=profile_arc_at(c,arc),authored=qlcg_river_profile_level(&c->profile,parameter);
    if(!c->ground_profile.count) return authored;
    double ground=qlcg_river_profile_level(&c->ground_profile,parameter);
    double depth=fmin(3,fmax(.6,depth_at(c,arc)*.5));
    return mix(clampd(authored,ground-depth,ground+.15),authored,crafted_weight(c,arc));
}
static double crossfall_level(const MacroWorld *w,RiverCurve *c,Point point,Point direction,
                              double arc,double radius,double offset,double natural,double level,
                              double depth,double blend,double *grade) {
    if(!c->ground_profile.count) return level;
    double reach=clampd(radius*1.1,96,384);
    double a=qlcg_river_profile_level(&c->ground_profile,profile_arc_at(c,arc-reach));
    double b=qlcg_river_profile_level(&c->ground_profile,profile_arc_at(c,arc+reach));
    double gate=smooth((a-b-20)/24);
    if(gate<=0) return level;
    double high=river_level_at(c,arc-reach,0),low=river_level_at(c,arc+reach,0);
    if(high<=low+20) return level;
    double center=qlcg_river_profile_level(&c->ground_profile,profile_arc_at(c,arc));
    double freeboard=clampd(river_level_at(c,arc,0)-center,-3,.15),target=clampd(natural+freeboard,low,high);
    level=mix(level,fmax(level,target),gate);
    *grade=fmax(*grade,gate*smooth((level-low)/4)*smooth((high-level)/4));
    if(qlcg_river_archive_terrain(w)>=3 && !crafted_at(c,arc)) {
        double length=c->points[c->count-1].arc;
        double fade=smooth(arc/128)*smooth((length-arc)/128)*blend;
        double recess=gate*fade*qlcg_macro_fall_recess(w->opt.seed,c->cell,profile_arc_at(c,arc),offset,radius);
        if(recess>.01) {
            Point sample=qlcg_frame_coordinates(&c->frame,point.x+direction.x*recess,point.z+direction.z*recess);
            int building=c->profile_building;c->profile_building=1;
            double eroded=qlcg_natural_detail_height(w,sample.x,sample.z);
            c->profile_building=building;
            double limit=fmax(20,fmin(32,depth*6));
            double next=arc+recess,water=river_level_at(c,next,0);
            double ground=qlcg_river_profile_level(&c->ground_profile,profile_arc_at(c,next));
            double fitted=clampd(eroded+clampd(water-ground,-3,.15),low,high);
            water=fmin(water,eroded+.15);
            water=mix(water,fmax(water,fitted),gate);
            level=fmin(level,fmax(fmax(low,natural-limit+depth),water));
        }
    }
    return level;
}
static int ensure_profile(const MacroWorld *w,RiverCurve *c) {
    if (c->profile_ready) return ensure_ground(w,c);
    if (c->profile_building) return 0;
    double length=c->points[c->count-1].arc; GroundContext ctx={w,c};
    double start=w->river_level?w->river_level[c->cell]:NAN;
    double end=w->river_level?w->river_level[c->receiver]:NAN;
    if (isfinite(c->outlet.arc)) start=c->outlet.level;
    if (c->inlet.kind) end=c->inlet.level;
    int bound=isfinite(start);
    if (bound && !isfinite(end)) {
        int r=c->receiver;
        end=w->water[r]==1?0:w->water[r]==2?w->level[r]:w->spill[r];
    }
    if (bound) end=fmin(start,end);
    c->profile_building=1;
    int built;
    if (bound) {
        c->profile.count=(int)fmax(2,ceil(length/8)+1); c->profile.arc=length;
        c->profile.levels=malloc((size_t)c->profile.count*sizeof(float));
        c->profile.stations=2; built=c->profile.levels!=NULL;
    } else built=qlcg_river_profile_build(&c->profile,length,ground_at,&ctx);
    c->profile_building=0;
    if (!built) return 0;
    double previous=INFINITY;
    for (int i=0;i<c->profile.count;i++) {
        double arc=i*8.0;
        if (bound) {
            double finish=isfinite(c->inlet.arc)?fmax(1,c->inlet.arc-c->inlet.blend*.25):length;
            double begin=isfinite(c->outlet.arc)?c->outlet.arc:0;
            if (finish<=begin && isfinite(c->inlet.arc)) {
                begin=isfinite(c->outlet.arc)?c->outlet.arc:0; finish=c->inlet.arc;
            }
            c->profile.levels[i]=(float)mix(start,end,smooth((arc-begin)/fmax(1,finish-begin)));
            continue;
        }
        double raw=c->profile.levels[i],fb=fmin(1.2+depth_at(c,arc)*.35,3.2);
        double level=c->sea_mouth?fmax(0,raw-fb*(1-clampd((arc/fmax(1,length)-.55)/.45,0,1))):
            raw>=0?fmax(0,raw-fb):raw-fb;
        if (isfinite(end)) {
            level=fmin(previous,fmax(end,level));
            double finish=isfinite(c->inlet.arc)?fmax(0,c->inlet.arc-c->inlet.blend*.25):length;
            double begin=isfinite(c->inlet.arc)?fmax(0,finish-c->inlet.blend):length*.75;
            level=mix(level,end,smooth((arc-begin)/fmax(1,finish-begin)));
            previous=level;
        }
        c->profile.levels[i]=(float)level;
    }
    c->profile_ready=1;
    c->waterfall=(MacroWaterfall){.state=1};
    if(ensure_ground(w,c)) return 1;
    c->profile_ready=0; qlcg_river_profile_free(&c->profile); return 0;
}
static CurvePoint curve_station(const RiverCurve *c,double arc) {
    int hi=arc_end(c,arc),lo=hi?hi-1:0;
    const CurvePoint *a=c->points+lo,*b=c->points+hi;
    double t=b->arc>a->arc?clampd((arc-a->arc)/(b->arc-a->arc),0,1):0;
    if (t==0) return *a;
    if (t==1) return *b;
    CurvePoint p={0}; p.x=mix(a->x,b->x,t); p.z=mix(a->z,b->z,t);
    Point q=qlcg_frame_coordinates(&c->frame,p.x,p.z);
    p.wx=q.x; p.wz=q.z; p.arc=arc; p.width=mix(a->width,b->width,t);
    return p;
}
static int visible_path(const RiverCurve *c,CurvePoint *out,int capacity) {
    double first=isfinite(c->outlet.arc)?c->outlet.arc:0;
    double last=isfinite(c->inlet.arc)?c->inlet.arc:c->points[c->count-1].arc;
    if (capacity<2 || last<=first) return 0;
    int n=0; out[n++]=curve_station(c,first);
    for (int k=0;k<c->count && n<capacity-1;k++)
        if (c->points[k].arc>first && c->points[k].arc<last) out[n++]=c->points[k];
    out[n++]=curve_station(c,last);
    return n;
}
int qlcg_river_map_path(const MacroWorld *w,int cell,RiverMapPoint *out,int capacity) {
    if (cell<0 || cell>=w->cell_count || !out || capacity<=0) return 0;
    RiverCurve *c=curve_at(w,cell); if (!c) return 0;
    CurvePoint points[161];
    int count=visible_path(c,points,capacity<161?capacity:161);
    for (int k=0;k<count;k++) {
        const CurvePoint *p=points+k;
        out[k]=(RiverMapPoint){p->wx,p->wz,p->width};
    }
    return count;
}
int qlcg_river_mouth(const MacroWorld *w,int cell,int outlet,RiverMouthPoint *out) {
    if (cell<0 || cell>=w->cell_count || !out) return 0;
    RiverCurve *c=curve_at(w,cell); if (!c) return 0;
    const RiverMouthPoint *m=outlet?&c->outlet:&c->inlet;
    if (!m->kind || !isfinite(m->arc)) return 0;
    *out=*m; return 1;
}

int qlcg_river_path(const MacroWorld *w,int cell,RiverPathPoint *out,int capacity) {
    if (cell<0 || cell>=w->cell_count) return 0;
    RiverCurve *c=curve_at(w,cell); if (!c || !ensure_profile(w,c)) return 0;
    CurvePoint points[161];
    int count=visible_path(c,points,capacity<161?capacity:161);
    for (int k=0;k<count;k++) {
        CurvePoint *p=points+k;
        out[k]=(RiverPathPoint){p->wx,p->wz,p->arc,river_level_at(c,p->arc,0),depth_at(c,p->arc),p->width};
    }
    return count;
}

int qlcg_river_edit_read_main(const MacroWorld *w,int cell,RiverEdit *edit) {
    RiverCurve *c=curve_at(w,cell); if(!c||!ensure_profile(w,c)) return 0;
    edit->kind=RIVER_RECORD_MAIN; edit->cell=cell; edit->source=-1; edit->count=c->count;
    double previous=qlcg_river_profile_level(&c->profile,0);
    double bottom=qlcg_river_profile_level(&c->profile,c->profile.arc);
    for(int k=0;k<c->count;k++) {
        const CurvePoint *p=c->points+k;
        double parameter=profile_arc_at(c,p->arc);
        previous=clampd(qlcg_river_profile_level(&c->profile,parameter),bottom,previous);
        edit->nodes[k]=(RiverPathPoint){p->wx,p->wz,parameter,previous,depth_at(c,p->arc),p->width};
    }
    return 1;
}
typedef struct { GroundContext ground; double begin; } EditGroundContext;
static RiverGround edit_ground_at(void *context,double arc) {
    EditGroundContext *g=context;
    return ground_at(&g->ground,geometry_arc_at(g->ground.c,g->begin+arc));
}
static int copy_profile(RiverProfile *out,const RiverProfile *in) {
    *out=*in; out->levels=NULL;
    if(!in->count) return 1;
    out->levels=malloc((size_t)in->count*sizeof(float)); if(!out->levels) return 0;
    memcpy(out->levels,in->levels,(size_t)in->count*sizeof(float)); return 1;
}
static int edit_profiles(const MacroWorld *w,RiverCurve *c,const RiverCurve *old,const RiverEdit *edit) {
    if(!copy_profile(&c->profile,&old->profile)||!copy_profile(&c->ground_profile,&old->ground_profile)) return 0;
    unsigned char changed[161]={0}; c->authored=2;
    for(int k=0;k<c->count;k++) {
        c->parameters[k]=edit->nodes[k].arc; c->saved_curvature[k]=c->points[k].curvature; c->crafted[k]=0;
        double before=geometry_arc_at(old,c->parameters[k]); Point p=pos_at(old,before);
        if(hypot(p.x-c->points[k].x,p.z-c->points[k].z)<.001) {
            int hi=arc_end(old,before),lo=hi?hi-1:0;
            double t=clampd((before-old->points[lo].arc)/fmax(1e-9,old->points[hi].arc-old->points[lo].arc),0,1);
            c->points[k].curvature=c->saved_curvature[k]=mix(old->points[lo].curvature,old->points[hi].curvature,t);
        } else { changed[k]=1; if(k+1<c->count) changed[k+1]=1; }
        if(k) {
            double a=geometry_arc_at(old,c->parameters[k-1]),b=before;
            if(fabs((b-a)-(c->points[k].arc-c->points[k-1].arc))>.001) changed[k]=1;
            for(int j=arc_end(old,a);j<=arc_end(old,b)&&j<old->count;j++)
                if(old->authored==1||(old->authored==2&&old->crafted[j])) c->crafted[k]=1;
        }
    }
    for(int k=1;k<c->count;k++) if(changed[k]) c->crafted[k]=1;
    configure_crafted(c);
    for(int first=1;first<c->count;first++) {
        if(!changed[first]) continue;
        int last=first; while(last+1<c->count&&changed[last+1]) last++;
        double begin=c->parameters[first-1],end=c->parameters[last];
        EditGroundContext context={{w,c},begin}; RiverProfile ground={0};
        if(!qlcg_river_profile_build(&ground,end-begin,edit_ground_at,&context)) return 0;
        double top=river_level_at(old,geometry_arc_at(old,begin),0),bottom=river_level_at(old,geometry_arc_at(old,end),0),previous=top;
        for(int j=(int)ceil(begin/8);j<c->profile.count&&j*8.0<=end;j++) {
            double at=j*8.0,g=qlcg_river_profile_level(&ground,at-begin),depth=fmin(3,fmax(.6,depth_at(c,geometry_arc_at(c,at))*.5));
            double water=top>=bottom?clampd(g-depth,bottom,previous):river_level_at(old,geometry_arc_at(old,at),0);
            c->profile.levels[j]=(float)water; previous=water;
            if(c->ground_profile.count) c->ground_profile.levels[j]=(float)g;
        }
        qlcg_river_profile_free(&ground); first=last;
    }
    if(isfinite(c->inlet.arc)) c->inlet.arc=geometry_arc_at(c,profile_arc_at(old,old->inlet.arc));
    if(isfinite(c->outlet.arc)) c->outlet.arc=geometry_arc_at(c,profile_arc_at(old,old->outlet.arc));
    return 1;
}
int qlcg_river_edit_main(MacroWorld *w,const RiverEdit *edit) {
    if(!w||!edit||edit->cell<0||edit->cell>=w->cell_count||edit->count<2||edit->count>161) return RIVER_EDIT_INVALID;
    RiverCurve *old=curve_at(w,edit->cell); if(!old||!ensure_profile(w,old)) return RIVER_EDIT_INVALID;
    int unchanged=edit->count==old->count;
    double expected=qlcg_river_profile_level(&old->profile,0),bottom=qlcg_river_profile_level(&old->profile,old->profile.arc);
    for(int k=0;unchanged&&k<edit->count;k++) {
        const RiverPathPoint *p=edit->nodes+k; const CurvePoint *q=old->points+k;
        expected=clampd(qlcg_river_profile_level(&old->profile,profile_arc_at(old,q->arc)),bottom,expected);
        if(p->wx!=q->wx||p->wz!=q->wz||p->half_width!=q->width||fabs(p->level-expected)>1e-7) unchanged=0;
    }
    if(unchanged) return RIVER_EDIT_OK;
    RiverCurve *c=malloc(sizeof(*c)); if(!c) return RIVER_EDIT_MEMORY;
    *c=*old; c->waterfall=(MacroWaterfall){.state=1}; c->spatial=(RiverVectorIndex){0}; c->profile=(RiverProfile){0}; c->ground_profile=(RiverProfile){0}; c->count=edit->count;
    c->authored=0;
    int status=RIVER_EDIT_OK;
    for(int k=0;k<c->count;k++) {
        RiverPathPoint p=edit->nodes[k];
        if(!isfinite(p.wx)||!isfinite(p.wz)||!isfinite(p.level)||!isfinite(p.half_width)) { curve_free(c); return RIVER_EDIT_INVALID; }
        SurfacePoint q=qlcg_frame_local(&c->frame,p.wx,p.wz);
        int cell=qlcg_world_cell_at_wxz(w,(float)p.wx,(float)p.wz);
        if(cell!=c->cell&&cell!=c->receiver) {
            int nearby=0; for(int j=0;j<8;j++) if(w->neighbors[c->cell*8+j]==cell) nearby=1;
            if(!nearby) status=RIVER_EDIT_OUTSIDE;
        }
        if(p.half_width<=0||p.half_width>surface_width_limit(w,p.wz)*.5) status=RIVER_EDIT_INVALID;
        c->points[k]=(CurvePoint){p.wx,p.wz,q.x,q.z,0,p.half_width,0,p.level,p.depth};
    }
    for(int i=0;i<2;i++) {
        const CurvePoint *a=c->points+(i?c->count-1:0),*b=old->points+(i?old->count-1:0);
        double level=qlcg_river_profile_level(&old->profile,profile_arc_at(old,b->arc));
        if(!i) level=fmax(level,qlcg_river_profile_level(&old->profile,old->profile.arc));
        if(hypot(a->x-b->x,a->z-b->z)>.01||fabs(a->level-level)>.002) status=RIVER_EDIT_ENDPOINT;
    }
    if(!status) status=qlcg_river_edit_validate(c->points,c->count);
    old->profile_building=1;
    RiverDetailCandidates candidates={0};
    if(!status&&!qlcg_river_detail_candidates(w,c->cell,&candidates)) status=RIVER_EDIT_MEMORY;
    for(int i=0;!status&&i<candidates.count;i++) {
        int id=candidates.edges[i]; if(id==c->cell) continue;
        RiverPathPoint path[161]; int count=qlcg_river_path(w,id,path,161); CurvePoint p[161];
        for(int j=0;j<count;j++) { SurfacePoint q=qlcg_frame_local(&c->frame,path[j].wx,path[j].wz); p[j]=(CurvePoint){0}; p[j].x=q.x; p[j].z=q.z; }
        for(int k=1;!status&&k<c->count;k++) for(int j=1;j<count;j++) {
            if(!qlcg_river_segments_cross(c->points+k-1,c->points+k,p+j-1,p+j)) continue;
            int joined=(k==1&&(hypot(c->points[0].x-p[j].x,c->points[0].z-p[j].z)<.05||hypot(c->points[0].x-p[j-1].x,c->points[0].z-p[j-1].z)<.05))||
                (k==c->count-1&&(hypot(c->points[k].x-p[j].x,c->points[k].z-p[j].z)<.05||hypot(c->points[k].x-p[j-1].x,c->points[k].z-p[j-1].z)<.05));
            if(!joined) { status=RIVER_EDIT_LOOP; break; }
        }
    }
    if(!status&&!finish_curve(w,c,1)) status=RIVER_EDIT_MEMORY;
    if(!status) {
        w->river_surface->detail_preview=c;
        if(!edit_profiles(w,c,old,edit)) status=RIVER_EDIT_MEMORY;
        w->river_surface->detail_preview=NULL;
    }
    RiverArchiveUpdate updates[19]={0}; int update_count=1,visited[18],visited_count=0;
    updates[0].kind=RIVER_RECORD_MAIN; updates[0].cell=c->cell;
    if(!status&&!curve_record(w,c,&updates[0].buffer)) status=RIVER_EDIT_MEMORY;
    for(int side=0;!status&&side<2;side++) for(int j=-1;!status&&j<8;j++) {
        int cell=side?c->receiver:c->cell; if(j>=0) cell=w->neighbors[cell*8+j];
        int seen=0; for(int k=0;k<visited_count;k++) if(visited[k]==cell) seen=1;
        if(seen) continue; visited[visited_count++]=cell;
        const SourceRiverNetwork *n=qlcg_source_network(w,cell,0); if(!n) continue;
        unsigned char detached[4096]={0}; int detach_count=0;
        for(int e=0;e<n->edge_count;e++) if(n->edges[e].target_kind==0&&n->edges[e].target==c->cell) {
            const RiverVectorPoint *end=n->edges[e].points+n->edges[e].count-1;
            Point q=qlcg_frame_local(&c->frame,end->wx,end->wz); double distance,t,previous=0,u;
            int near=qlcg_river_index_nearest(&c->spatial,c->points,q.x,q.z,&distance,&t);
            int before=qlcg_river_index_nearest(&old->spatial,old->points,q.x,q.z,&previous,&u);
            if(near<0) { detached[e]=1; detach_count++; continue; }
            double level=river_level_at(c,mix(c->points[near].arc,c->points[near+1].arc,t),0);
            double was=before<0?level:river_level_at(old,mix(old->points[before].arc,old->points[before+1].arc,u),0);
            if((distance>mix(c->points[near].width,c->points[near+1].width,t)+.05&&distance>previous+.05)||
               (level>was+.02&&level>end->level+.02)) { detached[e]=1; detach_count++; }
        }
        if(detach_count) {
            RiverArchiveUpdate *update=updates+update_count++;
            update->kind=RIVER_RECORD_SOURCE; update->cell=cell;
            if(!qlcg_source_detach_prepare(n,detached,&update->buffer)) status=RIVER_EDIT_MEMORY;
        }
    }
    if(!status&&!qlcg_river_archive_commit(w,updates,update_count)) status=RIVER_EDIT_MEMORY;
    for(int k=0;k<update_count;k++) qlcg_river_buffer_free(&updates[k].buffer);
    old->profile_building=0;
    curve_free(c);
    if(!status) w->surface_revision++;
    return status;
}

static int build_junction(const MacroWorld *w,int cell,JunctionSlot *slot) {
    if (w->water[cell]==1 || qlcg_lake_overlaps_cell(w,cell)) return 0;
    int count=1;
    slot->edges[0]=cell;
    for (int k=0;k<8;k++) {
        int i=w->neighbors[cell*8+k];
        if (w->receiver[i]!=cell || !main_width(w,i)) continue;
        if (count==3) return 0;
        slot->edges[count++]=i;
    }
    if (count!=3) return 0;
    RiverCurve *down=curve_at(w,cell);
    if (!down || down->sea_mouth) return 0;
    RiverJunction *plan=&slot->plan;
    slot->frame=down->frame;
    double span=w->circumference/w->columns*cos(slot->frame.lat);
    double last=down->points[down->count-1].arc;
    plan->width=down->points[0].width;
    plan->radius=fmin(fmin(5.5*plan->width,.34*span),last*.42);
    if (plan->radius<32 || plan->radius<1.45*plan->width || !ensure_profile(w,down)) return 0;
    plan->level=river_level_at(down,0,0);
    plan->depth=fmax(1.2,down->depth_a);
    Point p=pos_at(down,plan->radius);
    if (hypot(p.x,p.z)<1) return 0;
    qlcg_junction_configure(plan,w->opt.seed^((uint32_t)cell*0x9e3779b9u),p.x,p.z);
    return 1;
}

static const JunctionSlot *junction_at(const MacroWorld *w,int cell) {
    if (!main_width(w,cell) || w->receiver[cell]<0) return NULL;
    struct RiverSurfaceCache *cache=surface_cache_at(w);
    if (!cache) return NULL;
    JunctionSlot *slot=&cache->junctions[(cell^(cell>>8))&(RIVER_CELL_CACHE-1)];
    if (slot->key!=cell+1) {
        slot->key=cell+1;
        slot->valid=build_junction(w,cell,slot);
    }
    return slot->valid?slot:NULL;
}

static int junction_edge(const JunctionSlot *join,int cell) {
    return cell==join->edges[0] || cell==join->edges[1] || cell==join->edges[2];
}

static void decorate_junction(const RiverJunction *plan,Point point,double weight,
                               double core,RiverSurfaceSample *out) {
    if (out->kind!=WATER_KIND_RIVER || out->height>=out->level-.02f) return;
    JunctionRelief field;
    qlcg_junction_relief(plan,point.x,point.z,&field);
    double raise=1.58*field.bar+.64*field.shelf;
    double bed=out->height+plan->depth*weight*(raise-.58*field.scour-.42*field.branch);
    double guard=smooth((core+.12)/.24);
    double dry_limit=out->level+plan->depth*.14;
    double wet_limit=out->level-fmin(.35,fmax(.05,plan->depth*.08));
    if (raise>0) {
        double emergence=(1-guard)*smooth((field.bar-.14)/.35);
        double ceiling=mix(wet_limit,dry_limit,emergence);
        bed=fmin(bed,fmax(out->height,ceiling));
    }
    out->height=(float)bed;
    out->sediment=(float)clampd(weight*(1.6*field.bar+field.shelf),0,1);
    if (bed>=out->level-.02) {
        out->kind=WATER_KIND_DRY;
        out->level=-1e9f;
    }
}

void qlcg_river_surface(const MacroWorld *w,float wx,float wz,float natural,RiverSurfaceSample *out) {
    int lake=out->kind==WATER_KIND_LAKE,sea=out->kind==WATER_KIND_SEA;
    double best=INFINITY,height=natural;
    int cell=qlcg_world_cell_at_wxz(w,wx,wz);
    const JunctionSlot *join=lake||sea?NULL:junction_at(w,cell);
    Point join_point={0};
    double weight=0,first=-INFINITY,second=-INFINITY,core=-INFINITY;
    int winner=-1;
    JunctionBank bank={0};
    if (join) {
        join_point=qlcg_frame_local(&join->frame,wx,wz);
        weight=qlcg_junction_weight(&join->plan,hypot(join_point.x,join_point.z));
    }
    RiverDetailCandidates list={0};
    int ready=qlcg_river_detail_candidates(w,cell,&list);
    for (int k=0;k<(ready?list.count:9);k++) {
        int i=ready?list.edges[k]:(k?w->neighbors[cell*8+k-1]:cell);
        if (!(main_width(w,i)>0) || w->receiver[i]<0) continue;
        RiverCurve *c=curve_at(w,i); if (!c || !in_detail_bounds(c,wx,wz)) continue;
        Point p=qlcg_frame_local(&c->frame,wx,wz);
        double distance,t;
        int near=qlcg_river_index_nearest(&c->spatial,c->points,p.x,p.z,&distance,&t);
        if (near<0) continue;
        CurvePoint *a=&c->points[near],*b=&c->points[near+1];
        double radius=mix(a->width,b->width,t);
        if (distance>radius+(c->authored?512:48)) continue;
        if ((lake||sea) && isfinite(c->inlet.arc) && c->inlet.arc==0 &&
            (!c->outlet.kind || c->outlet.arc>=c->points[c->count-1].arc)) continue;
        if (lake && c->outlet.kind && c->outlet.arc>=c->points[c->count-1].arc && !c->inlet.kind) continue;
        double arc=mix(a->arc,b->arc,t),level=out->level,depth=depth_at(c,arc);
        int authored=crafted_at(c,arc);
        if(!authored&&distance>radius+48) continue;
        if (!lake && !sea) {
            if (!ensure_profile(w,c)) continue;
            level=river_level_at(c,arc,0);
        }
        double water_blend=1;
        if (lake || sea) {
            water_blend=0;
            if (c->inlet.kind==out->kind && isfinite(c->inlet.arc) && fabs(c->inlet.level-out->level)<.02)
                water_blend=fmax(water_blend,1-smooth(fabs(arc-c->inlet.arc)/fmax(1,c->inlet.blend)));
            if (c->outlet.kind==out->kind && isfinite(c->outlet.arc) && fabs(c->outlet.level-out->level)<.02)
                water_blend=fmax(water_blend,1-smooth(fabs(c->outlet.arc-arc)/fmax(1,c->outlet.blend)));
            if (water_blend<=0) continue;
            level=out->level;
        }
        double vx=b->x-a->x,vz=b->z-a->z,span=hypot(vx,vz);
        double side=((p.x-a->x)*(-vz)+(p.z-a->z)*vx)>=0?1:-1;
        double curvature=mix(a->curvature,b->curvature,t);
        double fall_grade=0;
        if(!lake && !sea && !authored) {
            double ahead=qlcg_river_profile_level(&c->ground_profile,profile_arc_at(c,arc)-16);
            double below=qlcg_river_profile_level(&c->ground_profile,profile_arc_at(c,arc)+16);
            double upper=qlcg_river_profile_level(&c->ground_profile,profile_arc_at(c,arc)-64);
            double lower=qlcg_river_profile_level(&c->ground_profile,profile_arc_at(c,arc)+64);
            fall_grade=fmax(0,(ahead-below)/32)*smooth((upper-lower-20)/20);
            depth=fmin(4,depth);
            level=fmin(level,natural+.15);
            if(fall_grade>.7) out->waterfall_rock=1;
        }
        if(!lake && !sea && qlcg_river_archive_terrain(w)>=2)
            level=crossfall_level(w,c,p,(Point){vx/span,vz/span},arc,radius,side*distance,
                                 natural,level,depth,1-weight,&fall_grade);
        if (weight>0 && junction_edge(join,i)) {
            level=mix(level,join->plan.level,weight);
            double margin=radius-distance;
            core=fmax(core,.55-distance/fmax(1,radius));
            if (margin>first) {
                second=first;
                first=margin;
                bank=(JunctionBank){i,arc,side*distance,radius,depth,level,curvature};
            } else second=fmax(second,margin);
        }
        double bed; int wet;
        if(authored) {
            bed=natural;
            wet=qlcg_source_section_span(distance,radius,depth,natural,level,radius+512,&bed,&level);
            fall_grade=fmax(fall_grade,fmax(0,(river_level_at(c,arc-16,0)-river_level_at(c,arc+16,0))/32));
            double weight=crafted_weight(c,arc);
            if(weight<1) {
                double base=natural; int base_wet=0;
                qlcg_river_bank_section(w->opt.seed,i,profile_arc_at(c,arc),side*distance,radius,depth,level,natural,curvature,&base,&base_wet);
                bed=mix(base,bed,weight); wet=distance<radius&&bed<level-.02;
            }
        } else if (!qlcg_river_bank_section(w->opt.seed,i,profile_arc_at(c,arc),side*distance,radius,depth,level,natural,curvature,&bed,&wet)) continue;
        if(!lake && !sea && !authored) {
            if(qlcg_river_archive_terrain(w)>=2 && fall_grade>.7 && distance<radius)
                bed=fmin(bed,level-fmin(.5,depth)*smooth((radius-distance)/6));
            double limit=fmax(20,fmin(32,depth*6));
            bed=fmax(bed,natural-limit);
            wet=distance<radius && bed<level-.02;
        }
        if(authored&&distance<best) height=mix(natural,bed,water_blend);
        else height=fmin(height,mix(natural,bed,water_blend));
        if (lake || sea) continue;
        if(!wet && (!out->has_bank_level || level-bed>out->bank_level-height)) {
            out->bank_level=(float)fmin(level,bed);
            out->has_bank_level=1;
        }
        if (!wet || distance>=best) continue;
        best=distance;
        winner=i;
        out->level=(float)(out->kind==WATER_KIND_SEA?fmax(out->level,level):level);
        if (out->kind!=WATER_KIND_SEA) out->kind=fall_grade>.7?WATER_KIND_FALL:WATER_KIND_RIVER;
        double lon=wx/w->circumference*2*PI,lat=-wz/w->circumference*2*PI;
        double east[3]={-sin(lon),0,cos(lon)},north[3]={-sin(lat)*cos(lon),cos(lat),-sin(lat)*sin(lon)};
        double e=0,n=0;
        for (int d=0;d<3;d++) {
            double v=(c->frame.east[d]*vx+c->frame.south[d]*vz)/span;
            e+=v*east[d]; n+=v*north[d];
        }
        out->east=(float)e; out->north=(float)n; out->width=(float)(radius*2);
        out->bank_distance=(float)fmax(0.0,radius-distance);
        out->lateral=(float)clampd(distance/fmax(1e-6,radius),0.0,1.0);
        out->discharge=(float)w->flow[i];
        out->channel_depth=w->river_level && isfinite(w->river_level[i])?(float)depth:0;
        out->grade=(float)fmax(fall_grade,fmax(0,(river_level_at(c,arc-24,0)-river_level_at(c,arc+24,0))/48));
    }
    if (weight>0 && isfinite(second)) {
        double band=fmin(42,fmax(6,.16*bank.radius));
        double overlap=fmax(0,1-(first-second)/band);
        double rounded=.25*band*overlap*overlap*weight;
        double bed;
        int wet;
        if (rounded>0 && qlcg_river_bank_section(w->opt.seed,bank.cell,bank.arc,bank.offset,
                    bank.radius+rounded,bank.depth,bank.level,natural,bank.curvature,&bed,&wet)) {
            height=fmin(height,bed);
            if (wet && out->kind==WATER_KIND_DRY) {
                winner=bank.cell;
                out->kind=WATER_KIND_RIVER;
                out->level=(float)bank.level;
                out->east=(float)join->plan.east;
                out->north=(float)-join->plan.south;
                out->discharge=(float)w->flow[bank.cell];
                out->width=(float)(bank.radius*2);
                out->lateral=1;
            }
        }
    }
    out->height=(float)height;
    if (weight>0 && junction_edge(join,winner))
        decorate_junction(&join->plan,join_point,weight,core,out);
}
