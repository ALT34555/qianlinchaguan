/* 工程增量按空间分桶，保持应用顺序 */
#include "surface_internal.h"
#include "world_internal.h"
#include "surface_macro.h"
#include "../../core/math_util.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>

#define DELTA_BIN_M 256.0
#define DELTA_BUCKETS 4096
#define DELTA_RECORD_BINS 16

typedef struct { int x,z,record,next; } DeltaBin;
typedef struct { int x0,z0,x1,z1; } DeltaBounds;
struct SurfaceDeltaIndex {
    uint32_t version;
    int count,capacity,node_count,global_count;
    bool periodic;
    int buckets[DELTA_BUCKETS],global[MAX_MACRO_DELTAS];
    DeltaBin *nodes;
};

static unsigned delta_hash(int x,int z) {
    return ((uint32_t)x*73856093u^(uint32_t)z*19349663u)&(DELTA_BUCKETS-1);
}

static int bin_coord(double v,int *out) {
    double k=floor(v/DELTA_BIN_M);
    if(!isfinite(k)||k<=INT_MIN||k>=INT_MAX) return 0;
    *out=(int)k;
    return 1;
}

static int delta_bounds(const MacroWorld *w,const SparseDeltaRecord *d,bool periodic,DeltaBounds *b) {
    float r=d->type==ENG_TOOL_RIVER_DIVERT?d->width_m*1.4f:d->radius;
    float ex=d->type==ENG_TOOL_RIVER_DIVERT?d->ex:d->wx;
    float ez=d->type==ENG_TOOL_RIVER_DIVERT?d->ez:d->wz;
    if(!isfinite(d->wx)||!isfinite(d->wz)||!isfinite(ex)||!isfinite(ez)||!isfinite(r)||r<0) return 0;
    float x0=nextafterf(fminf(d->wx,ex)-r,-INFINITY),x1=nextafterf(fmaxf(d->wx,ex)+r,INFINITY);
    float z0=nextafterf(fminf(d->wz,ez)-r,-INFINITY),z1=nextafterf(fmaxf(d->wz,ez)+r,INFINITY);
    if (periodic && (x0< -w->circumference*.5f || x1>w->circumference*.5f ||
        fabsf(ex-d->wx)>w->circumference*.5f)) return 0;
    if(!bin_coord(x0,&b->x0)||!bin_coord(x1,&b->x1)||
       !bin_coord(z0,&b->z0)||!bin_coord(z1,&b->z1)) return 0;
    double nx=(double)b->x1-b->x0+1,nz=(double)b->z1-b->z0+1;
    return nx*nz<=DELTA_RECORD_BINS;
}

static int build_index(const MacroWorld *w,struct SurfaceDeltaIndex *c) {
    for(int i=0;i<DELTA_BUCKETS;i++) c->buckets[i]=-1;
    c->node_count=c->global_count=0;
    c->periodic=false;
    for(int i=w->delta_count-1;i>=0;i--) {
        if (w->delta_origin[i]==QLCG_DELTA_PROJECTED && w->deltas[i].type!=ENG_TOOL_RIVER_DIVERT) continue;
        bool periodic=w->delta_origin[i]!=QLCG_DELTA_LOCAL && w->circumference>0 && !qlcg_world_flat(w);
        c->periodic|=periodic;
        DeltaBounds b;
        if(!delta_bounds(w,w->deltas+i,periodic,&b)) { c->global[c->global_count++]=i; continue; }
        for(int z=b.z0;z<=b.z1;z++) for(int x=b.x0;x<=b.x1;x++) {
            if(c->node_count==c->capacity) {
                int capacity=c->capacity?c->capacity*2:256;
                DeltaBin *nodes=realloc(c->nodes,(size_t)capacity*sizeof(*nodes));
                if(!nodes) return 0;
                c->nodes=nodes; c->capacity=capacity;
            }
            unsigned bucket=delta_hash(x,z);
            int at=c->node_count++;
            c->nodes[at]=(DeltaBin){x,z,i,c->buckets[bucket]}; c->buckets[bucket]=at;
        }
    }
    c->count=w->delta_count; c->version=w->delta_version;
    return 1;
}

void qlcg_surface_deltas_free(MacroWorld *w) {
    if(!w->delta_index) return;
    free(w->delta_index->nodes); free(w->delta_index); w->delta_index=NULL;
}

bool qlcg_surface_deltas_prepare(MacroWorld *w) {
    struct SurfaceDeltaIndex *c=w->delta_index;
    if (w->delta_count<=16 || (c&&c->count==w->delta_count&&c->version==w->delta_version)) return true;
    if(!c) c=calloc(1,sizeof(*c));
    if(!c) return false;
    w->delta_index=c;
    if(!build_index(w,c)) { qlcg_surface_deltas_free(w); return false; }
    return true;
}

static int next_bin(const struct SurfaceDeltaIndex *c,int at,int x,int z) {
    while(at>=0&&(c->nodes[at].x!=x||c->nodes[at].z!=z)) at=c->nodes[at].next;
    return at;
}

static float coarse_river_cut(const MacroWorld *w,const SparseDeltaRecord *d,float x,float z) {
    SurfaceMacroContext c=qlcg_macro_context(w,x,z);
    float a=qlcg_eng_macro_river_cut(w,d,c.a), b=qlcg_eng_macro_river_cut(w,d,c.b);
    float e=qlcg_eng_macro_river_cut(w,d,c.c), f=qlcg_eng_macro_river_cut(w,d,c.d);
    return qlcg_lerpf(qlcg_lerpf(a,b,(float)c.tx),qlcg_lerpf(e,f,(float)c.tx),(float)c.tz);
}

static void apply_delta(const MacroWorld *w,int i,float wx,float wz,
                         float *h,float *wl,float *wet,float *desert,float *forest) {
    uint8_t origin=w->delta_origin[i];
    const SparseDeltaRecord *dr=w->deltas+i;
    bool macro=origin!=QLCG_DELTA_LOCAL;
    if (dr->type == ENG_TOOL_RIVER_DIVERT) {
        float d=macro?qlcg_eng_macro_river_distance(w,dr,wx,wz):
            dist_to_seg2d(wx,wz,dr->wx,dr->wz,dr->ex,dr->ez,NULL);
        if (d < dr->width_m * 1.4f) {
            float k = 1.0f - d / (dr->width_m * 1.4f);
            float coarse=origin==QLCG_DELTA_PROJECTED?coarse_river_cut(w,dr,wx,wz):0;
            float cut=fmaxf(0,k*k*fabsf(dr->delta_h)-coarse);
            *h -= cut;
            *wl = qlcg_maxf(*wl, *h + 2.8f * k);
            *wet = qlcg_clampf(*wet + 0.42f*fmaxf(0,k-coarse/26), 0.0f, 1.0f);
            *desert = qlcg_clampf(*desert - 0.50f * k, 0.0f, 1.0f);
        }
    } else if (origin!=QLCG_DELTA_PROJECTED) {
        float dx=macro?qlcg_macro_delta_x(w,wx,dr->wx):wx-dr->wx;
        float d = hypotf(dx, wz - dr->wz);
        if (d < dr->radius) {
            float k = 1.0f - d / dr->radius;
            float sm = k * k * (3.0f - 2.0f * k);
            if (dr->type == ENG_TOOL_AFFOREST) {
                *forest = qlcg_clampf(*forest + (macro?.75f:.72f) * sm, 0.0f, 1.0f);
                *desert = qlcg_clampf(*desert - 0.65f * sm, 0.0f, 1.0f);
                *wet = qlcg_clampf(*wet + 0.25f * sm, 0.0f, 1.0f);
            } else if (dr->type == ENG_TOOL_RECLAIM_SEA) {
                if (*h < 12.0f) *h = qlcg_lerpf(*h, macro?14.0f:8.5f, sm);
                *wl = qlcg_minf(*wl, macro?*h:0.0f);
            } else if (dr->type == ENG_TOOL_DIG_LAKE) {
                *h += dr->delta_h * sm;
                *wl = qlcg_maxf(*wl, *h + (macro?18.0f:4.5f) * sm);
                *wet = qlcg_clampf(*wet + 0.45f * sm, 0.0f, 1.0f);
            } else if (dr->type == ENG_TOOL_FLATTEN) {
                *h = qlcg_lerpf(*h, macro?28.0f:dr->delta_h, sm * (macro?.75f:.85f));
            } else {
                *h += dr->delta_h * sm;
                if (macro && dr->type==ENG_TOOL_DEMOLISH)
                    *forest=qlcg_clampf(*forest-.6f*sm,0,1);
            }
        }
    }
}

void qlcg_surface_apply_deltas(const MacroWorld *w,float wx,float wz,
                                float *h,float *wl,float *wet,float *desert,float *forest) {
    if(!w->delta_count) return;
    int x,z;
    const struct SurfaceDeltaIndex *c=w->delta_index;
    if(!c || c->count!=w->delta_count || c->version!=w->delta_version ||
       (c->periodic && fabsf(wx)>w->circumference*.5f) || !bin_coord(wx,&x) || !bin_coord(wz,&z)) {
        for(int i=0;i<w->delta_count;i++) apply_delta(w,i,wx,wz,h,wl,wet,desert,forest);
        return;
    }
    int at=next_bin(c,c->buckets[delta_hash(x,z)],x,z),global=c->global_count;
    while(at>=0||global>0) {
        int local=at>=0?c->nodes[at].record:MAX_MACRO_DELTAS;
        int broad=global>0?c->global[global-1]:MAX_MACRO_DELTAS;
        int record=local<broad?local:broad;
        apply_delta(w,record,wx,wz,h,wl,wet,desert,forest);
        if(local<broad) at=next_bin(c,c->nodes[at].next,x,z);
        else global--;
    }
}
