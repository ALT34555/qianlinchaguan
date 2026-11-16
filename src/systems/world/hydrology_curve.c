/* 主河平面曲线与湖海口几何 */
#include "hydrology_river_internal.h"
#include "surface_internal.h"
#include "surface_macro.h"
#include "surface_detail.h"
#include "surface_coast.h"
#include "surface_islands.h"
#include "hydrology_lakes.h"
#include "gen/hydrology_dimensions.h"
#include <stdlib.h>
#include <string.h>

#define SURFACE_RIVER_MAX_WIDTH 6000.0

static int incoming(const MacroWorld *w,int node,int original,int *count) {
    int best=-1;
    if (count) *count=0;
    for (int k=0;k<8;k++) {
        int i=w->neighbors[node*8+k];
        if (w->receiver[i]!=node) continue;
        if (count && qlcg_river_main_width(w,i)>0) (*count)++;
        double width=original?w->river_width[i]:qlcg_river_main_width(w,i);
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
    Point a=qlcg_river_cell_center(w,prev>=0?prev:node),b=qlcg_river_cell_center(w,next>=0?next:node);
    Point p=qlcg_frame_local(f,a.x,a.z),q=qlcg_frame_local(f,b.x,b.z);
    double scale=length*.85/fmax(1,hypot(q.x-p.x,q.z-p.z));
    return (Point){(q.x-p.x)*scale,(q.z-p.z)*scale};
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
        Point p=qlcg_frame_coordinates(&c->frame,qlcg_mixd(a->x,b->x,t),qlcg_mixd(a->z,b->z,t));
        if (mouth_wet(w,mouth,p.x,p.z)==wet_a) lo=t; else hi=t;
    }
    double t=(lo+hi)*.5;
    Point p=qlcg_frame_coordinates(&c->frame,qlcg_mixd(a->x,b->x,t),qlcg_mixd(a->z,b->z,t));
    mouth->wx=p.x; mouth->wz=p.z; mouth->arc=qlcg_mixd(a->arc,b->arc,t);
    mouth->half_width=qlcg_mixd(a->width,b->width,t);
}

int qlcg_river_curve_finish(const MacroWorld *w,RiverCurve *c,int restored) {
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
        p->curvature=c->edit_mode==RIVER_CURVE_LOCAL_EDIT&&restored?c->saved_curvature[i]:qlcg_clampd(atan2(ax*bz-az*bx,ax*bx+az*bz)*p->width/fmax(1,span),-1,1);
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
        c->valley_span=qlcg_clampd(380+.42*max_width,400,1400);
        pad=fmax(pad,c->detail_clear+12+c->valley_span);
    }
    double lat=(fmax(fabs(b.top),fabs(b.bottom))+pad)*2*QLCG_RIVER_PI/C;
    double lon_pad=c->frame.flat?pad:lat<QLCG_RIVER_PI*.5?fmin(C*.5,pad/cos(lat)):C*.5;
    c->detail_bounds=(ReachBounds){b.left-lon_pad,b.right+lon_pad,b.top-pad,b.bottom+pad};
    if(qlcg_river_archive_terrain(w)>=12) {
        int head=qlcg_is_river_head(w,c->cell);
        for(int k=0;!head&&k<8;k++)head=qlcg_is_river_head(w,w->neighbors[c->cell*8+k]);
        if(head)pad=fmax(pad,c->detail_clear+w->circumference/w->columns*.25);
    }
    double extra=fmax(74,pad-2*min_width);
    c->spatial.bin_size=512;
    if (max_width>512 || pad>768)
        while (c->spatial.bin_size<max_width+extra*.5) c->spatial.bin_size*=2;

    return qlcg_river_index_build(&c->spatial,c->points,c->count,extra);
}

void qlcg_river_curve_free(RiverCurve *c) {
    if (!c) return;
    qlcg_river_profile_free(&c->ground_profile); qlcg_river_profile_free(&c->profile); qlcg_river_index_free(&c->spatial); free(c);
}

double qlcg_river_width_limit(const MacroWorld *w,double wz) {
    double span=w->circumference/w->columns*(qlcg_world_flat(w)?1:cos(wz/w->circumference*2*QLCG_RIVER_PI));
    return qlcg_clampd(.65*span,1024,SURFACE_RIVER_MAX_WIDTH);
}

static int curve_base(const MacroWorld *w,int cell,const Frame *frame,CurvePoint *points) {
    int r=w->receiver[cell];
    int sea_mouth=w->water[r]==1;
    Point origin=qlcg_river_cell_center(w,cell),end=qlcg_river_cell_center(w,r);
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
    if (!sea_mouth && inflows<2 && w->water[r]!=2 && qlcg_river_main_width(w,r)>0)
        end_width=qlcg_river_discharge_width(w->flow[r])/2;
    double gentle=1-qlcg_clampd(fabs((double)w->spill[cell]-w->spill[r])/fmax(1e-6,w->height_scale)/fmax(1,length)/.012,0,1);
    int bends=chunks<10?0:(int)fmin(3,fmax(1,floor((1+gentle*2)*qlcg_clampd(sqrt(chunks/625),.35,1)+.5)));
    double amplitude=fmin(w->circumference/(double)w->columns*.1,length/(bends+1)*.3)*(.2+.8*gentle);
    double side=sin(frame->lon*frame->radius/260000+sin(frame->lat*13)*2+(w->opt.seed%997))<0?-1:1;
    double knots[5]={0}; uint32_t rng=w->opt.seed^((uint32_t)(cell+1)*0x7f4a7c15u);
    for (int k=1;k<=bends;k++) knots[k]=side*(k%2?1:-1)*amplitude*(.65+qlcg_river_random_next(&rng)*.35);
    int steps=(int)fmin(160,fmax(24,floor(length/128+.5)));
    for (int k=0;k<=steps;k++) {
        double t=k/(double)steps,t2=t*t,t3=t2*t;
        int section=(int)fmin(bends,floor(t*(bends+1)));
        double u=qlcg_clampd(t*(bends+1)-section,0,1),wave=qlcg_mixd(knots[section],knots[section+1],qlcg_smooth01d(u));
        double x=begin.x*(2*t3-3*t2+1)+start_t.x*(t3-2*t2+t)+target.x*(-2*t3+3*t2)+end_t.x*(t3-t2)-(target.z-begin.z)/fmax(1,length)*wave;
        double z=begin.z*(2*t3-3*t2+1)+start_t.z*(t3-2*t2+t)+target.z*(-2*t3+3*t2)+end_t.z*(t3-t2)+(target.x-begin.x)/fmax(1,length)*wave;
        Point g=qlcg_frame_coordinates(frame,x,z);
        if (!k) g=origin;
        if (k==steps) g=end;
        Point p=qlcg_frame_local(frame,g.x,g.z);
        double width=qlcg_mixd(start_width,end_width,qlcg_smooth01d(t));
        if(head) width=qlcg_mixd(fmin(6,start_width*.12),width,qlcg_smooth01d(t*length/fmax(1,head_run)));
        if (width>512) width=fmin(qlcg_river_width_limit(w,g.z)/2,width);
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
    Point dst=qlcg_river_cell_center(w,next),forward=qlcg_frame_local(&frame,dst.x,dst.z);
    double length=hypot(forward.x,forward.z);
    double slope=fabs((double)w->spill[cell]-w->spill[next])/fmax(1e-6,w->height_scale)/fmax(1,length);
    double turn=0;
    if (prev>=0) {
        Point src=qlcg_river_cell_center(w,prev),back=qlcg_frame_local(&frame,src.x,src.z);
        turn=fabs(atan2(back.z*forward.x-back.x*forward.z,-back.x*forward.x-back.z*forward.z));
    }
    uint32_t seed=w->opt.seed^((uint32_t)(cell+1)*0x9e3779b9u);
    double first=.24+.16*qlcg_river_random_next(&seed),last=.62+.16*qlcg_river_random_next(&seed);
    double side=qlcg_river_random_next(&seed)<.5?-1:1;
    if (slope>.012 || w->uplift[cell]>.6)
        return (ReachShape){first,last,.42*side,.12*side,-.20,.08};
    if (slope<.006 && turn>.42)
        return (ReachShape){first,last,side,-.40*side,.23,-.15};
    return (ReachShape){first,last,.75*side,-.25*side,-.14,.20};
}

static double reach_controls(const ReachShape *s,double t,double a,double b) {
    if (t<s->first) return a*qlcg_smooth01d(t/s->first);
    if (t<s->last) return qlcg_mixd(a,b,qlcg_smooth01d((t-s->first)/(s->last-s->first)));
    return b*qlcg_smooth01d((1-t)/(1-s->last));
}

static Point reach_position(const Frame *frame,Point origin,double scale,const CurvePoint *p) {
    double dx=p->wx-origin.x;
    dx-=floor(dx/frame->C+.5)*frame->C;
    return (Point){dx*scale,p->wz-origin.z};
}

static int reach_clear(const MacroWorld *w,const RiverCurve *c,int cell,Point origin,
                       double scale,double size,ReachBounds bounds) {
    CurvePoint other[RIVER_CURVE_POINTS];
    for (int k=-1;k<8;k++) {
        int edge=k<0?cell:w->neighbors[cell*8+k],r=w->receiver[edge];
        if (edge==c->cell || r<0 || r>=w->cell_count || !qlcg_river_main_width(w,edge)) continue;
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
    Point origin=qlcg_river_cell_center(w,cell);
    double size=w->circumference/w->columns;
    double scale=.98*(qlcg_world_flat(w)?1:cos((fabs(origin.z)+size*.5)/w->circumference*2*QLCG_RIVER_PI));
    if (scale<.25) return;
    const CurvePoint *a=&c->points[first],*b=&c->points[last];
    double dx=b->x-a->x,dz=b->z-a->z,length=hypot(dx,dz);
    if (length<256) return;
    dx/=length; dz/=length;
    for (int k=0;k<first;k++) if (reach_along(&c->points[k],a,dx,dz)>=0) return;
    for (int k=last+1;k<c->count;k++) if (reach_along(&c->points[k],a,dx,dz)<=length) return;

    ReachShape shape=reach_shape(w,cell);
    double limit=fmin(length*.08,size*scale*.05);
    CurvePoint candidate[RIVER_CURVE_POINTS];
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
            double blend=qlcg_smooth01d(t/.22)*qlcg_smooth01d((1-t)/.22);
            double move=qlcg_clampd(offset-base,-limit,limit)*blend;
            p.x-=dz*move; p.z+=dx*move;
            p.width*=1+reach_controls(&shape,t,shape.width_a,shape.width_b)*(attempt?.5:1);
            Point g=qlcg_frame_coordinates(&c->frame,p.x,p.z);
            p.wx=g.x; p.wz=g.z;
            if (p.width>512) p.width=fmin(p.width,qlcg_river_width_limit(w,g.z)/2);
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

RiverCurve *qlcg_river_curve_build(const MacroWorld *w,int cell) {
    int r=w->receiver[cell];
    if (r<0 || r>=w->cell_count || !qlcg_river_main_width(w,cell)) return NULL;
    RiverCurve *c=calloc(1,sizeof(*c)); if (!c) return NULL;
    c->cell=cell; c->receiver=r; c->frame=qlcg_surface_frame(w,cell); c->sea_mouth=w->water[r]==1;
    RiverBuffer stored={0};
    if(qlcg_river_archive_get(w,RIVER_RECORD_MAIN,cell,&stored)) {
        if(!qlcg_river_curve_record(w,c,&stored)||stored.at!=stored.size||!qlcg_river_curve_finish(w,c,1)) { qlcg_river_curve_free(c); return NULL; }
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
                double t=qlcg_smooth01d((k/(double)(c->count-1)-.5)*2);
                CurvePoint *v=c->points+k; v->x+=dx*t; v->z+=dz*t;
                Point q=qlcg_frame_coordinates(&c->frame,v->x,v->z); v->wx=q.x; v->wz=q.z;
            }
        }
    }
    if (!qlcg_river_curve_finish(w,c,0)||!qlcg_river_curve_store(w,c)) { qlcg_river_curve_free(c); return NULL; }
    return c;
}
