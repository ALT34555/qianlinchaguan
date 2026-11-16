/* 河线共用空间分桶 */
#include "hydrology_vectors.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned hash(int x,int z) { return ((unsigned)x*73856093u^(unsigned)z*19349663u)&511; }
int qlcg_river_index_build(RiverVectorIndex *c,const RiverVectorPoint *points,int count,double extra) {
    c->count=0;
    if (c->bin_size<=0) c->bin_size=512;
    for(int i=0;i<512;i++) c->bins[i]=-1;
    for(int k=0;k<count-1;k++) {
        const RiverVectorPoint *a=points+k,*b=a+1;
        double pad=fmax(160,fmax(a->width,b->width)*2+extra);
        int x0=(int)floor((fmin(a->x,b->x)-pad)/c->bin_size),x1=(int)floor((fmax(a->x,b->x)+pad)/c->bin_size);
        int z0=(int)floor((fmin(a->z,b->z)-pad)/c->bin_size),z1=(int)floor((fmax(a->z,b->z)+pad)/c->bin_size);
        for(int z=z0;z<=z1;z++) for(int x=x0;x<=x1;x++) {
            if(c->count==c->capacity) {
                int cap=c->capacity?c->capacity*2:512;
                RiverBinEntry *p=realloc(c->entries,(size_t)cap*sizeof(*p));
                if(!p) return 0;
                c->entries=p; c->capacity=cap;
            }
            unsigned h=hash(x,z); int id=c->count++;
            c->entries[id]=(RiverBinEntry){x,z,k,c->bins[h]}; c->bins[h]=id;
        }
    }
    return 1;
}
int qlcg_river_index_nearest(const RiverVectorIndex *c,const RiverVectorPoint *points,
                              double x,double z,double *distance,double *t) {
    if (c->bin_size<=0) { *distance=INFINITY; *t=0; return -1; }
    int bx=(int)floor(x/c->bin_size),bz=(int)floor(z/c->bin_size),near=-1;
    *distance=INFINITY; *t=0;
    for(int id=c->bins[hash(bx,bz)];id>=0;id=c->entries[id].next) {
        const RiverBinEntry *e=c->entries+id; if(e->x!=bx||e->z!=bz) continue;
        const RiverVectorPoint *a=points+e->segment,*b=a+1;
        double vx=b->x-a->x,vz=b->z-a->z,len2=vx*vx+vz*vz;
        if(!(len2>0)) continue;
        double u=fmax(0,fmin(1,((x-a->x)*vx+(z-a->z)*vz)/len2));
        double d=hypot(x-a->x-vx*u,z-a->z-vz*u);
        if(d<*distance||(d==*distance&&e->segment<near)) { *distance=d; *t=u; near=e->segment; }
    }
    return near;
}
int qlcg_river_index_height(const RiverVectorIndex *c,const RiverVectorPoint *points,const float *heights,
                           double x,double z,double smoothing,double reach,double *height,double *distance) {
    *height=0;*distance=INFINITY;
    if(c->bin_size<=0||!heights||reach<=0||smoothing<=0)return 0;
    int bx=(int)floor(x/c->bin_size),bz=(int)floor(z/c->bin_size);double sum=0,weight=0,nearest=INFINITY;
    for(int id=c->bins[hash(bx,bz)];id>=0;id=c->entries[id].next) {
        const RiverBinEntry *e=c->entries+id;if(e->x!=bx||e->z!=bz)continue;
        const RiverVectorPoint *a=points+e->segment,*b=a+1;
        double vx=b->x-a->x,vz=b->z-a->z,len2=vx*vx+vz*vz;if(!(len2>0))continue;
        double u=fmax(0,fmin(1,((x-a->x)*vx+(z-a->z)*vz)/len2));
        double dx=x-a->x-vx*u,dz=z-a->z-vz*u,d2=dx*dx+dz*dz;nearest=fmin(nearest,d2);
        if(d2>=reach*reach)continue;
        double mx=x-(a->x+b->x)*.5,mz=z-(a->z+b->z)*.5;
        double denominator=mx*mx+mz*mz+smoothing*smoothing,fade=1-d2/(reach*reach);
        double w=(b->arc-a->arc)*fade*fade/(denominator*denominator);
        sum+=(heights[e->segment]+(heights[e->segment+1]-heights[e->segment])*u)*w;weight+=w;
    }
    if(!(weight>0))return 0;
    *height=sum/weight;*distance=sqrt(nearest);return 1;
}
int qlcg_river_index_level(const RiverVectorIndex *c,const RiverVectorPoint *points,
                          double x,double z,double smoothing,double reach,double *level,double *distance) {
    if(level) *level=0; if(distance) *distance=INFINITY;
    if(!c||!points||!level||c->bin_size<=0||reach<=0||smoothing<=0) return 0;
    int bx=(int)floor(x/c->bin_size),bz=(int)floor(z/c->bin_size);
    double sum=0,weight=0,nearest=INFINITY,r2=reach*reach,s2=smoothing*smoothing;
    for(int id=c->bins[hash(bx,bz)];id>=0;id=c->entries[id].next) {
        const RiverBinEntry *e=c->entries+id; if(e->x!=bx||e->z!=bz) continue;
        const RiverVectorPoint *a=points+e->segment,*b=a+1;
        if(!isfinite(a->level)||!isfinite(b->level)) continue;
        double vx=b->x-a->x,vz=b->z-a->z,len2=vx*vx+vz*vz; if(!(len2>0)) continue;
        double u=fmax(0,fmin(1,((x-a->x)*vx+(z-a->z)*vz)/len2));
        double dx=x-a->x-vx*u,dz=z-a->z-vz*u,d2=dx*dx+dz*dz; nearest=fmin(nearest,d2);
        if(d2>=r2) continue;
        double mx=x-(a->x+b->x)*.5,mz=z-(a->z+b->z)*.5;
        double denominator=mx*mx+mz*mz+s2,fade=1-d2/r2,fade2=fade*fade;
        double arc_len=b->arc>a->arc?b->arc-a->arc:sqrt(len2);
        double w=arc_len*fade2*fade2/(denominator*denominator);
        sum+=(a->level+(b->level-a->level)*u)*w; weight+=w;
    }
    if(!(weight>0)) return 0;
    *level=sum/weight; if(distance) *distance=sqrt(nearest); return 1;
}
void qlcg_river_index_free(RiverVectorIndex *c) { free(c->entries); memset(c,0,sizeof(*c)); }
