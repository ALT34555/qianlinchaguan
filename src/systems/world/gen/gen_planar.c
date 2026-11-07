#include "gen_tectonics.h"
#include "gen_plates.h"
#include "gen_noise.h"
#include "../../../core/math_util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float x,z,vx,vz,angle,spin,a,b;
} PlanePlate;

static float random01(uint32_t *seed) {
    *seed=qlcg_hash_u32(*seed+0x9e3779b9u);
    return (*seed>>8)/16777216.0f;
}

static float noise(float x,float z,float scale,uint32_t seed) {
    return qlcg_base_noise(x*scale,3.17f,z*scale,seed);
}

static float crust(const PlanePlate *p,float x,float z,float t,uint32_t seed) {
    float angle=p->angle+p->spin*t,c=cosf(angle),s=sinf(angle);
    float dx=x-p->x-p->vx*t,dz=z-p->z-p->vz*t;
    float u=(dx*c+dz*s)/p->a,v=(-dx*s+dz*c)/p->b;
    float distance=hypotf(u,v),edge=qlcg_clampf((1.2f-distance)/.4f,0,1);
    float warp=edge*(.28f*noise(u,v,2.7f,seed)+.10f*noise(u,v,7.3f,seed^0x7251u));
    return 1-distance+warp;
}

void qlcg_gen_planar(MacroWorld *w) {
    uint32_t seed=w->opt.seed,rng=seed^0x706c616eu;
    int major=2+(int)(qlcg_hash_u32(seed^0x6d616a72u)%4);
    int minor=2+(int)(qlcg_hash_u32(seed^0x6d696e72u)%4);
    int count=major+minor,n=w->cell_count;
    PlanePlate plate[MAX_PLATES]={0};
    DriftProfile drift=qlcg_drift_profile(w->opt.tectonic_activity);
    float phase=random01(&rng)*QLCG_TAU;
    const float radii[4]={.38f,.44f,.48f,.50f};
    const float axes[4]={.64f,.49f,.41f,.36f};
    w->plate_count=count;
    for(int k=0;k<count;k++) {
        PlanePlate *p=&plate[k];
        float a=phase+QLCG_TAU*(k<major?k+.15f*(random01(&rng)-.5f):k-major+.5f)/(k<major?major:minor);
        float radius=k<major?radii[major-2]+.025f*(random01(&rng)-.5f):.15f+.42f*random01(&rng);
        p->x=cosf(a)*radius;p->z=sinf(a)*radius;
        float v=random01(&rng)*QLCG_TAU;
        p->vx=cosf(v)*.12f;p->vz=sinf(v)*.12f;
        p->angle=a+QLCG_TAU*.25f+.15f*(random01(&rng)-.5f);p->spin=(random01(&rng)-.5f)*.3f;
        p->a=k<major?axes[major-2]*(.97f+.06f*random01(&rng)):.14f+.06f*random01(&rng);
        p->b=k<major?.40f+.02f*random01(&rng):.10f+.05f*random01(&rng);
        TectonicPlate *out=&w->plates[k];
        memset(out,0,sizeof(*out));out->id=k;out->is_minor=k>=major;
        out->start[0]=p->x;out->start[2]=p->z;
        out->axis[0]=p->vx;out->axis[2]=p->vz;out->speed=hypotf(p->vx,p->vz);
        out->radius=p->a;out->kind=out->is_minor;out->oceanic=out->is_minor;
    }
    float *memory=calloc((size_t)n*3,sizeof(float));
    if(!memory) return;
    float *up=memory,*rift=up+n,*shear=rift+n;
    int steps=(int)roundf(drift.steps);
    for(int step=0;step<=steps;step++) {
        float t=drift.span*step/steps;
        for(int k=0;k<count;k++) {
            w->plates[k].center[0]=plate[k].x+plate[k].vx*t;
            w->plates[k].center[2]=plate[k].z+plate[k].vz*t;
        }
        for(int i=0;i<n;i++) {
            float x=w->points[i*3],z=w->points[i*3+2];
            float wx=x+.035f*noise(x,z,5,seed),wz=z+.035f*noise(x,z,5,seed^311u);
            float best=1e9f;int id=0;
            for(int k=0;k<count;k++) {
                float dx=wx-w->plates[k].center[0],dz=wz-w->plates[k].center[2];
                float score=dx*dx+dz*dz;
                if(score<best) { best=score;id=k; }
            }
            w->plate_id[i]=(uint16_t)id;
        }
        if(step==0) continue;
        for(int i=0;i<n;i++) for(int d=0;d<4;d+=2) {
            int j=w->neighbors[i*8+d],a=w->plate_id[i],b=w->plate_id[j];
            if(a==b) continue;
            float dx=w->plates[b].center[0]-w->plates[a].center[0];
            float dz=w->plates[b].center[2]-w->plates[a].center[2];
            float len=fmaxf(.001f,hypotf(dx,dz));dx/=len;dz/=len;
            float vx=(plate[a].vx-plate[b].vx)*6*drift.speed;
            float vz=(plate[a].vz-plate[b].vz)*6*drift.speed;
            float push=vx*dx+vz*dz,slide=fabsf(vx*dz-vz*dx);
            up[i]=fmaxf(up[i],fmaxf(0,push));up[j]=fmaxf(up[j],fmaxf(0,push));
            rift[i]=fmaxf(rift[i],fmaxf(0,-push));rift[j]=fmaxf(rift[j],fmaxf(0,-push));
            shear[i]=fmaxf(shear[i],slide);shear[j]=fmaxf(shear[j],slide);
            if(step==steps) {
                w->boundary[i]=w->boundary[j]=push>.12f?1:push<-.12f?2:3;
                w->ridge_push_e[i]=-dx*push;w->ridge_push_n[i]=dz*push;
                w->ridge_push_e[j]=dx*push;w->ridge_push_n[j]=-dz*push;
            }
        }
    }
    float belt=w->circumference*drift.belt_width*.12f;
    qlcg_spread_tectonic(w,up,belt,w->uplift);
    qlcg_spread_tectonic(w,rift,belt*.6f,w->rift);
    qlcg_spread_tectonic(w,shear,belt*.4f,w->shear);
    bool home=!w->opt.plane_x && !w->opt.plane_z;
    float broad=home?0:qlcg_clampf((w->opt.land_ratio-.35f)/.15f,0,1);
    float shape=2+6*qlcg_clampf((w->opt.land_ratio-.65f)/.15f,0,1);
    for(int i=0;i<n;i++) {
        float x=w->points[i*3],z=w->points[i*3+2],density=-10;
        for(int k=0;k<count;k++) {
            float value=crust(&plate[k],x,z,drift.span,seed+(uint32_t)k*171u);
            density=fmaxf(density,value-(k>=major?.15f:0));
        }
        if(broad>0) {
            float xx=x+.025f*noise(x,z,3,seed^0x62617331u);
            float zz=z+.025f*noise(x,z,3,seed^0x62617332u);
            float basin=powf(powf(fabsf(xx),shape)+powf(fabsf(zz),shape),1/shape)/.94f;
            float crust=1-basin+.018f*noise(x,z,5,seed^0x62617365u);
            density+=(crust-density)*broad;
        }
        float continental=qlcg_clampf((density+.12f)*2,0,1);
        continental+=(qlcg_clampf(density*8,0,1)-continental)*broad;
        float ridge=1-fabsf(noise(x,z,19,seed^871u));ridge*=ridge;
        float h=density*1500+noise(x,z,11,seed^0x62617931u)*180*(1-broad+broad*continental);
        float mountains=w->opt.terrain_recipe==TERRAIN_MOUNTAINS?1.65f:1;
        float valleys=w->opt.terrain_recipe==TERRAIN_RIFT?1.6f:1;
        h+=continental*(w->uplift[i]*drift.uplift_scale*ridge*mountains-w->rift[i]*drift.rift_depth*valleys);
        if(w->opt.terrain_recipe==TERRAIN_ARCHIPELAGO)
            h+=continental*noise(x,z,14,seed^0x61726368u)*420;
        w->height[i]=h;w->basin[i]=qlcg_clampf(w->rift[i],0,1);
    }
    free(memory);
}
