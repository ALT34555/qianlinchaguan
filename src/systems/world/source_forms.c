/* 二十四水源模板与统一湖盆剖面 */
#include "source_rivers.h"
#include "surface_detail.h"
#include <math.h>

static double clampd(double v,double a,double b) { return fmax(a,fmin(b,v)); }
static double smoothd(double x) { x=clampd(x,0,1); return x*x*(3-2*x); }
static double mixd(double a,double b,double t) { return a+(b-a)*t; }

static const SourceForm FORMS[SOURCE_FORM_COUNT]={
    {"seep",1.25,.75,0,.10,0,.40,.18,0,1,2,0,0},
    {"artesian",.85,.90,-.10,0,0,.75,.35,0,1,1,0,0},
    {"fissure",1.70,.35,-.20,.22,0,.65,1.50,0,1,1,0,0},
    {"alcove",.90,1.15,-.18,.12,0,.85,2.80,0,1,1,0,0},
    {"spring_pool",1.10,1.05,.08,0,0,1.55,.25,0,1,1,0,0},
    {"twin",1.05,.65,-.12,.10,.45,.65,.40,0,2,1,0,0},
    {"sandbar",1.80,.85,.12,-.22,0,.45,.15,1,1,1,0,0},
    {"gravel_fan",1.50,1.20,-.20,.35,.24,.40,.65,.55,2,2,0,0},
    {"marsh",1.60,1.25,0,.18,.32,.28,.10,.25,1,2,0,0},
    {"tarn",1.30,1.05,-.15,-.10,0,2.20,.60,0,1,3,0,0},
    {"snowmelt",1.70,.50,-.22,.12,0,.30,.20,0,2,1,0,0},
    {"snow_gully",1.10,.55,-.25,-.15,.25,.45,1.10,0,3,1,0,0},
    {"hanging_fall",.90,1.05,-.22,.18,0,.95,3.20,0,1,1,0,0},
    {"cascade",1.65,.75,-.12,.16,.20,.75,1.90,.15,3,1,0,0},
    {"fan_fall",1.25,1.35,-.18,.30,.30,.65,2.40,0,2,1,0,0},
    {"lakeshore",1.55,.80,0,-.18,0,.60,.15,.20,1,2,0,0},
    {"basin",1.05,.95,.00,.00,0,1.10,.35,.05,1,2,1,1.00},
    {"dell",1.35,1.10,.06,-.05,0,.60,.18,.10,1,1,1,1.15},
    {"twin_basin",1.05,.70,-.08,.10,.42,.90,.30,.06,2,2,1,1.05},
    {"step_basin",1.10,1.00,-.10,.08,0,.85,.80,.12,3,2,1,1.10},
    {"alcove_basin",.80,1.10,-.16,.10,0,1.70,1.20,.02,1,2,1,.85},
    {"sand_basin",1.55,1.05,.10,-.18,0,.55,.16,.55,1,1,1,1.20},
    {"gravel_basin",1.25,1.15,-.14,.28,.20,.65,.45,.40,2,2,1,1.05},
    {"notch_basin",1.10,1.00,.04,.06,0,1.00,.40,.08,1,2,1,1.05}
};

const SourceForm *qlcg_source_form(int kind) {
    return FORMS+(kind>=0&&kind<SOURCE_FORM_COUNT?kind:0);
}

void qlcg_source_material_set(RiverSource *s,int material) {
    static const SourceMaterial finishes[5]={
        {.74,1.05,1.20,.08,.02},
        {1,1,.80,.035,0},
        {1.18,.48,.15,.065,0},
        {1.50,.55,.20,.075,.44},
        {1.13,.68,.35,.21,.04}
    };
    s->material=material>=1&&material<=5?material:2;
    s->finish=finishes[s->material-1];
}

void qlcg_source_pool_shape(RiverSource *s) {
    const SourceForm *form=qlcg_source_form(s->spring_form);
    s->pool=form->pool;
    s->pool_rx=s->pool==3?36:s->pool==2?22:12;
    s->pool_rz=s->pool_rx*form->rz/fmax(.7,form->rx)*s->finish.across;
    double depth=s->pool==3?2.2:s->pool==2?.70:fmax(.45,form->depth);
    s->pool_depth=depth*s->finish.depth;
    s->pool_yaw=atan2(s->direction_z,s->direction_x);
    s->pool_phase=(s->seed&65535)*.0001;
}

static int source_material(const MacroWorld *w,SourceRiverNetwork *n,const RiverSource *s) {
    SurfacePoint p=qlcg_frame_coordinates(&n->frame,s->x,s->z);
    int cx=(int)floor(p.x/CHUNK_SIZE),cz=(int)floor(p.z/CHUNK_SIZE);
    for(int i=0;i<n->material_chunks;i++)
        if(n->material_x[i]==cx&&n->material_z[i]==cz) return n->material_id[i];
    int material=qlcg_world_natural_chunk_material(w,cx,cz);
    if(n->material_chunks<SOURCE_RIVER_LIMIT) {
        int i=n->material_chunks++;
        n->material_x[i]=cx; n->material_z[i]=cz;
        n->material_id[i]=(uint8_t)material;
    }
    return material;
}

double qlcg_source_pool_chance(double moisture) {
    double t=clampd((moisture-.28)/.47,0,1);
    return .08+.78*t*t*(3-2*t);
}

double qlcg_source_pool_radius(const RiverSource *s) {
    if(!s) return 0;
    const SourceForm *f=qlcg_source_form(s->spring_form);
    if(!f->pooled||s->spring_radius<=0) return 0;
    double r=s->spring_radius*(f->pool_scale>0?f->pool_scale:1.0)*1.7+3.0;
    return clampd(r,6.0,18.0);
}

void qlcg_source_form_choose(const MacroWorld *w,SourceRiverNetwork *n,RiverSource *s,int id) {
    uint32_t seed=w->opt.seed^((uint32_t)(n->cell+1)*0x9e3779b1u)^((uint32_t)(id+1)*0x85ebca6bu);
    seed=(seed^(seed>>16))*0x7feb352du; seed=(seed^(seed>>15))*0x846ca68bu; seed^=seed>>16;
    s->seed=seed;
    double moisture=.5;
    if(w->moisture) {
        SurfacePoint g=qlcg_frame_coordinates(&n->frame,s->x,s->z);
        SurfaceMacroContext c=qlcg_macro_context(w,g.x,g.z);
        double m=qlcg_read_macro(w,&c,w->moisture);
        if(isfinite(m)) moisture=clampd(m,0,1);
    }
    double roll=((seed>>9)&0xffff)/65536.0;
    if(roll<qlcg_source_pool_chance(moisture))
        s->spring_form=SOURCE_POOL_FORM_BASE+(int)((seed>>21)%SOURCE_POOL_FORM_COUNT);
    else
        s->spring_form=(int)(seed%SOURCE_LEGACY_FORM_COUNT);
    if(s->snowmelt) s->spring_form=10+(seed&1);
    else if(s->spring_form==10||s->spring_form==11) s->spring_form=(seed&1)?4:8;
    if(s->plan_kind==SRC_PLAN_TARN) s->spring_form=9;
    if(s->plan_kind==SRC_PLAN_HANGING) s->spring_form=12+(seed%3);
    s->mirror=(seed&256)?-1:1;
    if(hypot(s->direction_x,s->direction_z)<.1) {
        double angle=(seed&65535)*6.283185307/65536;
        s->direction_x=cos(angle); s->direction_z=sin(angle);
    }
    qlcg_source_material_set(s,source_material(w,n,s));
}

/* 大池收敛高频粗糙，避免 2m 网格扇贝边 */
static double basin_rough(const RiverSource *s,double radius) {
    double rough=s->finish.roughness;
    if(radius>8) rough*=fmax(.30,8/radius);
    return rough;
}

static double basin_shelf(const RiverSource *s,double radius,double depth) {
    static const double base[5]={.28,.18,.12,.10,.22};
    int m=s->material>=1&&s->material<=5?s->material-1:1;
    double shelf=base[m]+.10*fmin(1,radius/18);
    shelf=clampd(shelf,.08,.40);
    return fmin(shelf,depth*.55);
}

static double basin_shape(const SourceForm *f,double rx,double rz,double radius,
                          double along,double across,double rough,double phase) {
    double u=along/radius-f->shift, v=across/radius;
    double x=u-f->skew*v;
    double r;
    if(f->lobe)
        r=fmin(hypot(x/rx,(v-f->lobe)/rz),hypot(x/rx,(v+f->lobe)/rz));
    else
        r=hypot(x/rx,v/rz);
    double a=atan2(v/rz,x/rx);
    double k1=radius>12?2:3,k2=radius>12?3:5,k3=radius>12?4:7;
    r/=1+rough*(sin(a*k1+phase)+.55*cos(a*k2-phase)+.25*sin(a*k3+phase));
    return r;
}

static double basin_depth(double r,double depth,double shelf) {
    if(r<=.55) return depth*(1-.10*smoothd(r/.55));
    if(r<=.82) return mixd(depth,shelf,smoothd((r-.55)/.27));
    if(r<=1.0) return mixd(shelf,0,smoothd((r-.82)/.18));
    return 0;
}

int qlcg_source_basin(const RiverSource *s,BasinMode mode,double dx,double dz,
                      double natural,RiverSurfaceSample *out) {
    if(!s||!out||!isfinite(natural)) return 0;
    if(out->kind==WATER_KIND_SEA||out->kind==WATER_KIND_LAKE) return 0;
    const SourceForm *f=qlcg_source_form(s->spring_form);
    double radius=0,level=0,depth=0,rx=1,rz=1;
    if(mode==BASIN_HEAD) {
        if(!s->connected||!f->pooled||s->damp_only) return 0;
        radius=qlcg_source_pool_radius(s);
        level=isfinite(s->surface_level)?s->surface_level:s->level;
        depth=clampd(f->depth*s->finish.depth,.35,2.4);
        rx=f->rx;
        rz=f->rz*fmax(.7,s->finish.across);
    } else {
        if(s->connected||!s->pool||s->damp_only) return 0;
        radius=s->pool_rx;
        level=isfinite(s->pool_level)?s->pool_level:s->level;
        depth=clampd(s->pool_depth,.35,2.6);
        rz=radius>1e-6?s->pool_rz/radius:1.0;
    }
    if(!(radius>0)||!isfinite(level)||!isfinite(depth)||!isfinite(rz)||rz<=0) return 0;
    if(out->level>-1e8f&&out->level>level) level=out->level;
    double along=dx*s->direction_x+dz*s->direction_z;
    double across=(-dx*s->direction_z+dz*s->direction_x)*s->mirror;
    if(mode==BASIN_HEAD) along+=radius*.50; /* 池心退到溪头上游 */
    double phase=(s->seed&65535)*.0001;
    double r=basin_shape(f,rx,rz,radius,along,across,basin_rough(s,radius),phase);
    if(r>=1.35) return 0;
    double shelf=basin_shelf(s,radius,depth);
    double bed=level-basin_depth(r,depth,shelf);
    if(r>1.0) bed=mixd(level,natural,smoothd((r-1.0)/.35));
    double u=along/radius-f->shift, v=across/radius;
    double bar=fmax(f->bar,s->finish.sediment*.35);
    if(bar>0) {
        double bu=mode==BASIN_TERMINAL?-.80:.20;
        bed+=bar*exp(-((u-bu)*(u-bu)*5+(v-.65)*(v-.65)*12))*(1-smoothd(r/1.2));
    }
    if(mode==BASIN_TERMINAL)
        bed+=.22*exp(-((u+.80)*(u+.80))/.05)*exp(-(v*v)/.12)*(1-smoothd(r/1.2));
    if(s->pool==2||s->material==5)
        bed+=.05*(sin(u*9+phase)+cos(v*11))*(1-smoothd(r/1.2));
    double lip=clampd(.04+.08*f->rise*s->finish.relief,.04,.16);
    if(natural>level+.02&&r>.94&&r<1.06)
        bed=fmax(bed,level+lip*smoothd((r-.94)/.06));
    if(!isfinite(bed)) return 0;
    if(bed>natural) bed=natural;
    if(!isfinite(out->height)||out->height>bed) out->height=bed;
    out->damp=fmaxf(out->damp,(float)(1-smoothd((r-.85)/.55)));
    out->sediment=fmaxf(out->sediment,(float)(fmax(f->bar,s->finish.sediment)*(1-smoothd(r/1.7))));
    if(r<1.0&&natural>=level-.02&&bed<level-.03) {
        out->level=(float)level;
        out->kind=(uint8_t)(mode==BASIN_HEAD?WATER_KIND_STATIC:
            (s->pool==3?WATER_KIND_LAKE:WATER_KIND_STATIC));
        out->decorative=1;
        out->fresh=s->material!=3;
        out->width=(float)fmin(64.0,2*radius);
        out->discharge=(float)s->discharge;
        out->channel_depth=0;
        out->grade=0;
        double dl=hypot(s->direction_x,s->direction_z);
        if(dl<1e-9) dl=1;
        out->east=(float)(s->direction_x/dl);
        out->north=(float)(-s->direction_z/dl);
        double spring=0;
        if(mode==BASIN_HEAD) spring=110*(1-smoothd(r/1.02));
        else if(mode==BASIN_SPRING) spring=(s->pool==3?55:75)*(1-smoothd(r/1.02));
        else spring=35*exp(-((u+.85)*(u+.85))/.08)*(1-r);
        out->source_effects.spring=(uint8_t)fmax(out->source_effects.spring,clampd(spring,0,255));
        if(s->material==3)
            out->source_effects.salt=(uint8_t)fmax(out->source_effects.salt,224*(1-smoothd((r-.55)/.40)));
    }
    return 1;
}

void qlcg_source_form_surface_mode(const RiverSource *s,BasinMode mode,double dx,double dz,
                                   double natural,RiverSurfaceSample *out) {
    const SourceForm *f=qlcg_source_form(s->spring_form);
    if(mode==BASIN_HEAD&&f->pooled&&!s->damp_only) {
        qlcg_source_basin(s,mode,dx,dz,natural,out);
        return;
    }
    if((mode==BASIN_SPRING||mode==BASIN_TERMINAL)&&s->pool&&!s->damp_only) {
        qlcg_source_basin(s,mode,dx,dz,natural,out);
        return;
    }
    double scale=s->connected?s->spring_radius:s->pool_rx;
    if(scale<=0) return;
    double along=dx*s->direction_x+dz*s->direction_z,across=(-dx*s->direction_z+dz*s->direction_x)*s->mirror;
    double u=along/scale-f->shift,v=across/scale;
    double rz=s->connected?f->rz*s->finish.across:s->pool_rz/s->pool_rx;
    double rx=s->connected?f->rx:1,x=u-f->skew*v;
    double r=f->lobe?fmin(hypot(x/rx,(v-f->lobe)/rz),hypot(x/rx,(v+f->lobe)/rz)):hypot(x/rx,v/rz);
    if(r>=1.7*(1+1.8*s->finish.roughness)) return;
    double a=atan2(v/rz,x/rx),phase=(s->seed&65535)*.0001;
    r/=1+s->finish.roughness*(sin(a*3+phase)+.55*cos(a*5-phase)+.25*sin(a*7+phase));
    if(r>=1.7) return;
    out->damp=fmaxf(out->damp,(float)(1-smoothd((r-.7)/1.0)));
    if(out->kind==WATER_KIND_RIVER||out->kind==WATER_KIND_FALL) {
        if(s->material==1&&!s->damp_only&&s->connected&&out->decorative&&out->level>out->height+.02f)
            out->source_effects.spring=(uint8_t)fmax(out->source_effects.spring,
                255*(1-smoothd(hypot(along,across)/6)));
        return;
    }
    if(out->kind&&out->level>out->height+.02f) return;
    double sediment=fmax(f->bar,s->finish.sediment);
    out->sediment=fmaxf(out->sediment,(float)(sediment*(1-smoothd(r/1.7))));
    double level=s->connected?s->surface_level:s->pool_level;
    double depth=s->connected?f->depth*s->finish.depth:s->pool_depth;
    double rise=fmin(.30,f->rise*s->finish.relief*smoothd((r-.65)/.4)*
        (1-smoothd((r-1)/.7))*smoothd((.2-u)/.7));
    double h=out->height+rise;
    if(s->material==3) {
        double rim=smoothd((r-.55)/.30)*(1-smoothd((r-1.27)/.40));
        out->source_effects.salt=(uint8_t)fmax(out->source_effects.salt,224*rim);
    }
    if(!s->damp_only&&r<1.35&&natural>=level-.08) {
        double bar=fmax(f->bar,s->finish.sediment*.35)*exp(-((u-.20)*(u-.20)*5+(v-.65)*(v-.65)*12));
        double hollows=(s->pool==2||s->material==5)?.06*(sin(u*9+phase)+cos(v*11)):0;
        double bed=level-depth*(1-smoothd(r/1.2))+bar+hollows;
        double blend=1-smoothd((r-.65)/.70);
        h=fmin(out->height,h+(bed-h)*blend);
        if(h<level-.025) {
            out->level=(float)level; out->kind=(s->connected?f->pool:s->pool)==3?WATER_KIND_LAKE:WATER_KIND_STATIC;
            out->decorative=1; out->fresh=s->material!=3; out->width=(float)(scale*2);
            out->source_effects.salt=s->material==3?255:0;
            if(s->material==1)
                out->source_effects.spring=(uint8_t)(255*(1-smoothd(hypot(along,across)/6)));
            out->discharge=(float)s->discharge; out->east=out->north=out->grade=0;
            out->channel_depth=0;
        }
    }
    out->height=(float)h;
}

void qlcg_source_form_surface(const RiverSource *s,double dx,double dz,double natural,RiverSurfaceSample *out) {
    qlcg_source_form_surface_mode(s,s->connected?BASIN_HEAD:BASIN_SPRING,dx,dz,natural,out);
}
