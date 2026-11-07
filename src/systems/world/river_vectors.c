/* 河线共用空间分桶 */
#include "river_vectors.h"
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
void qlcg_river_index_free(RiverVectorIndex *c) { free(c->entries); memset(c,0,sizeof(*c)); }
