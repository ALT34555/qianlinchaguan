#include "river_junction.h"
#include "gen/gen_noise.h"
#include <math.h>

typedef struct { double x,z,axis_x,axis_z,along,across,gain; } JunctionFeature;
#include "junction_features.inc"

static uint32_t hash(uint32_t x) {
    x=(x^(x>>16))*0x7feb352du;
    x=(x^(x>>15))*0x846ca68bu;
    return x^(x>>16);
}
static double smooth(double x) {
    double t=fmax(0,fmin(x,1));
    return t*t*(3-2*t);
}

void qlcg_junction_configure(RiverJunction *plan,uint32_t seed,double x,double z) {
    double length=hypot(x,z);
    plan->east=x/length;
    plan->south=z/length;
    plan->seed=hash(seed);
    plan->first=plan->seed&15;
    plan->second=(plan->first+1+((plan->seed>>8)%15))&15;
    plan->blend=.28+.44*((plan->seed>>16)&255)/255;
    plan->mirror=(plan->seed&256)?-1:1;
}

static double lobe(const JunctionFeature *f,double x,double z) {
    if (f->gain<=0) return 0;
    double dx=x-f->x,dz=z-f->z;
    double a=(dx*f->axis_x+dz*f->axis_z)/f->along;
    double b=(-dx*f->axis_z+dz*f->axis_x)/f->across;
    double support=fmax(0,1-a*a-b*b);
    return support*support*f->gain;
}

static double bypass(const JunctionFeature *f,double x,double z) {
    if (f->gain<=0) return 0;
    JunctionFeature channel=*f;
    channel.along*=1.22;
    channel.across*=.43;
    double dx=-f->axis_z*f->across*.92,dz=f->axis_x*f->across*.92;
    return fmax(lobe(&channel,x-dx,z-dz),lobe(&channel,x+dx,z+dz));
}

static void feature_field(int tile,double x,double z,JunctionRelief *out) {
    const JunctionFeature *f=junction_features[tile];
    out->bar=fmax(lobe(f,x,z),lobe(f+1,x,z));
    out->shelf=fmax(lobe(f+2,x,z),lobe(f+3,x,z));
    out->scour=lobe(f+4,x,z);
    out->branch=fmax(bypass(f,x,z),bypass(f+1,x,z));
}

void qlcg_junction_relief(const RiverJunction *plan,double x,double z,JunctionRelief *out) {
    double u=(x*plan->east+z*plan->south)/plan->radius;
    double v=(-x*plan->south+z*plan->east)/plan->radius;
    double du=.065*qlcg_base_noise_d(u*3.7,v*3.7,7,plan->seed);
    double dv=.065*qlcg_base_noise_d(u*3.7,v*3.7,19,plan->seed^0x328dafb1u);
    JunctionRelief a,b;
    feature_field(plan->first,u+du,plan->mirror*v+dv,&a);
    feature_field(plan->second,(u-du)*1.10,-plan->mirror*v-dv,&b);
    double t=plan->blend;
    out->bar=fmax((1-.16*t)*a.bar,(.84+.16*t)*b.bar);
    out->shelf=(1-t)*a.shelf+t*b.shelf;
    out->scour=(1-t)*a.scour+t*b.scour;
    out->branch=fmax((1-.16*t)*a.branch,(.84+.16*t)*b.branch);
}

double qlcg_junction_weight(const RiverJunction *plan,double distance) {
    return 1-smooth((distance/plan->radius-.53)/.47);
}
