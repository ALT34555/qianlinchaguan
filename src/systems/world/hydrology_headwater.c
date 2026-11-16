#include "hydrology_headwater.h"
#include "surface_detail.h"
#include "surface_internal.h"
#include "hydrology_archive.h"
#include "hydrology_fields.h"
#include "hydrology_river.h"
#include "gen/gen_noise.h"
#include <math.h>
#include <string.h>

static const char *NAMES[HEAD_KIND_COUNT]={
    "山坳涵泉","岩壁裂隙泉","砾堆渗流源","沙丘脚泉","湿地漫泉","阶潭悬泉","积雪岩隙源","高寒雪谷源"
};
static const struct { double melt_start,melt_peak,melt_fade,melt_end,snow_start,snow_end; } ART={-1,2,7,15,-2,8};
static double clamp(double x,double a,double b) { return fmax(a,fmin(b,x)); }
static double smooth(double x) { x=clamp(x,0,1);return x*x*(3-2*x); }
static double mix(double a,double b,double t) { return a+(b-a)*t; }
static double random_next(uint32_t *seed) {
    uint32_t t=(*seed+=0x6d2b79f5u);
    t=(t^(t>>15))*(t|1u);t^=t+(t^(t>>7))*(t|61u);
    return (t^(t>>14))/4294967296.0;
}
const char *qlcg_headwater_name(int kind) { return NAMES[kind>=0&&kind<HEAD_KIND_COUNT?kind:0]; }

static void design_shape(HeadwaterPlan *p) {
    if(p->design!=1)return;
    uint32_t rng=p->seed^0x6d617373u;HeadwaterDesign *s=&p->shape;
    s->family=(int)(random_next(&rng)*4);
    s->aspect=.75+.45*random_next(&rng);s->shear=(random_next(&rng)-.5)*.55;
    s->warp=.10+.20*random_next(&rng);s->ridge=.08+.24*random_next(&rng);
    s->lift=14+28*random_next(&rng);s->spread=.85+.38*random_next(&rng);
    s->bend=(random_next(&rng)-.5)*.38;
    for(int k=0;k<3;k++) {
        double *peak=s->peaks[k];
        peak[0]=-.16-.40*random_next(&rng);
        peak[1]=k?(k==1?-1:1)*(.25+.28*random_next(&rng)):(random_next(&rng)-.5)*.45;
        peak[2]=(k?.40:.75)+.40*random_next(&rng);peak[3]=.06+.12*random_next(&rng);
        if(s->family==0&&k)peak[2]*=.4;
        if(s->family==2) { peak[3]+=.10;peak[2]*=.72; }
        if(s->family==3&&k==1) { peak[2]*=1.4;peak[0]+=.18; }
    }
    for(int k=0;k<p->springs;k++) {
        double t=k/(double)(p->springs-1);
        s->angles[k]=(t*2-1)*s->spread+s->bend+(random_next(&rng)-.5)*.20;
        s->reaches[k]=fmax(78/p->radius,.40+.24*random_next(&rng));
    }
}
static int legacy_record(HeadwaterPlan *p,RiverBuffer *b) {
    uint32_t meta[5]={p->design==2?4:p->design?3:p->springs?2:1,(uint32_t)p->kind,(uint32_t)p->material,(uint32_t)p->cell,p->seed};
    qlcg_river_values(b,meta,5,4);
    if(b->failed||meta[0]<1||meta[0]>4||meta[1]>=HEAD_KIND_COUNT||meta[2]<1||meta[2]>5)return 0;
    double v[19]={p->x,p->z,p->dx,p->dz,p->radius,p->rise,p->pool_radius,p->skew,p->phase,
        p->source_ground,p->outlet_ground,p->source_level,p->outlet_level,p->run,p->moisture,p->cold,p->warm,p->springs,p->design==2?p->region:p->design};
    qlcg_river_values(b,v,19,8);
    for(int k=0;k<19;k++)if(!isfinite(v[k]))return 0;
    if(b->failed||v[4]<32||v[4]>(meta[0]==4?4096:meta[0]>=2?384:256)||v[5]<0||v[5]>(meta[0]==4?320:meta[0]==3?128:meta[0]==2?96:40)||(v[13]!=0&&v[13]<32)||v[13]>2048)return 0;
    if(meta[0]<4&&(v[6]<8||v[6]>28))return 0;
    if(meta[0]>=2&&(v[17]<3||v[17]>(meta[0]==4?9:5)||v[17]!=floor(v[17])||v[4]<(meta[0]==4?96:144)))return 0;
    if(meta[0]==3&&v[18]!=1)return 0;
    if(meta[0]==4&&(v[18]<0||v[18]>=HEAD_REGION_COUNT||v[18]!=floor(v[18])||v[6]<0||v[6]>240))return 0;
    if(fabs(hypot(v[2],v[3])-1)>.01||hypot(v[0],v[1])>(meta[0]==4?4096:512)||v[11]<v[12]||v[14]<0||v[14]>1||v[15]>v[16])return 0;
    if(b->reading) {
        p->kind=meta[1];p->material=meta[2];p->cell=meta[3];p->seed=meta[4];p->springs=meta[0]>=2?(int)v[17]:0;
        p->design=meta[0]==4?2:meta[0]==3;p->region=meta[0]==4?(int)v[18]:-1;
        p->x=v[0];p->z=v[1];p->dx=v[2];p->dz=v[3];p->radius=v[4];p->rise=v[5];p->pool_radius=v[6];
        p->skew=v[7];p->phase=v[8];p->source_ground=v[9];p->outlet_ground=v[10];p->source_level=v[11];
        p->outlet_level=v[12];p->run=v[13];p->moisture=v[14];p->cold=v[15];p->warm=v[16];
        design_shape(p);
        if(p->design==2)qlcg_headwater_region_shape(p);
    }
    return 1;
}
static int record(HeadwaterPlan *p,RiverBuffer *b) {
    uint32_t version=p->design==3?6:0;
    if(b->reading&&b->at<=b->size&&b->size-b->at>=4)memcpy(&version,b->data+b->at,4);
    if(version!=6)return legacy_record(p,b);
    uint32_t meta[9]={6,p->kind,p->material,p->cell,p->seed,p->region,p->variant,p->springs,p->shape.geometry_ready};
    qlcg_river_values(b,meta,9,4);
    if(b->failed||meta[1]>=HEAD_KIND_COUNT||meta[2]<1||meta[2]>5||meta[5]>=7||meta[6]>=4||meta[7]<3||meta[7]>12||meta[8]>1)return 0;
    qlcg_river_values(b,&p->shape.geometry_hash,1,8);
    if(meta[8]&&!p->shape.geometry_hash)return 0;
    double v[26]={p->x,p->z,p->dx,p->dz,p->radius,p->rise,p->pool_radius,p->skew,p->phase,
        p->source_ground,p->outlet_ground,p->source_level,p->outlet_level,p->run,p->moisture,p->cold,p->warm,
        p->span,p->span_x,p->center_x,p->center_z,p->lift,p->ridge_height,p->valley_width,p->grade,p->edge_width};
    qlcg_river_values(b,v,26,8);
    for(int k=0;k<26;k++)if(!isfinite(v[k]))return 0;
    if(b->failed||v[4]<32||v[4]>32768||v[5]<0||v[5]>6500||v[6]<0||v[6]>240||
       fabs(hypot(v[2],v[3])-1)>.01||v[11]<v[12]||v[13]<0||v[13]>4096||v[14]<0||v[14]>1||v[15]>v[16]||
       v[17]<640||v[17]>40001||v[18]<=0||v[18]>v[17]*1.01||v[21]<0||v[22]<0||v[23]<=0||v[24]<0||v[24]>.1||v[25]<=0)return 0;
    if(b->reading) {
        p->design=3;p->kind=meta[1];p->material=meta[2];p->cell=meta[3];p->seed=meta[4];
        p->region=meta[5];p->variant=meta[6];p->springs=meta[7];p->shape.geometry_ready=meta[8];
        p->x=v[0];p->z=v[1];p->dx=v[2];p->dz=v[3];p->radius=v[4];p->rise=v[5];p->pool_radius=v[6];p->skew=v[7];p->phase=v[8];
        p->source_ground=v[9];p->outlet_ground=v[10];p->source_level=v[11];p->outlet_level=v[12];p->run=v[13];p->moisture=v[14];p->cold=v[15];p->warm=v[16];
        p->span=v[17];p->span_x=v[18];p->center_x=v[19];p->center_z=v[20];p->lift=v[21];p->ridge_height=v[22];p->valley_width=v[23];p->grade=v[24];p->edge_width=v[25];
        p->shape.family=p->variant;p->shape.branch_count=p->springs;
    }
    for(int k=0;k<p->springs;k++) {
        HeadwaterValley *a=p->shape.branches+k;int32_t parent=a->parent,count=a->point_count;
        double q[10]={a->ax,a->az,a->bx,a->bz,a->start,a->end,a->width,a->fraction,a->target_x,a->target_z};
        qlcg_river_values(b,&parent,1,4);qlcg_river_values(b,&count,1,4);qlcg_river_values(b,q,10,8);
        for(int j=0;j<10;j++)if(!isfinite(q[j]))return 0;
        if(b->failed||count<0||count>1025||(meta[8]&&count<2)||(!k&&parent!=-1)||(k&&(parent<0||parent>=k))||q[4]<q[5]||q[6]<=0||q[6]>p->span||q[7]<0||q[7]>1||
           hypot(q[0],q[1])>p->span||hypot(q[2],q[3])>p->span)return 0;
        if(b->reading)*a=(HeadwaterValley){parent,count,q[0],q[1],q[2],q[3],q[4],q[5],q[6],q[7],q[8],q[9]};
    }
    return !b->failed;
}
int qlcg_headwater_store(const MacroWorld *w,const HeadwaterPlan *plan) {
    HeadwaterPlan copy=*plan;RiverBuffer b={0};
    int ok=record(&copy,&b)&&qlcg_river_archive_put((MacroWorld *)w,RIVER_RECORD_HEADWATER,plan->cell,&b);
    qlcg_river_buffer_free(&b);return ok;
}
int qlcg_headwater_validate(const unsigned char *data,size_t size,int cell) {
    HeadwaterPlan p={0};RiverBuffer b={(unsigned char *)data,size,0,0,1,0};
    return record(&p,&b)&&b.at==b.size&&p.cell==cell;
}
void qlcg_headwater_compact(const MacroWorld *w,int cell) {
    HeadwaterPlan *p=(HeadwaterPlan *)qlcg_headwater_plan(w,cell);if(!p)return;
    if(p->design==3)return;
    p->x=p->z=0;p->source_ground=p->outlet_ground;p->source_level=p->outlet_level;
    RiverBuffer b={0};
    if(record(p,&b))qlcg_river_archive_put((MacroWorld *)w,RIVER_RECORD_HEADWATER,cell,&b);
    qlcg_river_buffer_free(&b);
}

int qlcg_headwater_build(const MacroWorld *w,int cell,HeadwaterPlan *p) {
    const MacroCellPlan *macro=qlcg_macro_cell_plan(w,cell);
    if(!macro||!macro->head||qlcg_river_archive_terrain(w)<5)return 0;
    RiverBuffer stored={0};
    if(qlcg_river_archive_get(w,RIVER_RECORD_HEADWATER,cell,&stored))
        return record(p,&stored)&&stored.at==stored.size&&p->cell==cell;
    if(!qlcg_headwater_region_build(w,cell,p))return 0;
    RiverBuffer output={0};int ok=record(p,&output)&&qlcg_river_archive_put((MacroWorld *)w,RIVER_RECORD_HEADWATER,cell,&output);
    qlcg_river_buffer_free(&output);return ok;
}

static int local_point(const MacroWorld *w,const HeadwaterPlan *p,double wx,double wz,double reach,SurfacePoint *q) {
    const MacroCellPlan *macro=qlcg_macro_cell_plan(w,p->cell);if(!macro)return 0;
    const SurfaceFrame *f=&macro->source.frame;
    double cx=f->flat?f->anchor[0]:f->lon*f->C/6.283185307179586;
    double cz=f->flat?f->anchor[2]:-f->lat*f->C/6.283185307179586;
    if(fabs(wz-cz)>reach*1.01)return 0;
    double dx=wx-cx;
    if(!f->flat)dx-=floor(dx/f->C+.5)*f->C;
    double scale=f->flat?1:fmax(0,hypot(f->anchor[0],f->anchor[2])-reach/f->radius);
    if(fabs(dx)*scale>reach*1.6)return 0;
    *q=qlcg_frame_local(f,wx,wz);return 1;
}
static double mountain_height(const HeadwaterPlan *p,double u,double v,double r,double *edge) {
    const HeadwaterDesign *s=&p->shape;
    double broad=qlcg_base_noise_d(u*1.7+7,v*1.7+11,0,p->seed^0x74657272u);
    double x=u+s->shear*v+s->warp*broad,z=v/s->aspect;
    double fold=qlcg_base_noise_d(x*4.3,z*4.3,0,p->seed^0x6c616e64u);
    z+=s->warp*.65*fold;
    double grain=qlcg_base_noise_d(x*10.1,z*10.1,0,p->seed^0x73686170u);
    *edge=1-smooth((r-.65)/(.28+.05*broad+.02*fold));
    double mass=0,peak=0;
    for(int k=0;k<3;k++) {
        const double *a=s->peaks[k];double dx=x-a[0],dz=z-a[1];
        double lobe=a[2]*exp(-(dx*dx+dz*dz)/a[3]);
        mass+=lobe;peak=fmax(peak,lobe);
    }
    double ridge=1-fabs(fold),back=1-smooth((u+.02)/.58);
    mass=(peak*.70+mass*.28+s->ridge*ridge*ridge)*back;
    if(s->family==2)mass=smooth(mass/.95)*.95;
    mass=(mass+.16*(1-smooth(r)))*(1+.20*grain+.18*broad);
    return p->rise*(1-exp(-mass*1.5))*(*edge);
}
double qlcg_headwater_height(const MacroWorld *w,double wx,double wz,double base) {
    int cell=qlcg_world_cell_at_wxz(w,(float)wx,(float)wz);
    const HeadwaterPlan *p=qlcg_headwater_plan(w,cell);if(!p)return base;
    if(p->design==3) {
        if(!p->shape.geometry_ready) {
            if(!qlcg_headwater_geometry(w,cell))return base;
            p=qlcg_headwater_plan(w,cell);if(!p)return base;
        }
        return qlcg_headwater_landform_height(w,p,wx,wz,base,1);
    }
    SurfacePoint q;if(!local_point(w,p,wx,wz,p->radius+hypot(p->x,p->z)+8,&q))return base;
    if(p->design==2)return qlcg_headwater_region_height(p,q.x,q.z,base);
    double dx=q.x-p->x,dz=q.z-p->z;
    double u=(dx*p->dx+dz*p->dz)/p->radius,v=(-dx*p->dz+dz*p->dx)/p->radius;
    double r=hypot(u,v),edge=1-smooth((r-.65)/.35);if(edge<=0)return base;
    double hill=0;
    if(p->design)hill=mountain_height(p,u,v,r,&edge);
    else {
    double n=.58*qlcg_base_noise_d(u*2.3,v*2.3,0,p->seed^0x74657272u)+
        .29*qlcg_base_noise_d(u*5.1,v*5.1,0,p->seed^0x6c616e64u)+
        .13*qlcg_base_noise_d(u*11.3,v*11.3,0,p->seed^0x73686170u);
    double back=exp(-((u+.42)*(u+.42)/.065+v*v/.38));
    double shoulders=exp(-((fabs(v)-.43-p->skew*u)*(fabs(v)-.43-p->skew*u)/.035+u*u/.65));
    shoulders*=1+p->skew*(v>=0?1:-1)*2;
    double outlet=smooth((u+.05)/.25)*(1-smooth((fabs(v)-.08)/.18));
    hill=p->rise*(back+shoulders*.68)*(1+n*.3)*edge*(1-outlet);
    if(p->kind==HEAD_CLIFF)hill=p->rise*smooth((back+shoulders*.5-.15)/.6)*edge*(1-outlet);
    if(p->kind==HEAD_GRAVEL)hill=p->rise*(back*.55+shoulders*.8)*(1+n*.6)*edge*(1-outlet);
    if(p->kind==HEAD_SAND)hill=p->rise*(.5+.5*sin(v*6+u*2+p->phase+n))*edge*(1-outlet);
    if(p->kind==HEAD_MARSH)hill=p->rise*(.22*back+.3*shoulders+.4*smooth((n-.1)/.5))*edge*(1-outlet);
    if(p->kind==HEAD_ALPINE)hill=p->rise*(back*.8+shoulders)*(1+n*.22)*edge*(1-outlet);
    if(p->kind==HEAD_SNOW)hill=p->rise*(back*.8+shoulders*.55)*(1+n*.4)*edge*(1-outlet);
    if(p->springs)hill=p->rise*(back*.85+shoulders*.7+.28*(1-smooth(r)))*(1+n*.24)*edge*(1-outlet);
    hill=fmin(p->rise,hill);
    }
    double distance=hypot(p->x,p->z),t=distance>1?clamp(((q.x-p->x)*(-p->x)+(q.z-p->z)*(-p->z))/(distance*distance),0,1):0;
    double cx=p->x*(1-t),cz=p->z*(1-t),across=hypot(q.x-cx,q.z-cz);
    double corridor=1-smooth((across-fmax(10,p->pool_radius))/(p->radius*(p->design?.08:.16)));
    double level=mix(p->source_level,p->outlet_level,t);
    if(p->kind==HEAD_STEPS) {
        double step=t*3,part=step-floor(step);
        level=mix(p->source_level,p->outlet_level,(floor(step)+smooth((part-.78)/.22))/3);
    }
    double floor=level+.65;
    double height=base+hill;
    if(p->springs)height=mix(height,fmax(height,p->source_level+.8+hill),edge);
    double lift=fmax(0,p->source_level+.65-p->source_ground)*(1-smooth(r/.4));
    if(!p->design)height+=lift;
    return mix(height,floor,corridor*edge);
}

float qlcg_headwater_activity(const HeadwaterPlan *p,float temperature) {
    if(!p||p->kind<HEAD_SNOW)return 1;
    return (float)(smooth((temperature-ART.melt_start)/(ART.melt_peak-ART.melt_start))*
        (1-smooth((temperature-ART.melt_fade)/(ART.melt_end-ART.melt_fade))));
}
void qlcg_headwater_color(const HeadwaterPlan *p,double x,double z,float temperature,uint8_t color[3]) {
    if(!p)return;
    if(p->design>=2) { qlcg_headwater_region_color(p,x,z,temperature,color);return; }
    double dx=x-p->x,dz=z-p->z,r=hypot(dx,dz)/p->radius;
    if(r>=1)return;
    double u=(dx*p->dx+dz*p->dz)/p->radius,v=(-dx*p->dz+dz*p->dx)/p->radius;
    double noise=qlcg_base_noise_d(u*9,v*9,0,p->seed^0x736e6f77u);
    double wet=(1-smooth(r/.55))*p->moisture*.35;
    double snow=p->kind>=HEAD_SNOW?(1-smooth((temperature-ART.snow_start)/(ART.snow_end-ART.snow_start)))*
        smooth((noise+.3-u*.8)/.8)*(1-smooth((r-.6)/.4))*(.65+.35*p->moisture):0;
    static const double white[3]={226,235,237},moss[3]={81,104,74},stone[3]={116,123,125};
    double rock=(p->kind==HEAD_CLIFF||p->kind>=HEAD_SNOW)?(1-smooth((r-.65)/.35))*.9:0;
    for(int k=0;k<3;k++)color[k]=(uint8_t)clamp(mix(mix(mix(color[k],stone[k],rock),moss[k],wet),white[k],snow),0,255);
}
int qlcg_headwater_excludes(const MacroWorld *w,double wx,double wz,double radius) {
    int cell=qlcg_world_cell_at_wxz(w,(float)wx,(float)wz);
    const HeadwaterPlan *p=qlcg_headwater_plan(w,cell);if(!p)return 0;
    SurfacePoint q;if(!local_point(w,p,wx,wz,hypot(p->x,p->z)+radius+8,&q))return 0;
    double length=p->x*p->x+p->z*p->z;
    double t=length>1?clamp(((q.x-p->x)*(-p->x)+(q.z-p->z)*(-p->z))/length,0,1):0;
    return hypot(q.x-p->x*(1-t),q.z-p->z*(1-t))<radius+8;
}
