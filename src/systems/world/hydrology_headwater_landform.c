#include "hydrology_headwater.h"
#include "hydrology_archive.h"
#include "hydrology_river.h"
#include "surface_detail.h"
#include "surface_internal.h"
#include "gen/gen_noise.h"
#include "gen/gen_height.h"
#include "gen/hydrology_balance.h"
#include "../../core/math_util.h"
#include <math.h>
#include <string.h>

static const char *VARIANTS[HEAD_REGION_COUNT][HEAD_VARIANT_COUNT]={
    {"双脊夹谷","单崖偏谷","阶地峡谷","弧脊围谷"},
    {"低丘树状网","平行脊间网","宽谷漫丘网","串盆连谷网"},
    {"单脊雪沟","宽底雪谷","开口冰斗","刃脊双谷"},
    {"双谷 Y 汇","主峡侧汇","宽谷扇汇","围谷向心汇"},
    {"平畴浅洼","缓谷长洼","台原湿洼","山间悬盆"},
    {"狭长谷盆","偏心宽盆","开口围盆","台地浅盆"},
    {"山麓坡脚泉","单侧崖壁泉","脊间裂隙泉","盆缘群泉"}
};
static const int KINDS[7]={HEAD_CLIFF,HEAD_HOLLOW,HEAD_SNOW,HEAD_GRAVEL,HEAD_MARSH,HEAD_HOLLOW,HEAD_CLIFF};
static const int COUNTS[7]={5,10,6,6,8,5,5};
static const double HEIGHTS[7]={.065,.012,.085,.040,.006,.032,.048};
static const double GRADES[7]={.016,.003,.012,.006,.0008,.004,.010};
static const double WIDTHS[7]={48,140,100,120,220,100,48};

const char *qlcg_headwater_variant_name(int region,int variant) {
    return region>=0&&region<7&&variant>=0&&variant<4?VARIANTS[region][variant]:"旧版河源";
}
static int largest(const double *v,int count) {
    int best=0;for(int k=1;k<count;k++)if(v[k]>v[best])best=k;return best;
}
static double field(const float *v,int cell) { return v?qlcg_clampd(v[cell],0,1):0; }
static double relief_budget(const MacroWorld *w,int cell) {
    double low=w->height[cell],high=low;
    for(int k=0;k<8;k++) {
        double height=w->height[w->neighbors[cell*8+k]];
        low=fmin(low,height);high=fmax(high,height);
    }
    double scale=fmax(.01,w->height_scale),uplift=field(w->uplift,cell);
    double activity=pow(qlcg_clampd(w->opt.tectonic_activity/9.0,0,1),1.504);
    double cap=qlcg_terrain_height_cap(&w->opt);
    double local=(high-low)*.25+(18+uplift*150)*scale;
    double global=cap*(.012+.20*activity*uplift);
    return fmax(0,fmin(fmin(local,global),cap-fmax(0,high)));
}

int qlcg_headwater_landform_build(const MacroWorld *w,int cell,HeadwaterPlan *p) {
    const MacroCellPlan *m=qlcg_macro_cell_plan(w,cell);
    if(!m||!m->head||!m->source.valid)return 0;
    SourcePlan source=m->source;
    p->cell=cell;p->seed=w->opt.seed^((uint32_t)(cell+1)*0x68656164u);p->design=3;
    p->dx=source.dx;p->dz=source.dz;p->span=w->circumference/w->columns;
    p->span_x=p->span*(source.frame.flat?1:fmax(.01,cos(source.frame.lat)));
    if(p->span_x<320)return 0;
    SurfacePoint center=qlcg_frame_coordinates(&source.frame,0,0);
    p->center_x=center.x;p->center_z=center.z;
    double aridity=w->rainfall[cell]/fmax(150,w->evaporation[cell]);
    int substrate=qlcg_classify_chunk(false,false,false,false,false,w->height[cell],w->temperature[cell],aridity,0,source.slope,0,0);
    p->material=qlcg_chunk_material(substrate);
    double balance=w->rainfall[cell]/fmax(150,w->evaporation[cell]);
    p->moisture=qlcg_clampd(.65*w->moisture[cell]+.35*balance,0,1);
    double season=w->season_amp?w->season_amp[cell]:0;
    p->cold=w->temperature[cell]-season;p->warm=w->temperature[cell]+season;
    double wet=qlcg_smooth01d((p->moisture-.25)/.6),slope=qlcg_smooth01d(source.slope/.008);
    double uplift=field(w->uplift,cell),rift=field(w->rift,cell),shear=field(w->shear,cell);
    double snow=p->cold< -1&&p->warm>1?qlcg_smooth01d((-p->cold-1)/14)*qlcg_smooth01d(w->rainfall[cell]/250):0;
    double seasonal=qlcg_clampd(season/25,0,1);
    double score[7]={.25+2*slope*wet+uplift*.8+shear*.3,.25+1.45*wet*(1-slope)+.12*seasonal,3.5*snow,
        .75+.75*wet+.25*slope,wet>.65&&source.slope<.0012&&p->warm<22&&balance>1.2?2.4*wet*(1-slope):0,
        wet>.35?.25+wet*(.4+rift*2.8)+.7*(m->lake_in>=0):0,.25+1.8*(1-wet)+shear*.25};
    p->region=largest(score,7);
    if(balance<.25&&!snow) {
        double own=qlcg_runoff_m3s(w->rainfall[cell],w->evaporation[cell],w->areas[cell]);
        p->region=w->flow&&w->flow[cell]>fmax(.1,own*2)?HEAD_REGION_NETWORK:HEAD_REGION_SPRING;
    }
    double up=qlcg_smooth01d(uplift/.12),ri=qlcg_smooth01d(rift/.25),sh=qlcg_smooth01d(shear/.10);
    double forms[4]={.55*(1-ri)*(1-sh),sh*1.4+slope*.65+(p->material==3?.08:0),ri*1.3+up*.15+(p->material==2?.05:0),up*.85+wet*.25};
    if(p->region==HEAD_REGION_MARSH) {
        forms[0]=.6*(1-up);forms[1]=slope*1.1+seasonal*.35;forms[2]=up;forms[3]=ri+sh*.5;
    }
    if(p->region==HEAD_REGION_SNOW) {
        forms[0]=.65;forms[1]=wet;forms[2]=ri*1.6;forms[3]=up+sh;
    }
    p->variant=largest(forms,4);p->kind=KINDS[p->region];p->springs=COUNTS[p->region];
    double scale=qlcg_clampd(w->height_scale,.35,3),size=pow(p->span/4096,.65);
    p->ridge_height=fmin(6500,HEIGHTS[p->region]*4096*size*(.65+.9*uplift)*sqrt(scale));
    p->edge_width=fmin(p->span*.20,fmax(256,p->ridge_height*.45));
    p->lift=fmin(p->edge_width*.25,p->ridge_height*.24);
    p->grade=GRADES[p->region]*(.75+.5*uplift);
    if(qlcg_river_archive_terrain(w)>=13) {
        static const double ridge_bound[7]={1.4,2,2.4,2,1.4,1.4,1.4};
        double budget=relief_budget(w,cell);
        p->ridge_height=fmin(6500,budget*.65*HEIGHTS[p->region]/.085/ridge_bound[p->region]);
        p->edge_width=fmin(p->span*.20,fmax(256,p->ridge_height*.45));
        p->lift=fmin(p->edge_width*.25,budget*.08);
        p->grade=fmin(p->grade*fmax(.01,w->height_scale),budget*.20/fmax(64,p->span*.45));
    }
    p->valley_width=fmin(p->span_x*.12,WIDTHS[p->region]*pow(p->span/4096,.35));
    p->radius=fmin(32768,hypot(p->span_x,p->span)*.5);p->rise=p->ridge_height;
    p->skew=((p->seed>>12)&255)/255.0*.16-.08;p->phase=(p->seed&65535)*6.283185307179586/65536;
    p->outlet_ground=w->height[cell];
    p->run=fmin(1536,p->span*.12);p->outlet_level=p->outlet_ground-.8;
    p->pool_radius=p->region==HEAD_REGION_LAKE?fmin(240,p->span*.055):p->region==HEAD_REGION_SPRING?16:0;
    qlcg_headwater_layout(w,p);
    p->x=p->shape.branches[0].ax;p->z=p->shape.branches[0].az;
    p->source_ground=p->shape.branches[0].start;p->source_level=p->source_ground-.6;
    return 1;
}

static SurfacePoint local_position(const MacroWorld *w,const HeadwaterPlan *p,double u,double v) {
    double x=u*p->dx-v*p->dz,z=u*p->dz+v*p->dx;
    SurfaceFrame frame=qlcg_surface_frame(w,p->cell);
    return qlcg_frame_local(&frame,p->center_x+qlcg_clampd(x,-.38,.38)*p->span,p->center_z+qlcg_clampd(z,-.38,.38)*p->span);
}
void qlcg_headwater_layout(const MacroWorld *w,HeadwaterPlan *p) {
    HeadwaterDesign *s=&p->shape;s->family=p->variant;s->branch_count=p->springs;s->geometry_ready=0;s->geometry_hash=0;
    for(int k=0;k<s->branch_count;k++) {
        HeadwaterValley *b=s->branches+k;memset(b,0,sizeof(*b));b->parent=-1;
        double u=-.36,v=p->skew;
        if(!k&&p->region==HEAD_REGION_LAKE) { u=p->variant==3?-.14:-.18;v+=p->variant==1?.10:0; }
        if(!k&&p->region==HEAD_REGION_SNOW&&p->variant==3)v-=.18;
        if(!k&&p->region==HEAD_REGION_CONFLUENCE&&p->variant==0)v-=.20;
        if(!k&&p->region==HEAD_REGION_SPRING)u=p->variant==0?-.20:p->variant==3?-.24:u;
        if(k) {
            int side=k&1?1:-1;double rank=(k-1)/2.0/fmax(1,(p->springs-2)/2.0);
            u=-.40+.30*rank;v=side*(.22+.12*rank);
            if(p->variant==1) { u=-.39+.05*((k-1)%3);v=side*(.16+.07*((k-1)/2)); }
            if(p->variant==2) { u=-.36+.28*rank;v=side*(.30-.08*rank); }
            if(p->variant==3) { double angle=1.79+(k-1)*2.70/fmax(1,p->springs-2);u=.38*cos(angle);v=.38*sin(angle); }
            b->parent=0;b->fraction=.20+.55*rank;
            if(p->region==HEAD_REGION_CONFLUENCE&&p->variant==1)v=fmin(.38,.16+.055*(k-1));
            if(p->region==HEAD_REGION_CONFLUENCE&&p->variant==2)b->fraction=.68;
            if((p->region==HEAD_REGION_NETWORK||p->region==HEAD_REGION_MARSH)&&k>=5) {
                b->parent=1+(k-5)%3;b->fraction=.45;
            }
            if(((p->region==HEAD_REGION_SNOW&&p->variant==3)||(p->region==HEAD_REGION_CONFLUENCE&&p->variant==0))&&k>=3) {
                b->parent=1+(k%2);b->fraction=.35;
            }
            if(qlcg_river_archive_terrain(w)>=11&&p->region==HEAD_REGION_CONFLUENCE&&!p->variant&&k<3) {
                u=k==1?-.38:-.26;v=k==1?.30:-.34;
            }
            if(p->region==HEAD_REGION_LAKE) {
                double angle=1.65+(k-1)*2.98/fmax(1,p->springs-2);
                u=(p->variant==3?-.14:-.18)+.26*cos(angle);
                v=p->skew+(p->variant==1?.10:0)+.26*sin(angle);b->fraction=0;
            }
            if(p->region==HEAD_REGION_SNOW&&p->variant==3&&k<3) {
                u=-.38;v=k==1?.18:-.34;b->fraction=k==1?.72:.25;
            }
            if(p->region==HEAD_REGION_SNOW&&p->variant==3&&k>=3) {
                u=-.36+.07*(k-3);v=side*.36;b->parent=k&1?1:2;b->fraction=.30+.175*(k-3);
            }
            if(p->region==HEAD_REGION_SPRING&&p->variant==0)u=-.20+.12*rank;
            if(p->region==HEAD_REGION_SPRING&&p->variant==1) {u=-.38+.07*(k-1);v=.06;}
            if(p->region==HEAD_REGION_SPRING&&p->variant==3) {u*=.80;v*=.80;}
        }
        SurfacePoint a=local_position(w,p,u,v);b->ax=a.x;b->az=a.z;
        if(k) {
            const HeadwaterValley *parent=s->branches+b->parent;
            b->bx=qlcg_mixd(parent->ax,parent->bx,b->fraction);
            b->bz=qlcg_mixd(parent->az,parent->bz,b->fraction);
            b->end=qlcg_mixd(parent->start,parent->end,b->fraction);
        } else b->end=p->outlet_level+.8;
        double length=hypot(b->ax-b->bx,b->az-b->bz);
        b->start=b->end+fmax(2,p->grade*length);
        double bend=fmin(length*.085,420)*1.5625+fmin(length*.07,160);
        b->width=(p->valley_width+bend)/.65;
    }
}

static double envelope(const MacroWorld *w,const HeadwaterPlan *p,double wx,double wz) {
    double x=wx-p->center_x;
    if(!qlcg_world_flat(w))x-=floor(x/w->circumference+.5)*w->circumference;
    double z=wz-p->center_z;
    double ex=(p->span*.5-fabs(x))*(qlcg_world_flat(w)?1:p->span_x/p->span);
    double ez=p->span*.5-fabs(z);
    double width=fmin(p->edge_width,fmin(p->span_x,p->span)*.22);
    return qlcg_smooth01d(ex/fmax(1,width))*qlcg_smooth01d(ez/fmax(1,width));
}

static double mass_weight(const MacroWorld *w,int cell,double wx,double wz,double *texture,int coarse) {
    if(!w||cell<0||cell>=w->cell_count||qlcg_river_archive_terrain(w)<8||w->height[cell]<4)return 0;
    const MacroCellPlan *m=qlcg_macro_cell_plan(w,cell);
    if(!m||!m->head||!m->source.valid)return 0;
    double span=w->circumference/w->columns;
    double east=span*(m->source.frame.flat?1:fmax(.01,cos(m->source.frame.lat)));
    if(east<320)return 0;
    SurfacePoint q=qlcg_frame_local(&m->source.frame,wx,wz);
    double x=q.x/east,z=q.z/span,dx=m->source.dx,dz=m->source.dz;
    double u=x*dx+z*dz,v=-x*dz+z*dx;
    uint32_t seed=w->opt.seed^((uint32_t)(cell+1)*0x68656164u);
    double noise=qlcg_base_noise_d(u*3.1+9,v*3.1+17,0,seed^0x72696467u);
    double fold=0,grain=0;
    if(qlcg_river_archive_terrain(w)>=10) {
        fold=qlcg_base_noise_d(u*11.7+3,v*11.7+23,0,seed^0x666f6c64u);
        double scale=qlcg_clampd(span*.02,48,192);
        if(!coarse)grain=qlcg_base_noise_d(q.x/scale+19,q.z/scale+5,0,seed^0x67756c6cu);
        if(qlcg_river_archive_terrain(w)>=11) {
            u+=.050*noise+.015*fold;v+=.025*fold+fmin(.026,scale*.20/span)*grain;
        } else { u+=.065*noise+.038*fold;v+=.055*fold+.026*grain; }
    }
    if(texture) { texture[0]=noise;texture[1]=fold;texture[2]=grain; }
    double a=hypot((u+.07+noise*.025)/.43,(v-noise*.035)/.39);
    double b=hypot((u+.23-noise*.018)/.25,(v-.13)/.24);
    double c=hypot((u-.13)/.27,(v+.15+noise*.02)/.23);
    double wa=1-qlcg_smooth01d((a-.55)/.45),wb=1-qlcg_smooth01d((b-.5)/.5),wc=1-qlcg_smooth01d((c-.5)/.5);
    SurfacePoint center=qlcg_frame_coordinates(&m->source.frame,0,0);
    double gx=wx-center.x;if(!m->source.frame.flat)gx-=floor(gx/w->circumference+.5)*w->circumference;
    double ex=(span*.5-fabs(gx))*east/span,ez=span*.5-fabs(wz-center.z);
    double width=fmax(32,fmin(128,east*.12));
    double guard=qlcg_smooth01d((ex-16)/width)*qlcg_smooth01d((ez-16)/width);
    double modulation=qlcg_river_archive_terrain(w)>=11?qlcg_clampd(.90+.25*fold+.12*grain,.5,1):
        qlcg_river_archive_terrain(w)>=10?qlcg_clampd(.82+.38*fold+.28*grain,.25,1):1;
    return (1-(1-wa)*(1-wb)*(1-wc))*guard*modulation;
}
double qlcg_headwater_mass_weight(const MacroWorld *w,int cell,double wx,double wz) {
    return mass_weight(w,cell,wx,wz,NULL,0);
}
MacroWaterfall qlcg_headwater_fall_plan(const HeadwaterPlan *p,int branch,double length) {
    static const double relief[7]={.14,.035,.15,.10,0,.10,.12};
    MacroWaterfall fall={0};
    if(p->region==HEAD_REGION_MARSH||length<384||p->ridge_height<30)return fall;
    uint32_t seed=qlcg_hash_u32(p->seed^((uint32_t)(branch+1)*0x66616c6cu));
    if(branch&&(seed&3)>1)return fall;
    double drop=fmin(64,p->ridge_height*relief[p->region])*(.75+.25*((seed>>8)&255)/255.0);
    drop=fmin(drop,length*.06);if(drop<5)return fall;
    double begin=length*((p->region==HEAD_REGION_LAKE?.46:.32)+.08*((seed>>16)&255)/255.0);
    double run=qlcg_clampd(length*.12,64,240);
    static const int styles[4]={0,3,7,4};
    return (MacroWaterfall){2,styles[p->variant],begin,begin+run,drop,0,fmax(8,p->valley_width*.5)};
}

static double bell(double x,double width) { return exp(-pow(x/width,2)); }
static double ridge_form(const HeadwaterPlan *p,double u,double v,double noise) {
    double a=u+.18,b=v-p->skew,h=0;
    double n=isfinite(noise)?noise:qlcg_base_noise_d(u*3.1+9,v*3.1+17,0,p->seed^0x72696467u);
    switch(p->variant) {
    case 0:h=bell(fabs(b)-.24-n*.025,.09)*(.55+.45*qlcg_smooth01d((.38-u)/.65));break;
    case 1:h=bell(b-.23-n*.03,.075)*(.65+.35*cos(a*5));break;
    case 2:h=qlcg_smooth01d((fabs(b)-.16)/.18)*(.55+.30*qlcg_smooth01d((-a+.12)/.3));break;
    default:h=bell(hypot(a,b)-.30-n*.025,.08)*(1-qlcg_smooth01d((u-.05)/.28));break;
    }
    switch(p->region) {
    case HEAD_REGION_GORGE:
        if(p->variant==2)h=qlcg_detail_terrace_height(h,.24,.65);
        break;
    case HEAD_REGION_NETWORK:
        if(p->variant==0)h=.55*bell(a+.08,.17)*bell(b-.27,.13)+.65*bell(a-.13,.15)*bell(b+.23,.15)+.22*(1-fabs(n));
        if(p->variant==1)h=(.48*bell(b-.27,.055)+.32*bell(b+.08,.05)+.52*bell(b+.31,.06))*(.7+.3*cos(a*4));
        if(p->variant==2)h=.28+.35*bell(a+.1,.28)*qlcg_smooth01d((fabs(b)-.14)/.24)+.15*n;
        if(p->variant==3)h=.55*bell(hypot((a+.1)*1.2,b+.11)-.17,.07)+.45*bell(hypot((a-.17)*1.3,b-.11)-.14,.06);
        break;
    case HEAD_REGION_SNOW:
        if(p->variant==0)h=bell(u+.31+b*.18+n*.02,.065)*(1-qlcg_smooth01d((fabs(b)-.20)/.24));
        if(p->variant==1)h=bell(fabs(b)-.31-n*.02,.11)*(.75+.25*bell(a,.35));
        if(p->variant==2)h=bell(hypot((u+.24)*.8,b)-.28-n*.02,.07)*(1-qlcg_smooth01d((u-.02)/.25));
        if(p->variant==3)h=(bell(b+n*.012,.045)+.70*bell(fabs(b)-.34,.055))*(1-qlcg_smooth01d((u+.02)/.32));
        break;
    case HEAD_REGION_CONFLUENCE:
        if(p->variant==0)h=.8*bell(b-a*.55-.13,.075)+.65*bell(b+a*.65+.21,.08);
        if(p->variant==1)h=.85*bell(b+.20,.07)+.5*bell(b-.32,.13);
        if(p->variant==2)h=(.4+.3*n)*qlcg_smooth01d((fabs(b)-.15)/.20)+.55*bell(u+.38,.08);
        break;
    case HEAD_REGION_MARSH:
        if(p->variant==0)h=.18+.12*n+.15*bell(a+.06,.20)*bell(fabs(b)-.28,.13);
        if(p->variant==1)h=.55*bell(fabs(b)-.27,.17)*(1-qlcg_smooth01d((u+.04)/.35));
        if(p->variant==2)h=.65*(1-qlcg_smooth01d((hypot(a*.8,b)-.22)/.15))+.08*n;
        if(p->variant==3)h=.85*bell(hypot((a+.06)*.85,b)-.29-n*.018,.075)*(1-qlcg_smooth01d((u-.02)/.24));
        break;
    case HEAD_REGION_LAKE:
        if(p->variant==0)h=.75*bell(fabs(b)-.20-n*.02,.085)*bell(a,.36);
        if(p->variant==1)h=.85*bell(hypot((a+.02)*.8,(b-.10)*1.15)-.30-n*.02,.09)*(1-qlcg_smooth01d((u-.05)/.3));
        if(p->variant==2)h=bell(hypot((u+.24)*.8,b)-.28-n*.02,.07)*(1-qlcg_smooth01d((u-.02)/.25));
        if(p->variant==3)h=.65*(1-qlcg_smooth01d((hypot(u+.12,b)-.26)/.10));
        break;
    case HEAD_REGION_SPRING:
        if(p->variant==0)h=bell(u+.35+b*.12,.08)*(1-qlcg_smooth01d((fabs(b)-.24)/.2));
        if(p->variant==1)h=qlcg_smooth01d((b-.10-n*.02)/.10)*(1-qlcg_smooth01d((fabs(a)-.20)/.18));
        if(p->variant==2)h=.85*bell(fabs(b)-.14-n*.012,.045)*(1-qlcg_smooth01d((u+.02)/.3));
        if(p->variant==3)h=.85*bell(hypot(a,b)-.34-n*.02,.065)*(1-qlcg_smooth01d((u-.02)/.3));
        break;
    }
    return fmax(0,h)*(1+.10*n);
}

static double uplift_at(const MacroWorld *w,const HeadwaterPlan *p,double wx,double wz,double u,double v,double *texture,double *mass,int detail) {
    int rough=qlcg_river_archive_terrain(w)>=10;
    int continuous=qlcg_river_archive_terrain(w)>=11;
    *mass=mass_weight(w,p->cell,wx,wz,rough?texture:NULL,!detail&&qlcg_river_archive_terrain(w)>=13);
    double fine_warp=fmin(.020,qlcg_clampd(p->span*.02,48,192)*.20/p->span);
    double ridge=ridge_form(p,u+(rough?(continuous?.018:.035)*texture[1]:0),
        v+(rough?(continuous?.035:.045)*texture[0]+(continuous?fine_warp:.020)*texture[2]:0),rough?texture[0]:NAN);
    if(rough)ridge*=continuous?qlcg_clampd(1+.45*texture[1],.35,1.25):fmax(.12,1+.65*texture[1]+.55*texture[2]);
    double uplift=p->lift+fmax(0,-u)*p->span*p->grade+p->ridge_height*ridge;
    return *mass*uplift;
}
double qlcg_headwater_backdrop(const MacroWorld *w,const HeadwaterPlan *p,double wx,double wz,double base) {
    SurfaceFrame frame=qlcg_surface_frame(w,p->cell);SurfacePoint q=qlcg_frame_local(&frame,wx,wz);
    double x=q.x/fmax(1,p->span_x),z=q.z/p->span,texture[3]={0},mass;
    return base+uplift_at(w,p,wx,wz,x*p->dx+z*p->dz,-x*p->dz+z*p->dx,texture,&mass,1)*envelope(w,p,wx,wz);
}
double qlcg_headwater_landform_height(const MacroWorld *w,const HeadwaterPlan *p,double wx,double wz,double base,int detail) {
    double edge=envelope(w,p,wx,wz);if(edge<=0)return base;
    const HeadwaterGeometryView *geometry=detail?qlcg_headwater_geometry(w,p->cell):NULL;
    SurfaceFrame frame=qlcg_surface_frame(w,p->cell);SurfacePoint q=qlcg_frame_local(&frame,wx,wz);
    double x=q.x/fmax(1,p->span_x),z=q.z/p->span;
    double u=x*p->dx+z*p->dz,v=-x*p->dz+z*p->dx;
    int rough=qlcg_river_archive_terrain(w)>=10,continuous=qlcg_river_archive_terrain(w)>=11;
    double texture[3]={0},mass=0;
    double target=base+uplift_at(w,p,wx,wz,u,v,texture,&mass,detail);
    if(continuous&&geometry) {
        double support=0;
        for(int k=0;k<geometry->count;k++) {
            if(!geometry->ground[k]||geometry->backdrop[k]<=0)continue;
            const RiverVectorPoint *a=geometry->points[k],*b=a+1;
            double raise=fmax(0,geometry->ground[k][0]-geometry->backdrop[k]);if(raise<=0)continue;
            double dx=b->x-a->x,dz=b->z-a->z,len=fmax(1e-6,hypot(dx,dz));
            double along=((q.x-a->x)*dx+(q.z-a->z)*dz)/len,across=((q.z-a->z)*dx-(q.x-a->x)*dz)/len;
            double length=qlcg_clampd(raise*4,256,p->span*.12),width=qlcg_clampd(raise,96,240);
            double r2=pow(along/length+.25,2)+pow(across/width,2);
            if(r2>=4)continue;
            support=fmax(support,raise*exp(.0625-r2)*(1-qlcg_smooth01d(sqrt(r2)-1)));
        }
        target+=support;
    }
    double raw=target,weight=0,sum=0,gate=0,near_sum=0,near_weight=0,near_cover=0;
    int supported=qlcg_river_archive_terrain(w)>=9;
    for(int k=0;k<p->shape.branch_count;k++) {
        const HeadwaterValley *b=p->shape.branches+k;
        double dx=b->bx-b->ax,dz=b->bz-b->az,length2=dx*dx+dz*dz;
        double t=qlcg_clampd(((q.x-b->ax)*dx+(q.z-b->az)*dz)/fmax(1,length2),0,1);
        double distance=hypot(q.x-b->ax-dx*t,q.z-b->az-dz*t);
        double width=b->width;
        if(geometry&&k<geometry->count) {
            double along=0,field_floor=0;
            int near=continuous?(qlcg_river_index_height(geometry->indices[k],geometry->points[k],geometry->ground[k],
                q.x,q.z,qlcg_clampd(p->valley_width*.25,8,32),480,&field_floor,&distance)?0:-1):
                qlcg_river_index_nearest(geometry->indices[k],geometry->points[k],q.x,q.z,&distance,&along);
            if(near>=0&&supported) {
                const float *ground=geometry->ground[k];
                double floor=continuous?field_floor:ground?qlcg_mixd(ground[near],ground[near+1],along):qlcg_mixd(b->start,b->end,t);
                double core=fmin(192,p->valley_width);
                int gentle=qlcg_river_archive_terrain(w)>=13;
                if(rough)core*=gentle?qlcg_clampd(.95+.15*texture[1]+.10*texture[2],.70,1.20):
                    qlcg_clampd(.90+.35*texture[1]+.65*texture[2],.35,1.5);
                double reach=qlcg_clampd(core+fmax(gentle?128:64,fabs(raw-floor)*(gentle?6:2.5)),core+(gentle?128:64),480);
                if(rough)reach=qlcg_clampd(core+(reach-core)*(gentle?1+.25*texture[1]:1+.85*texture[2]),core+(gentle?128:40),480);
                double head_gate=1;
                if(continuous&&!(p->region==HEAD_REGION_LAKE&&!k)) {
                    const RiverVectorPoint *a=geometry->points[k],*next=a+1;
                    double vx=next->x-a->x,vz=next->z-a->z,len=fmax(1e-6,hypot(vx,vz));
                    double axial=((q.x-a->x)*vx+(q.z-a->z)*vz)/len;
                    double cap=qlcg_clampd(p->valley_width*.25,16,48),lead=qlcg_clampd(p->valley_width*2,128,384);
                    double grow=qlcg_smooth01d((axial+cap)/(lead+cap));
                    core=qlcg_mixd(cap,core,grow);reach=qlcg_mixd(cap*2,reach,grow);
                    head_gate=qlcg_smooth01d((axial+cap)/cap);
                }
                double warped=distance;
                if(rough)warped+=(reach-core)*(gentle?.15*texture[1]+.10*texture[2]:.25*texture[1]+.85*texture[2])*
                    qlcg_smooth01d((distance-core*.65)/(core+48));
                double cover=1-qlcg_smooth01d((warped-core)/(reach-core));
                cover*=head_gate;
                if(rough)cover*=1-qlcg_smooth01d((distance-416)/64);
                if(cover>0) {
                    double strength=1/pow(1+distance/fmax(16,core*.35),4);
                    if(continuous)strength*=cover;
                    near_sum+=floor*cover*strength;near_cover+=cover*strength;near_weight+=strength;
                }
                distance=hypot(q.x-b->ax-dx*t,q.z-b->az-dz*t);
            } else if(near>=0) {
                const RiverVectorPoint *points=geometry->points[k];
                double total=points[geometry->sizes[k]-1].arc;
                double precise=1-qlcg_smooth01d((distance-256)/224);
                double arc=qlcg_mixd(points[near].arc,points[near+1].arc,along)/fmax(1,total);
                double broad=hypot(q.x-b->ax-dx*t,q.z-b->az-dz*t);
                t=qlcg_mixd(t,arc,precise);distance=qlcg_mixd(broad,distance,precise);
                double narrow=(p->valley_width+.20*fmax(0,b->width*.65-p->valley_width))/.65;
                width=qlcg_mixd(width,narrow,precise);
            } else distance=hypot(q.x-b->ax-dx*t,q.z-b->az-dz*t);
        }
        double core=distance/fmax(1,width),blend=1-qlcg_smooth01d((core-.65)/.95);
        if(continuous&&!(p->region==HEAD_REGION_LAKE&&!k)) {
            double axial=((q.x-b->ax)*dx+(q.z-b->az)*dz)/sqrt(fmax(1,length2));
            double cap=qlcg_clampd(p->valley_width*.25,16,48),lead=qlcg_clampd(p->valley_width*2,128,384);
            blend*=qlcg_smooth01d((axial+cap)/(lead+cap));
        }
        if(blend<=0)continue;
        double floor=b->end+(b->start-b->end)*(supported?1-t:1-t*t);
        double shoulder=p->ridge_height*.16*pow(qlcg_smooth01d((core-.65)/.95),p->region==HEAD_REGION_SNOW?2:1);
        double influence=blend*blend/pow(1+distance/fmax(12,width*.08),6);
        sum+=(floor+shoulder)*influence;weight+=influence;gate=fmax(gate,blend);
    }
    if(weight>0)target=qlcg_mixd(target,sum/weight,gate);
    if(supported) {
        target=fmin(raw,target);
        if(near_weight>0)target+=(near_sum-target*near_cover)/near_weight;
    }
    double shelter=near_weight>0?1-qlcg_clampd(near_cover/near_weight,0,1):1;
    if(rough)target+=mass*fmin(256,p->ridge_height*(continuous?.25:.40))*
        (continuous?.65*texture[1]+.35*texture[2]:.35*texture[1]+.65*texture[2])*shelter;
    if(detail) {
        double noise=qlcg_base_noise_d(q.x/180,q.z/180,0,p->seed^0x67726169u);
        target+=noise*(rough?mass*fmin(continuous?4:12,p->ridge_height*(continuous?.015:.04))*shelter:fmin(3,p->ridge_height*.015)*(1-gate));
    }
    double exit=qlcg_smooth01d(hypot(q.x,q.z)/fmax(64,p->span*.18));
    double minimum=base+mass*p->lift*exit;
    double addition=fmax(0,target-minimum);
    target=minimum+addition*qlcg_smooth01d(addition/8);
    if(detail&&u>-.02) {
        RiverDetailCandidates rivers;RiverValleySample valley;
        if(qlcg_river_detail_candidates(w,p->cell,&rivers)) {
            double keep;
            if(qlcg_river_archive_terrain(w)>=12) {
                double relief=p->ridge_height*1.5+p->lift+p->span*p->grade*.5;
                double fade=fmin(fmin(p->span,p->span_x)*.25,fmax(128,2*relief));
                keep=1-qlcg_river_relief_gate(w,&rivers,wx,wz,fade);
            } else keep=1-qlcg_river_detail_gate(w,&rivers,wx,wz,&valley);
            edge*=1-keep*qlcg_smooth01d(u/.04);
        }
    }
    return qlcg_mixd(base,target,edge);
}

double qlcg_headwater_map_height(const MacroWorld *w,double wx,double wz,double base) {
    if(qlcg_river_archive_terrain(w)<8||base<=0)return base;
    int cell=qlcg_world_cell_at_wxz(w,(float)wx,(float)wz);
    if(cell<0||cell>=w->cell_count||w->height[cell]<4)return base;
    const HeadwaterPlan *p=qlcg_headwater_plan(w,cell);if(!p||p->design!=3)return base;
    HeadwaterPlan coarse=*p;coarse.outlet_ground=w->height[cell];coarse.outlet_level=coarse.outlet_ground-.8;
    qlcg_headwater_layout(w,&coarse);coarse.source_ground=coarse.shape.branches[0].start;
    return qlcg_headwater_landform_height(w,&coarse,wx,wz,base,0);
}
