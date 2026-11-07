#include "macro_waterfalls.h"
#include "gen/gen_noise.h"
#include "../../core/math_util.h"
#include <math.h>
static double smooth(double t) { t=fmax(0,fmin(1,t)); return t*t*(3-2*t); }
static double strata(const MacroWaterfall *p,double u,uint32_t salt) {
    static const double edges[4]={-.73,-.24,.18,.68};
    uint32_t seed=qlcg_hash_u32((uint32_t)p->begin^(uint32_t)(p->width*31)^salt);
    double previous=(seed%4)/3.0,value=previous;
    for(int i=0;i<4;i++) {
        uint32_t key=qlcg_hash_u32(seed+(uint32_t)(i+1)*2654435761u);
        double edge=edges[i]+(((key>>8)&255)/255.0-.5)*.16;
        double next=(key%4)/3.0;
        value+=(next-previous)*smooth((u-edge)/.055);
        previous=next;
    }
    return value;
}
double qlcg_macro_fall_level(const MacroWaterfall *p,double arc,double lateral,double base) {
    if(p->state!=2 || arc<=p->begin || arc>=p->end) return base;
    double length=p->end-p->begin,u=fmax(-1.3,fmin(1.3,lateral));
    double shift=(strata(p,u,79)*2-1)*fmin(14,p->width*.12);
    if(p->style==1 || p->style==3) shift+=fmin(length*.07,p->width*.35)*(1-u*u);
    if(p->style==2) shift*=.7;
    if(p->style==3) shift+=(strata(p,u,191)*2-1)*fmin(8,p->width*.08);
    if(p->style==4) shift+=u*fmin(6,p->width*.1);
    if(p->style==5) shift+=(strata(p,u,257)*2-1)*fmin(4,p->width*.04);
    double x=arc-p->begin-shift,run=fmax(5,fmin(12,(p->top-p->bottom)*.075));
    double shelf=.28*strata(p,u,331);
    double split=run+5+13*strata(p,u,419);
    double drop=(1-shelf)*smooth((x-length*.12)/run)+shelf*smooth((x-length*.12-split)/run);
    if(p->style==4) drop=.35*drop+.65*smooth((x-length*.32)/run);
    if(p->style==7) drop=.57*drop+.26*smooth((x-length*.28)/run)+.17*smooth((x-length*.43)/run);
    return fmin(base,p->top-(p->top-p->bottom)*drop);
}
double qlcg_macro_fall_width(const MacroWaterfall *p,double arc) {
    if(p->state!=2 || p->style!=6) return 1;
    double t=(arc-p->begin)/(p->end-p->begin);
    return 1-.64*smooth(t/.10)*(1-smooth((t-.3)/.4));
}
double qlcg_macro_fall_island(const MacroWaterfall *p,double arc,double lateral) {
    if(p->state!=2) return 0;
    double x=arc-p->begin-(p->end-p->begin)*.12;
    double band=fmin(fabs(lateral+.37),fabs(lateral-.59));
    double width=p->style==3?.065:.038;
    return (1-smooth((band-width*.3)/width))*smooth((x+24)/18)*(1-smooth((x-24)/24));
}

/* 噪声控制水平退壁深度 */
double qlcg_macro_fall_recess(uint32_t seed,int cell,double along,double offset,double radius) {
    double bank=smooth((radius-fabs(offset))/fmax(24,radius*.20));
    if(bank<=0) return 0;
    seed^=qlcg_hash_u32((uint32_t)cell^0x726f636bu);
    double x=offset/fmax(32,fmin(110,radius*.24));
    double broad=.5+.5*qlcg_base_noise_d(x,0,0,seed^127u);
    double warp=12*qlcg_base_noise_d(x/1.7,0,0,seed^31u);
    double layer=qlcg_base_noise_d((along+warp)/24,x/.65,0,seed^313u);
    return bank*(2.5+6.5*broad+3.5*smooth((layer+.5)));
}
