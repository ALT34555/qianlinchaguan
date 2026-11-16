#include "hydrology_headwater.h"
#include "surface_detail.h"
#include "surface_internal.h"
#include "hydrology_river.h"
#include "hydrology_archive.h"
#include "gen/gen_noise.h"
#include <math.h>

static const struct {
    const char *name;
    int effect,channels;
    double rise,spread,grade,valley;
} REGIONS[HEAD_REGION_COUNT]={
    {"山涧型河源",HEAD_CLIFF,4,140,140,.055,.075},
    {"水网型河源",HEAD_HOLLOW,9,12,24,.009,.28},
    {"融雪型河源",HEAD_SNOW,5,110,140,.040,.14},
    {"汇流型河源",HEAD_GRAVEL,4,65,110,.025,.19},
    {"沼泽河源",HEAD_MARSH,7,3,7,.003,.42},
    {"湖泊河源",HEAD_HOLLOW,3,30,75,.012,.24},
    {"泉水河源",HEAD_CLIFF,3,65,85,.030,.10}
};
static double clamp(double x,double a,double b) { return fmax(a,fmin(b,x)); }
static double smooth(double x) { x=clamp(x,0,1);return x*x*(3-2*x); }
static double mix(double a,double b,double t) { return a+(b-a)*t; }
static double random_next(uint32_t *seed) {
    uint32_t t=(*seed+=0x6d2b79f5u);
    t=(t^(t>>15))*(t|1u);t^=t+(t^(t>>7))*(t|61u);
    return (t^(t>>14))/4294967296.0;
}
const char *qlcg_headwater_region_name(int kind) {
    return kind>=0&&kind<HEAD_REGION_COUNT?REGIONS[kind].name:"旧版河源";
}
void qlcg_headwater_region_shape(HeadwaterPlan *p) {
    uint32_t rng=p->seed^0x7265676eu;HeadwaterDesign *s=&p->shape;
    s->family=(int)(random_next(&rng)*4);s->aspect=.80+.35*random_next(&rng);
    s->shear=(random_next(&rng)-.5)*.4;s->warp=.08+.16*random_next(&rng);
    s->ridge=.12+.22*random_next(&rng);s->spread=.8+.4*random_next(&rng);
    s->bend=(random_next(&rng)-.5)*.10;
    for(int k=0;k<3;k++) {
        double *a=s->peaks[k];a[0]=-.2-.65*random_next(&rng);
        a[1]=(k==1?-1:1)*(.18+.30*random_next(&rng));
        a[2]=.55+.45*random_next(&rng);a[3]=.045+.10*random_next(&rng);
    }
}
int qlcg_headwater_region_build(const MacroWorld *w,int cell,HeadwaterPlan *p) {
    if(qlcg_river_archive_terrain(w)>=8&&w->height[cell]>=4)return qlcg_headwater_landform_build(w,cell,p);
    const MacroCellPlan *m=qlcg_macro_cell_plan(w,cell);if(!m||!m->head)return 0;
    SourcePlan source=m->source;SurfaceFrame frame=source.frame;
    double span=w->circumference/w->columns;
    double east=frame.flat?span:span*fmax(0,cos(fmin(1.5707963267948966,fabs(frame.lat)+span/(2*frame.radius))));
    double usable=fmin(span,east);if(usable<320)return 0;
    p->cell=cell;p->seed=w->opt.seed^((uint32_t)(cell+1)*0x68656164u);
    p->dx=source.dx;p->dz=source.dz;p->design=2;
    SurfacePoint origin=qlcg_frame_coordinates(&frame,0,0);
    p->material=qlcg_world_base_chunk_material(w,(int)floor(origin.x/32),(int)floor(origin.z/32));
    p->moisture=clamp(.65*w->moisture[cell]+.35*w->rainfall[cell]/fmax(150,w->evaporation[cell]),0,1);
    double amp=w->season_amp?w->season_amp[cell]:0;
    p->cold=w->temperature[cell]-amp;p->warm=w->temperature[cell]+amp;
    double wet=smooth((p->moisture-.2)/.65),slope=smooth(source.slope/.035);
    double snow=smooth((-p->cold-1)/10)*smooth(w->rainfall[cell]/250)*
        fmax(smooth((10-p->warm)/12),smooth((w->height[cell]-1200)/1800))*smooth((p->warm+1)/3);
    double weights[7]={.7+5*slope*wet,4*wet*(1-slope),6*snow,
        1.2+wet,5*wet*wet*(1-slope)*(1-slope),.6+1.5*wet*(1-slope),.6+3*(1-wet)};
    uint32_t rng=p->seed^0x636c696du;double sum=0;for(int k=0;k<7;k++)sum+=weights[k];
    double pick=random_next(&rng)*sum;p->region=HEAD_REGION_SPRING;
    for(int k=0;k<7;k++) { pick-=weights[k];if(pick<=0) { p->region=k;break; } }
    int type=p->region;p->kind=REGIONS[type].effect;p->springs=REGIONS[type].channels;
    if(type==HEAD_REGION_SNOW&&p->warm<=-1)p->kind=HEAD_ALPINE;
    p->radius=fmin(4096,usable*(.20+.06*random_next(&rng)));
    p->radius=fmax(96,p->radius);p->rise=REGIONS[type].rise+REGIONS[type].spread*random_next(&rng);
    p->skew=(random_next(&rng)-.5)*.10;p->phase=random_next(&rng)*6.283185307;
    qlcg_headwater_region_shape(p);
    double along=-fmax(80,p->radius*.62),side=p->skew*p->radius;
    p->x=p->dx*along-p->dz*side;p->z=p->dz*along+p->dx*side;
    SurfacePoint head=qlcg_frame_coordinates(&frame,p->x,p->z);
    p->source_ground=qlcg_natural_base_height(w,head.x,head.z);
    p->outlet_ground=qlcg_natural_base_height(w,origin.x,origin.z);
    p->run=fmin(1536,fmax(96,p->radius*.45));p->outlet_level=p->outlet_ground-.8;
    double downstream;int connection=qlcg_river_head_constraint(w,cell,&p->run,&downstream);
    if(connection)p->outlet_level=fmax(p->outlet_level,downstream+.05);
    if(connection==2)p->outlet_level=downstream;
    double drop=fmax(2,p->radius*REGIONS[type].grade);
    p->source_level=p->outlet_level+drop;
    if(type!=HEAD_REGION_NETWORK&&type!=HEAD_REGION_MARSH)
        p->source_level=fmax(p->source_level,p->source_ground+8+random_next(&rng)*20);
    p->pool_radius=type==HEAD_REGION_LAKE?clamp(p->radius*.09,24,240):
        type==HEAD_REGION_SPRING?10+8*random_next(&rng):0;
    return 1;
}
double qlcg_headwater_region_height(const HeadwaterPlan *p,double x,double z,double base) {
    double u=(x*p->dx+z*p->dz)/p->radius,v=(-x*p->dz+z*p->dx)/p->radius;
    double r=hypot((u+.30),v/.78);if(r>=1.06||u>=.18)return base;
    double broad=qlcg_base_noise_d(u*2.1+9,v*2.1+17,0,p->seed^0x74657272u);
    double a=u+p->shape.warp*broad,b=v/p->shape.aspect+p->shape.shear*u;
    double fold=qlcg_base_noise_d(a*4.3,b*4.3,0,p->seed^0x6c616e64u);
    double grain=qlcg_base_noise_d(a*11.1,b*11.1,0,p->seed^0x73686170u);
    double edge=(1-smooth((r-.65)/(.34+.05*broad)))*(1-smooth(u/.18));
    double ridge=1-fabs(fold),mass=0;
    for(int k=0;k<3;k++) {
        const double *q=p->shape.peaks[k];double dx=a-q[0],dz=b-q[1]-fold*.08;
        mass=fmax(mass,q[2]*exp(-(dx*dx*.4+dz*dz)/q[3]));
    }
    double center=p->skew*clamp(-u/.62,0,1);
    double valley=1-smooth((fabs(v-center)-.055)/fmax(.035,REGIONS[p->region].valley-.055));
    double height=p->rise*(mass*.72+ridge*ridge*.28)*(1+grain*.16);
    if(p->region==HEAD_REGION_GORGE||p->region==HEAD_REGION_SPRING)
        height=qlcg_detail_terrace_height(height,18+12*p->shape.spread,.22+.16*p->shape.warp);
    if(p->region==HEAD_REGION_MARSH)height=p->rise*(.22+.26*broad+.18*grain);
    if(p->region==HEAD_REGION_NETWORK)height=p->rise*(.35+.45*ridge+.20*grain);
    double datum=p->outlet_level+(p->source_level-p->outlet_level)*clamp(-u/.62,0,1.8);
    double ground=fmax(base,datum+.8)+height;
    if(p->region==HEAD_REGION_NETWORK||p->region==HEAD_REGION_MARSH)ground=datum+.8+height;
    ground=mix(ground,datum+.8,valley);
    return mix(base,ground,edge);
}
void qlcg_headwater_region_color(const HeadwaterPlan *p,double x,double z,float temperature,uint8_t color[3]) {
    double u=(x*p->dx+z*p->dz)/p->radius,v=(-x*p->dz+z*p->dx)/p->radius;
    double r=hypot(u+.30,v/.78);if(r>=1||u>=.18)return;
    double edge=(1-smooth((r-.65)/.35))*(1-smooth(u/.18));
    double noise=qlcg_base_noise_d(u*8,v*8,0,p->seed^0x736e6f77u);
    double snow=p->region==HEAD_REGION_SNOW?(1-smooth((temperature+2)/12))*smooth((noise-u*.7+.2)/.8)*edge:0;
    double wet=(p->region==HEAD_REGION_MARSH?.5:p->region==HEAD_REGION_NETWORK?.20:.05)*edge;
    double rock=(p->region==HEAD_REGION_GORGE||p->region==HEAD_REGION_SPRING)?.28*edge*smooth(fabs(v)/.3):0;
    static const double moss[3]={77,105,68},stone[3]={119,125,125},white[3]={227,236,239};
    for(int k=0;k<3;k++)color[k]=(uint8_t)clamp(mix(mix(mix(color[k],stone[k],rock),moss[k],wet),white[k],snow),0,255);
}
