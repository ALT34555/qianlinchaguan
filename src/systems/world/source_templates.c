/* 装饰河线模板与贴地剖面 */
#include "source_rivers.h"
#include "surface_internal.h"
#include "surface_lakes.h"
#include "gen/gen_water.h"
#include "gen/gen_noise.h"
#include <math.h>
#include <string.h>

static double clamp(double x,double a,double b) { return fmax(a,fmin(b,x)); }
static double smooth(double x) { x=clamp(x,0,1); return x*x*(3-2*x); }
static double mix(double a,double b,double t) { return a+(b-a)*t; }
static double random_next(uint32_t *seed) {
    uint32_t t=(*seed+=0x6d2b79f5u);
    t=(t^(t>>15))*(t|1u); t^=t+(t^(t>>7))*(t|61u);
    return (t^(t>>14))/4294967296.0;
}
void qlcg_source_dimensions(double rain,double evaporation,int columns,int snow,RiverSource *out) {
    static const double ranges[3][2][4]={
        {{1,12,.2,.8},{.5,2,.2,.5}},
        {{1,24,.2,1.6},{.5,3,.2,.8}},
        {{1,32,.2,2.5},{.5,4,.2,2}}
    };
    const double *r=ranges[columns<=256?0:columns<=512?1:2][snow!=0];
    out->discharge=qlcg_runoff_m3s(rain,evaporation,1);
    double strength=-expm1(-out->discharge/.04);
    out->half_width=(r[0]+(r[1]-r[0])*sqrt(strength))/2;
    out->min_half_width=r[0]/2; out->depth=r[2]+(r[3]-r[2])*strength;
    out->snowmelt=snow;
}
int qlcg_source_template(double ax,double az,double bx,double bz,uint32_t seed,int attempt,RiverVectorPoint *out) {
    static const double forms[4][5][2]={
        {{0,0},{.28,.65},{.68,1},{1,0}},
        {{0,0},{.23,.85},{.71,-.5},{1,0}},
        {{0,0},{.18,.4},{.48,.95},{.81,-.35},{1,0}},
        {{0,0},{.37,.18},{.76,-.28},{1,0}}
    };
    double dx=bx-ax,dz=bz-az,length=hypot(dx,dz); if(!length) return 0;
    uint32_t rng=seed^((uint32_t)(attempt+1)*0x9e3779b1u);
    int form=(int)(random_next(&rng)*4),knots=form==2?5:4;
    double side=random_next(&rng)<.5?-1:1,amplitude=fmin(length*.085,420)*(.75+random_next(&rng)*.5);
    double ts[5],vs[5],slopes[4],tangents[5];
    for(int k=0;k<knots;k++) {
        ts[k]=forms[form][k][0]; vs[k]=0;
        if(k&&k<knots-1) {
            ts[k]+=(random_next(&rng)-.5)*.07;
            vs[k]=forms[form][k][1]*(.75+random_next(&rng)*.5)*side;
        }
    }
    for(int k=0;k<knots-1;k++) slopes[k]=(vs[k+1]-vs[k])/(ts[k+1]-ts[k]);
    for(int k=0;k<knots;k++) {
        if(!k) tangents[k]=slopes[0];
        else if(k==knots-1) tangents[k]=slopes[k-1];
        else tangents[k]=slopes[k-1]*slopes[k]<=0?0:2*slopes[k-1]*slopes[k]/(slopes[k-1]+slopes[k]);
    }
    int steps=(int)clamp(ceil(length/32),24,SOURCE_POINT_LIMIT-1),section=0;
    double period=fmax(96,fmin(length*.7,180+(seed&255)*1.2));
    memset(out,0,(size_t)(steps+1)*sizeof(*out));
    for(int k=0;k<=steps;k++) {
        double t=k/(double)steps;
        while(section<knots-2&&t>ts[section+1]) section++;
        double span=ts[section+1]-ts[section],u=(t-ts[section])/span,u2=u*u,u3=u2*u;
        double offset=amplitude*((2*u3-3*u2+1)*vs[section]+(u3-2*u2+u)*span*tangents[section]+
            (-2*u3+3*u2)*vs[section+1]+(u3-u2)*span*tangents[section+1]);
        double s=t*length/period;
        double ripple=.68*qlcg_base_noise_d(s,7,0,seed^0x62656e64u)+
            .24*qlcg_base_noise_d(s*2.1,19,0,seed^0x72697632u)+.08*qlcg_base_noise_d(s*4.3,31,0,seed^0x72697633u);
        offset+=fmin(length*.07,fmin(160,period*.32))*ripple*smooth(t*length/fmin(64,length*.10))*
            smooth((1-t)*length/fmin(96,length*.14));
        out[k].x=ax+dx*t-dz/length*offset; out[k].z=az+dz*t+dx/length*offset;
    }
    out[0].x=ax; out[0].z=az; out[steps].x=bx; out[steps].z=bz;
    return steps+1;
}
static RiverVectorPoint point_at(const SurfaceFrame *frame,const RiverVectorPoint *p,int count,double arc,int *index) {
    int lo=0,hi=count-1;
    while(hi-lo>1) { int m=(lo+hi)/2; if(p[m].arc>arc) hi=m; else lo=m; }
    double t=clamp((arc-p[lo].arc)/fmax(1e-9,p[hi].arc-p[lo].arc),0,1);
    RiverVectorPoint q={0}; q.x=mix(p[lo].x,p[hi].x,t); q.z=mix(p[lo].z,p[hi].z,t); q.arc=arc;
    SurfacePoint g=qlcg_frame_coordinates(frame,q.x,q.z); q.wx=g.x; q.wz=g.z;
    if(index) *index=hi;
    return q;
}
static RiverSurfaceSample receiver_at(const MacroWorld *w,RiverVectorPoint p,double natural) {
    RiverSurfaceSample q={0}; q.height=(float)natural; q.level=-1e9f;
    qlcg_river_surface(w,(float)p.wx,(float)p.wz,(float)natural,&q); return q;
}
static int receiver_wet(RiverSurfaceSample q) {
    return (q.kind==WATER_KIND_RIVER||q.kind==WATER_KIND_FALL)&&q.level>q.height+.05;
}
int qlcg_source_profile(const MacroWorld *w,const SurfaceFrame *frame,int cell,
                         RiverSource *source,RiverVectorPoint *p,int count,int target_source,double join_distance,
                         SourceProfile *profile) {
    if(count<2) return 0;
    p[0].arc=0;
    for(int k=1;k<count;k++) p[k].arc=p[k-1].arc+hypot(p[k].x-p[k-1].x,p[k].z-p[k-1].z);
    double total=p[count-1].arc,start=source->level,end=p[count-1].level;
    if(total<64||total>32768||!isfinite(start)||!isfinite(end)||start<=end) return 0;
    double maximum=fmin(16,fmin(source->half_width,total*.01));
    double minimum=source->min_half_width;
    if(maximum<minimum) return 0;
    int stations=(int)clamp(ceil(total/220),4,14);
    double ground[14],filled[14];
    memset(profile,0,sizeof(*profile)); profile->count=stations;
    for(int k=0;k<stations;k++) {
        int at=(int)floor(k*(count-1)/(double)(stations-1)+.5);
        profile->arc[k]=p[at].arc;
        if(!k) { ground[k]=start+.8; continue; }
        SurfacePoint g=qlcg_frame_coordinates(frame,p[at].x,p[at].z);
        if(qlcg_world_cell_at_wxz(w,(float)g.x,(float)g.z)!=cell) ground[k]=NAN;
        else {
            LakeShoreField lake=qlcg_lake_field(w,g.x,g.z);
            ground[k]=qlcg_lake_height(&lake,qlcg_sample_natural_height(w,(float)g.x,(float)g.z));
            if(target_source==SOURCE_TARGET_LAKE && lake.lake && lake.d<0 && lake.bed<0) ground[k]=lake.level+.12;
            if(target_source==SOURCE_TARGET_LAKE&&k==stations-1) ground[k]=end+.12;
        }
        if(!target_source&&isfinite(ground[k])) {
            RiverVectorPoint q=p[at]; q.wx=g.x; q.wz=g.z;
            RiverSurfaceSample hit=receiver_at(w,q,ground[k]);
            if(receiver_wet(hit)) {
                double lo=profile->arc[k-1],hi=q.arc;
                for(int j=0;j<10&&hi-lo>.25;j++) {
                    double mid=(lo+hi)*.5;
                    RiverVectorPoint m=point_at(frame,p,count,mid,NULL);
                    double h=qlcg_sample_natural_height(w,(float)m.wx,(float)m.wz);
                    RiverSurfaceSample water=receiver_at(w,m,h);
                    if(receiver_wet(water)) { hi=mid; hit=water; } else lo=mid;
                }
                int last; p[at]=point_at(frame,p,count,hi,&last);
                if(last!=at) p[last]=p[at];
                count=last+1; total=hi; end=hit.level;
                profile->arc[k]=hi; ground[k]=end+.12; stations=k+1; profile->count=stations;
                break;
            }
        }
    }
    if(!target_source&&isfinite(ground[stations-1])) {
        SurfacePoint g=qlcg_frame_coordinates(frame,p[count-1].x,p[count-1].z);
        RiverSurfaceSample target={0}; target.height=(float)ground[stations-1]; target.level=-1e9f;
        qlcg_river_surface(w,(float)g.x,(float)g.z,target.height,&target);
        if(target.kind==WATER_KIND_RIVER) end=target.level;
    }
    if(start<=end) return 0;
    maximum=fmin(16,fmin(source->half_width,total*.01));
    if(total<64||maximum<minimum) return 0;
    /* 窄槽覆盖网格对角 */
    minimum=fmax(minimum,TERRAIN_GRID*1.4);
    maximum=fmax(minimum,maximum);
    double head=minimum;
    profile->mouth=target_source?fmin(total*.15,fmax(0,join_distance)):0;
    profile->transition=fmax(32,fmin(128,maximum*6));
    for(int k=0;k<stations;k++) {
        filled[k]=ground[k];
        for(int d=1;!isfinite(filled[k])&&d<stations;d++) {
            if(k-d>=0&&isfinite(ground[k-d])) filled[k]=ground[k-d];
            else if(k+d<stations&&isfinite(ground[k+d])) filled[k]=ground[k+d];
        }
        if(k&&k<stations-1&&isfinite(filled[k])&&filled[k]<end-.4) return 0;
    }
    double previous=start;
    profile->influence=fmax(48,maximum*5+24);
    const SourceForm *form=qlcg_source_form(source->spring_form);
    for(int k=0;k<stations;k++) {
        double raw=(filled[k?k-1:0]+2*filled[k]+filled[k+1<stations?k+1:k])/4;
        double water=!k?start:k==stations-1?end:fmax(end,fmin(previous,raw-.35));
        profile->level[k]=water; previous=water;
        profile->influence=fmax(profile->influence,fmin(512,fabs(filled[k]-water)*2.5+maximum*2+32));
        if(!k) continue;
        double span=profile->arc[k]-profile->arc[k-1],drop=profile->level[k-1]-water;
        int mode=drop>=1.5&&drop/fmax(1,span)>.025?form->drops:0;
        profile->mode[k-1]=mode;
        profile->run[k-1]=fmin(span*.75,fmax(6*mode,fmin(96*mode,drop*2.2+12)));
        profile->begin[k-1]=(span-profile->run[k-1])*(.30+.25*((source->seed>>(k%16))&7)/7.0);
    }
    double slopes[13];
    for(int k=0;k<stations-1;k++) slopes[k]=(profile->level[k+1]-profile->level[k])/fmax(1e-6,profile->arc[k+1]-profile->arc[k]);
    for(int k=0;k<stations;k++) {
        profile->tangent[k]=!k?slopes[0]:k==stations-1?0:
            slopes[k-1]*slopes[k]<=0?0:2*slopes[k-1]*slopes[k]/(slopes[k-1]+slopes[k]);
    }
    int station=0; previous=start;
    for(int k=0;k<count;k++) {
        double t=p[k].arc/total; int fall=0;
        p[k].level=qlcg_source_profile_value(profile,p[k].arc,NULL,&fall); p[k].fall=fall;
        while(station<stations-2&&p[k].arc>profile->arc[station+1]) station++;
        double fraction=(p[k].arc-profile->arc[station])/fmax(1e-6,profile->arc[station+1]-profile->arc[station]);
        double ground_at=mix(filled[station],filled[station+1],clamp(fraction,0,1));
        if(k&&k<count-1) {
            SurfacePoint g=qlcg_frame_coordinates(frame,p[k].x,p[k].z);
            if(fabs(p[k].arc-profile->arc[station])<1e-6) ground_at=filled[station];
            else if(fabs(p[k].arc-profile->arc[station+1])<1e-6) ground_at=filled[station+1];
            else {
                LakeShoreField lake=qlcg_lake_field(w,g.x,g.z);
                ground_at=qlcg_lake_height(&lake,qlcg_sample_natural_height(w,(float)g.x,(float)g.z));
            }
            if(!isfinite(ground_at)||ground_at+.4<end) return 0;
            p[k].level=fmax(end,fmin(previous,fmin(p[k].level,ground_at-.12)));
        }
        previous=p[k].level;
        profile->influence=fmax(profile->influence,fmin(512,fabs(ground_at-p[k].level)*2.5+maximum*2+32));
        p[k].width=head+(maximum-head)*smooth(t);
        double width_noise=qlcg_base_noise_d(p[k].arc/160,source->spring_form,0,source->seed^0x77696474u);
        p[k].width=fmax(minimum,p[k].width*(.85+.15*width_noise));
        p[k].depth=fmin(source->depth,fmin(p[k].width*2*.22,2.5));
        SurfacePoint g=qlcg_frame_coordinates(frame,p[k].x,p[k].z); p[k].wx=g.x; p[k].wz=g.z;
    }
    for(int k=0;k<count;k++) {
        double left=k?(p[k].level-p[k-1].level)/fmax(1e-6,p[k].arc-p[k-1].arc):0;
        double right=k<count-1?(p[k+1].level-p[k].level)/fmax(1e-6,p[k+1].arc-p[k].arc):0;
        p[k].slope=!k?right:k==count-1?0:left*right<=0?0:2*left*right/(left+right);
        p[k].fall=k<count-1&&p[k].level-p[k+1].level>=1.5&&-right>.025?form->drops:0;
    }
    profile->path=p; profile->path_count=count; profile->drops=form->drops;
    return count;
}
double qlcg_source_profile_value(const SourceProfile *p,double arc,double *grade,int *fall) {
    if(grade) *grade=0; if(fall) *fall=0;
    if(!p||p->count<2) return NAN;
    if(p->path_count>=2) {
        int lo=0,hi=p->path_count-1;
        while(hi-lo>1) { int mid=(lo+hi)/2; if(p->path[mid].arc>arc) hi=mid; else lo=mid; }
        const RiverVectorPoint *a=p->path+lo,*b=p->path+hi;
        double span=fmax(1e-6,b->arc-a->arc),u=clamp((arc-a->arc)/span,0,1),value,slope;
        double drop=a->level-b->level;
        int steps=(int)a->fall;
        if(steps) {
            double run=fmin(span*.8,fmax(4*steps,fmin(40*steps,drop*1.2+6))),begin=(span-run)*.45;
            double sum=0,derivative=0;
            for(int k=0;k<steps;k++) {
                double x=clamp((u*span-begin-run*k/steps)/(run/steps),0,1);
                sum+=smooth(x); derivative+=6*x*(1-x)/(run/steps);
            }
            value=a->level-drop*(.12*u+.88*sum/steps);
            slope=drop*(.12/span+.88*derivative/steps);
            if(fall) *fall=slope>.08;
        } else {
            double u2=u*u,u3=u2*u;
            value=(2*u3-3*u2+1)*a->level+(u3-2*u2+u)*span*a->slope+
                (-2*u3+3*u2)*b->level+(u3-u2)*span*b->slope;
            slope=-((6*u2-6*u)*a->level+(3*u2-4*u+1)*span*a->slope+
                (-6*u2+6*u)*b->level+(3*u2-2*u)*span*b->slope)/span;
        }
        double ceiling=mix(a->level,b->level,u);
        if(value>ceiling) { value=ceiling; slope=drop/span; if(fall&&steps) *fall=slope>.08; }
        if(grade) *grade=fmax(0,slope);
        return clamp(value,b->level,a->level);
    }
    int lo=0,hi=p->count-1;
    while(hi-lo>1) { int m=(lo+hi)/2; if(p->arc[m]>arc) hi=m; else lo=m; }
    double span=fmax(1e-6,p->arc[hi]-p->arc[lo]),u=clamp((arc-p->arc[lo])/span,0,1);
    double delta=p->level[lo]-p->level[hi],drop,slope;
    if(p->mode[lo]) {
        int steps=p->mode[lo]; double sum=0,derivative=0,run=p->run[lo]/steps;
        for(int k=0;k<steps;k++) {
            double x=clamp((u*span-p->begin[lo]-run*k)/fmax(1e-6,run),0,1);
            sum+=smooth(x); derivative+=6*x*(1-x)/fmax(1e-6,run);
        }
        drop=delta*(.08*u+.92*sum/steps); slope=delta*(.08/span+.92*derivative/steps);
        if(fall) *fall=slope>.08;
    } else {
        double u2=u*u,u3=u2*u,a=p->level[lo],b=p->level[hi],ta=p->tangent[lo],tb=p->tangent[hi];
        double value=(2*u3-3*u2+1)*a+(u3-2*u2+u)*span*ta+(-2*u3+3*u2)*b+(u3-u2)*span*tb;
        drop=a-value;
        slope=-((6*u2-6*u)*a+(3*u2-4*u+1)*span*ta+(-6*u2+6*u)*b+(3*u2-2*u)*span*tb)/span;
    }
    double value=p->level[lo]-clamp(drop,0,delta),end=p->level[p->count-1];
    if(p->mouth>0) {
        double x=clamp((arc-(p->arc[p->count-1]-p->mouth-p->transition))/p->transition,0,1);
        slope=(1-smooth(x))*slope+(value-end)*6*x*(1-x)/p->transition;
        value=mix(value,end,smooth(x));
        if(fall&&slope>.08&&value-end>1.5) *fall=1;
    }
    if(grade) *grade=fmax(0,slope);
    return value;
}
int qlcg_source_section(double distance,double radius,double depth,double natural,double level,
                         double *height,double *water) {
    return qlcg_source_section_span(distance,radius,depth,natural,level,512,height,water);
}
int qlcg_source_section_span(double distance,double radius,double depth,double natural,double level,
                              double span,double *height,double *water) {
    double valley=fmin(span,fmax(32,fmax(radius*4+20,fabs(natural-level)*2.5+radius*2+24)));
    if(distance>=valley||radius<=0) return 0;
    double bed=level-fmin(depth,fmin(radius*2*.22,2.5)),core=distance/radius,lip=fmin(level+.12,natural+.4);
    *height=core<1?mix(bed,lip,smooth(core)):
        fmin(natural+.4,mix(lip,natural,smooth((distance-radius)/fmax(1,valley-radius))));
    *water=level;
    return core<1&&*height<level-.025;
}
