#include "astronomy_infinite.h"
#include "../../core/math_util.h"
#include <math.h>
#include <string.h>

static AstraEclipse event_at(uint32_t seed,int64_t slot) {
    uint32_t h=qlcg_hash_u32(seed^(uint32_t)slot^qlcg_hash_u32((uint32_t)((uint64_t)slot>>32))^0xec11f1a7u);
    uint32_t t=qlcg_hash_u32(h);
    int kind=(h&1)?1:2;
    double hour=kind==1?10+(t>>8)/16777216.0*4:2+(t>>8)/16777216.0*2;
    double peak=(slot*32.0+(h>>4)%32+hour/24)*86400000;
    double half=(.3+(qlcg_hash_u32(t)>>8)/16777216.0*.9)*3600000;
    return (AstraEclipse){peak-half,peak,peak+half,kind,1+(int)((h>>1)%3)};
}

AstraEclipse qlcg_infinite_eclipse(uint32_t seed,double time) {
    int64_t slot=(int64_t)floor(time/(32*86400000.0));
    AstraEclipse e=event_at(seed,slot);
    if(e.end<time) e=event_at(seed,slot+1);
    return e;
}

static void offset(const float *p,double angle,float *out) {
    float axis[3]={p[2],0,-p[0]};
    float d=hypotf(axis[0],axis[2]);
    if(d<.001f) { axis[0]=1; axis[2]=0; d=1; }
    for(int k=0;k<3;k++) out[k]=(float)(p[k]*cos(angle)+axis[k]/d*sin(angle));
}

void qlcg_infinite_astra(const WorldCreatorOptions *opt,const CalendarClock *clock,AstraFrame *f) {
    f->solar_type=f->lunar_type=0;
    f->solar_cover=f->lunar_cover=f->lunar_penumbra=0;
    f->shadow_radius=f->penumbra_radius=0; f->shadow_dir[1]=-1;
    double now=clock?clock->unix_ms:0;
    AstraEclipse e=qlcg_infinite_eclipse(opt->seed,now);
    if(now<e.start || now>e.end) return;
    double t=(now-e.peak)/(e.end-e.peak);
    if(e.kind==1) {
        double r=f->sun_radius,oc=r*(e.type==3?.88:1.06);
        double off=(r+oc)*t;
        if(e.type==1) off=copysign(hypot(off,r*.9),t);
        offset(f->sun,off,f->occluder);
        f->occluder_radius=(float)oc;
        f->solar_cover=(float)qlcg_astra_overlap(r,oc,fabs(off));
        f->solar_type=f->solar_cover<=0?0:fabs(off)<=fabs(r-oc)?e.type:1;
    } else {
        double r=f->moon_radius,shadow=r*(e.type==1?0:e.type==2?.8:1.6);
        double pen=r*2.3,off=(r+pen)*t;
        offset(f->moon,off,f->shadow_dir);
        f->shadow_radius=(float)shadow; f->penumbra_radius=(float)pen;
        f->lunar_cover=(float)qlcg_astra_overlap(r,shadow,fabs(off));
        f->lunar_penumbra=(float)qlcg_astra_overlap(r,pen,fabs(off));
        f->lunar_type=f->lunar_cover>.999f?3:f->lunar_cover>0?2:f->lunar_penumbra>0?1:0;
    }
}
