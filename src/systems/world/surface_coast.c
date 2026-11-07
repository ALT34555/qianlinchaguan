#include "surface_coast.h"
#include "gen/gen_noise.h"
#include "../../core/math_util.h"
#include <math.h>

enum { COAST_RASTER = 97 };
static const int8_t coast_atlas[6][COAST_RASTER*COAST_RASTER] = {
#include "coast_atlas_data.inc"
};

static double sample_height(int id,double u,double v) {
    double x=qlcg_clampf((float)u,0,1)*(COAST_RASTER-1);
    double z=qlcg_clampf((float)v,0,1)*(COAST_RASTER-1);
    int ix=qlcg_clampi((int)x,0,COAST_RASTER-2);
    int iz=qlcg_clampi((int)z,0,COAST_RASTER-2);
    double dx=x-ix,dz=z-iz,result=0;
    for(int k=0;k<4;k++) {
        double q=coast_atlas[id][(iz+k/2)*COAST_RASTER+ix+k%2]/127.0;
        result+=q*fabs(q)*120*(k%2?dx:1-dx)*(k/2?dz:1-dz);
    }
    return result;
}

CoastSample qlcg_coast_surface(const MacroWorld *w,const SurfaceMacroContext *c,
                              double wx,double wz,double base,double natural) {
    CoastSample out={.height=natural};
    if(!w->water||!w->rift||!w->uplift||!w->shear||!w->basin||!w->receiver||c->pole>=0)
        return out;
    int ids[4]={c->a,c->b,c->c,c->d};
    float protection=0;
    float sea=0,land=0,lake=0;
    for(int k=0;k<4;k++) {
        int i=ids[k],r=w->receiver[i];
        float weight=(float)((k%2?c->tx:1-c->tx)*(k/2?c->tz:1-c->tz));
        if(w->water[i]==1) sea+=weight;
        else if(w->water[i]==0) land+=weight;
        else lake+=weight;
        if(w->main_river_width&&w->main_river_width[i]>0&&r>=0&&r<w->cell_count&&w->water[r]==1)
            protection+=weight;
    }
    if(!sea||!land||fabs(base)>400*w->height_scale) return out;
    double lat=wz/w->circumference*6.283185307179586;
    double lon=wx/w->circumference*6.283185307179586;
    double cosine=qlcg_world_flat(w)?1:cos(lat);
    if(cosine<.1) return out;
    double gradient=0;
    for(int k=0;k<4;k++) {
        double weight=(k%2?c->tx:1-c->tx)*(k/2?c->tz:1-c->tz);
        gradient+=weight*fabs(w->height[ids[k]]-base);
    }
    gradient*=2;
    double band=fmax(16,fmin(340*w->height_scale,gradient*.30));
    double u=.5-base/(2*band);
    if(u<=0||u>=1) return out;
    double px=cos(lon)*cosine,py=sin(lat),pz=sin(lon)*cosine;
    if(qlcg_world_flat(w)) { px=wx/w->radius; py=1; pz=wz/w->radius; }
    double up=qlcg_read_macro(w,c,w->uplift),rift=qlcg_read_macro(w,c,w->rift);
    double shear=qlcg_read_macro(w,c,w->shear),basin=qlcg_read_macro(w,c,w->basin);
    double activity=w->opt.tectonic_activity/9.0;
    double freq=w->columns*.53;
    double n=qlcg_base_noise_d(px*freq,py*freq,pz*freq,w->opt.seed^0x636f6173u);
    double push=w->ridge_push_e?qlcg_read_macro(w,c,w->ridge_push_e):0;
    double push_n=w->ridge_push_n?qlcg_read_macro(w,c,w->ridge_push_n):0;
    double v=.5+.48*qlcg_base_noise_d(px*freq+push,py*freq+push_n,pz*freq,w->opt.seed^0x73686f72u);
    float rock=qlcg_smoothf(.17f,1.12f,(float)(.10+up*.9+shear*.38+rift*.22+
                                      fmin(.31,gradient/1100)+activity*.1+n*.39));
    float mud=(1-rock)*qlcg_smoothf(.15f,.78f,(float)(.43+basin*.48-gradient/950+n*.44));
    float sand=1-rock-mud;
    float decor=qlcg_smoothf(.05f,.65f,(float)(.22+n*.62+shear*.18+rift*.18));
    float weight=qlcg_smoothf(0,.22f,(float)u)*qlcg_smoothf(0,.22f,(float)(1-u))*
                 qlcg_smoothf(0,.13f,fminf(sea,land))*qlcg_smoothf(.1f,.24f,(float)cosine)*
                 (1-qlcg_smoothf(0,.12f,lake))*(1-qlcg_smoothf(.01f,.24f,protection));
    if(weight<=0) return out;
    const float geology[3]={mud,sand,rock};
    double target=0;
    for(int k=0;k<3;k++) {
        if(geology[k]<=0) continue;
        double h=sample_height(k*2,u,v);
        if(decor>0) h+=(sample_height(k*2+1,u,v)-h)*decor;
        target+=geology[k]*h;
    }
    float cliff=qlcg_smoothf(.36f,.78f,rock);
    if(cliff>0 && target>0) {
        double broad=w->radius/82.0,detail=w->radius/19.0;
        float shape=(float)qlcg_base_noise_d(px*broad,py*broad,pz*broad,w->opt.seed^0x636c6966u);
        float grain=(float)qlcg_base_noise_d(px*detail,py*detail,pz*detail,w->opt.seed^0x73637270u);
        float t=(float)target;
        float face=qlcg_smoothf(0,7.8f+1.9f*shape+.65f*grain,t);
        float shoulder=qlcg_smoothf(5+2.2f*shape,24+5*shape,t);
        float relief=qlcg_smoothf(-.55f,.6f,shape);
        float taper=1-qlcg_smoothf(35,120,t);
        target+=cliff*((27+8*shape+2*grain)*face+(2+9*relief)*shoulder)*taper;
    } else if(cliff>0) {
        target-=cliff*12*qlcg_smoothf(0,15,(float)-target);
    }
    out.height=natural+(target*w->height_scale-natural)*weight;
    out.mud=mud; out.sand=sand; out.rock=rock; out.weight=weight;
    return out;
}
