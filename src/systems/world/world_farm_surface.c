#include "world_farm_surface.h"
#include "../../core/math_util.h"
#include <math.h>
#include <string.h>

void qlcg_farm_surface_footprint(const QlcgFarmPlot *plot,float points[QLCG_FARM_FOOTPRINT_POINTS][2]) {
    float c=cosf(plot->yaw),s=sinf(plot->yaw);
    points[0][0]=plot->x;points[0][1]=plot->z;
    for (int k=1;k<QLCG_FARM_FOOTPRINT_POINTS;k++) {
        float u=(k&1?-.5f:.5f)*plot->width,v=(k<3?-.5f:.5f)*plot->depth;
        points[k][0]=plot->x+u*c-v*s;
        points[k][1]=plot->z+u*s+v*c;
    }
}

bool qlcg_farm_surface_dry(float ground,float water) {
    return isfinite(ground)&&(!isfinite(water)||water<=ground+.02f);
}

bool qlcg_farm_surface_plot(QlcgFarmSurface *surface,const QlcgFarmPlot *plot) {
    memset(surface,0,sizeof(*surface));
    if(!plot||!plot->farm_type_id)return false;
    const QlcgFarmlandType *type=qlcg_farmland_type_find(plot->farm_type_id);
    if(!type)return false;
    surface->plot=*plot;surface->type=type;
    surface->cosine=cosf(plot->yaw);surface->sine=sinf(plot->yaw);
    return true;
}

bool qlcg_farm_surface_at(QlcgFarmSurface *surface,const MacroWorld *world,int cx,int cz) {
    QlcgFarmPlot plot;
    if(!world||!world->opt.artificial_enabled||!qlcg_civ_farm_at(world,cx,cz,&plot)) {memset(surface,0,sizeof(*surface));return false;}
    return qlcg_farm_surface_plot(surface,&plot);
}

static void local_position(const QlcgFarmSurface *surface,float x,float z,float *rx,float *rz) {
    float dx=x-surface->plot.x,dz=z-surface->plot.z;
    *rx=dx*surface->cosine+dz*surface->sine;*rz=-dx*surface->sine+dz*surface->cosine;
}

bool qlcg_farm_surface_inside(const QlcgFarmSurface *surface,float x,float z,float padding) {
    if(!surface||!surface->type)return false;
    float rx,rz;local_position(surface,x,z,&rx,&rz);
    return fabsf(rx)<surface->plot.width*.5f+padding&&fabsf(rz)<surface->plot.depth*.5f+padding;
}

void qlcg_farm_surface_sample(const QlcgFarmSurface *surface,float x,float z,float ground,float water,uint8_t farm[4]) {
    memset(farm,0,4);
    if(!surface||!surface->type||!qlcg_farm_surface_dry(ground,water))return;
    float rx,rz;local_position(surface,x,z,&rx,&rz);
    float edge=fminf(surface->plot.width*.5f-fabsf(rx),surface->plot.depth*.5f-fabsf(rz));
    float mask=qlcg_smoothf(0,1,edge);
    if(mask<=0)return;
    float ridge=.5f+.5f*cosf(rx/surface->type->ridge_period*6.28318530718f);
    float shade=1-.28f*surface->type->ridge_strength*ridge;
    farm[0]=(uint8_t)(mask*255+.5f);
    for(int i=0;i<3;i++)farm[i+1]=(uint8_t)(((surface->type->soil_rgb>>(16-i*8))&255)*shade+.5f);
}

void qlcg_farm_surface_color(const uint8_t farm[4],uint8_t color[3]) {
    if(!farm[0])return;
    for(int i=0;i<3;i++)color[i]=(uint8_t)((color[i]*(255-farm[0])+farm[i+1]*farm[0]+127)/255);
}
