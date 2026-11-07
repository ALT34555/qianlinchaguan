#include "planet_macro.h"
#include "surface_internal.h"
#include "surface_stream.h"
#include "ice_runtime.h"
#include "../../core/math_util.h"
#include <math.h>
#include <string.h>

#define ICE_SHELL_VERTS 16

#define ICE_EPS 1e-5f

typedef struct {
    float x, z, top, bottom, coverage, depth, strength;
} IceVertex;

typedef struct {
    IceVertex v[ICE_SHELL_VERTS];
    int count;
} IcePolygon;

enum { CLIP_COVER, CLIP_DEPTH, CLIP_X0, CLIP_X1, CLIP_Z0, CLIP_Z1,
       CLIP_FEET, CLIP_TOP, CLIP_HEAD, CLIP_CEILING };

static float distance_to(IceVertex v, int plane, float value) {
    switch (plane) {
        case CLIP_COVER: return v.coverage-.5f;
        case CLIP_DEPTH: return v.depth-.02f;
        case CLIP_X0: return v.x-value;
        case CLIP_X1: return value-v.x;
        case CLIP_Z0: return v.z-value;
        case CLIP_Z1: return value-v.z;
        case CLIP_FEET: return value-v.bottom+ICE_EPS;
        case CLIP_TOP: return v.top-value-ICE_EPS;
        case CLIP_HEAD: return value-v.bottom-ICE_EPS;
        default: return v.bottom-value+ICE_EPS;
    }
}

static IceVertex interpolate(IceVertex a, IceVertex b, float t) {
    IceVertex v;
#define LERP(field) v.field=a.field+(b.field-a.field)*t
    LERP(x); LERP(z); LERP(top); LERP(bottom); LERP(coverage); LERP(depth); LERP(strength);
#undef LERP
    return v;
}

/* 复用原三角面做局部裁剪 */
static void clip(IcePolygon *p, int plane, float value) {
    IcePolygon out={0};
    for (int i=0;i<p->count;i++) {
        IceVertex a=p->v[i], b=p->v[(i+1)%p->count];
        float da=distance_to(a,plane,value), db=distance_to(b,plane,value);
        if (da>=0) out.v[out.count++]=a;
        if ((da>=0)!=(db>=0)) out.v[out.count++]=interpolate(a,b,da/(da-db));
    }
    *p=out;
}

static bool has_area(const IcePolygon *p) {
    if (p->count<3) return false;
    double area=0;
    IceVertex a=p->v[0];
    for (int i=1;i<p->count-1;i++) {
        IceVertex b=p->v[i], c=p->v[i+1];
        area+=(double)(b.x-a.x)*(c.z-a.z)-(double)(c.x-a.x)*(b.z-a.z);
    }
    return fabs(area)>1e-10;
}

static double area_of(const IcePolygon *p) {
    if (p->count < 3) return 0.0;
    double area = 0;
    const IceVertex *a = &p->v[0];
    for (int i = 1; i + 1 < p->count; i++) {
        const IceVertex *b = &p->v[i], *c = &p->v[i + 1];
        area += (double)(b->x - a->x) * (c->z - a->z) - (double)(c->x - a->x) * (b->z - a->z);
    }
    return fabs(area) * 0.5;
}

typedef struct {
    int mode;
    float feet, head, result, top, bottom;
    bool blocked;
    /* 接触收集 */
    IceContact *cells;
    int cell_count, cell_cap;
    int cur_cx, cur_cz, cur_ix, cur_iz;
    float cur_wx, cur_wz;
    float cur_area, cur_strength, cur_top, cur_bottom, cur_depth;
} IceQuery;

static void visit_polygon(IcePolygon p, IceQuery *q) {
    if (!has_area(&p)) return;
    for (int i=0;i<p.count;i++) {
        q->top=fmaxf(q->top,p.v[i].top);
        q->bottom=fminf(q->bottom,p.v[i].bottom);
    }
    if (q->mode==0) {
        clip(&p,CLIP_FEET,q->feet);
        for (int i=0;i<p.count;i++) q->result=fmaxf(q->result,p.v[i].top);
    } else if (q->mode==1) {
        clip(&p,CLIP_CEILING,q->head);
        for (int i=0;i<p.count;i++) q->result=fminf(q->result,p.v[i].bottom);
    } else if (q->mode==2) {
        clip(&p,CLIP_TOP,q->feet); clip(&p,CLIP_HEAD,q->head);
        if (has_area(&p)) q->blocked=true;
    } else {
        double a = area_of(&p);
        if (a <= 1e-6) return;
        q->cur_area += (float)a;
        for (int i=0;i<p.count;i++) {
            if (p.v[i].strength < q->cur_strength) q->cur_strength = p.v[i].strength;
            if (p.v[i].depth < q->cur_depth) q->cur_depth = p.v[i].depth;
            if (p.v[i].top > q->cur_top) q->cur_top = p.v[i].top;
            if (p.v[i].bottom < q->cur_bottom) q->cur_bottom = p.v[i].bottom;
        }
    }
}

static void flush_contact(IceQuery *q) {
    if (!q->cells || q->cell_count >= q->cell_cap) return;
    if (q->cur_area <= 1e-4f) return;   /* 与 has_area 同量级：细长重叠也要算接触 */
    IceContact *k = &q->cells[q->cell_count++];
    k->cx = q->cur_cx;
    k->cz = q->cur_cz;
    k->ix = q->cur_ix;
    k->iz = q->cur_iz;
    k->wx = q->cur_wx;
    k->wz = q->cur_wz;
    k->area = q->cur_area;
    k->strength = q->cur_strength;
    k->top = q->cur_top;
    k->thickness = qlcg_maxf(0.0f, q->cur_top - q->cur_bottom);
    k->quality = 1.0f;
    k->depth = q->cur_depth;
}

static void visit_chunk(const RegionData *c, float x, float z, float half, IceQuery *q,
                        float world_x, float world_z) {
    if (!c->ice_bottom || !c->ice_coverage || !c->water_levels || c->water_form==WATER_FORM_CUTOFF) return;
    float span=(c->width-1)*c->step, origin=-span*.5f;
    /* 外围样点不属于可见网格 */
    int border=c->width==QLCG_CHUNK_GRID ? 1 : 0;
    int x0=qlcg_clampi((int)floorf((x-half-origin)/c->step),border,c->width-2-border);
    int x1=qlcg_clampi((int)floorf((x+half-origin)/c->step),border,c->width-2-border);
    int z0=qlcg_clampi((int)floorf((z-half-origin)/c->step),border,c->height-2-border);
    int z1=qlcg_clampi((int)floorf((z+half-origin)/c->step),border,c->height-2-border);
    for (int iz=z0;iz<=z1;iz++) for (int ix=x0;ix<=x1;ix++) {
        /* 洞是单元属性：整格不再参与任何查询 */
        if (c->ice && qlcg_ice_cell_open(c->ice, ix-1, iz-1)) continue;
        int ids[]={iz*c->width+ix,iz*c->width+ix+1,(iz+1)*c->width+ix,(iz+1)*c->width+ix+1};
        int wet=0; bool frozen=false;
        for (int i=0;i<4;i++) {
            wet+=c->water_levels[ids[i]]>c->heights[ids[i]]+.02f;
            frozen=frozen || c->ice_coverage[ids[i]]>=.5f;
        }
        if (wet<2 || !frozen) continue;
        IceVertex v[4];
        for (int i=0;i<4;i++) {
            int vx=ix+(i&1), vz=iz+(i>>1), j=ids[i];
            float top=qlcg_chunk_water_vertex(c,vx,vz);
            float bottom=c->ice_bottom[j]>-1e8f ? c->ice_bottom[j] : top;
            float noise = c->ice ? (float)c->ice->noise[j]/255.0f : .5f;
            float h = c->ice ? c->ice->h[j] : top-bottom;
            float qy = c->ice ? c->ice->q[j] : 1.0f;
            v[i]=(IceVertex){origin+vx*c->step,origin+vz*c->step,top,
                fminf(top,fmaxf(c->heights[j],bottom)),c->ice_coverage[j],top-c->heights[j],
                qlcg_ice_load_capacity(h,qy,noise)};
        }
        static const int triangles[2][3]={{0,2,1},{1,2,3}};
        if (q->mode == 3) {
            q->cur_area = 0.0f;
            q->cur_depth = INFINITY;
            q->cur_strength = INFINITY;
            q->cur_top = -INFINITY;
            q->cur_bottom = INFINITY;
            q->cur_ix = ix - 1;
            q->cur_iz = iz - 1;
            q->cur_wx = world_x + origin + ((float)ix + .5f) * c->step;
            q->cur_wz = world_z + origin + ((float)iz + .5f) * c->step;
            q->cur_cx = (int)floorf(q->cur_wx / (float)CHUNK_SIZE);
            q->cur_cz = (int)floorf(q->cur_wz / (float)CHUNK_SIZE);
        }
        for (int t=0;t<2;t++) {
            IcePolygon p={0}; p.count=3;
            for (int i=0;i<3;i++) p.v[i]=v[triangles[t][i]];
            clip(&p,CLIP_COVER,0); clip(&p,CLIP_DEPTH,0);
            clip(&p,CLIP_X0,x-half); clip(&p,CLIP_X1,x+half);
            clip(&p,CLIP_Z0,z-half); clip(&p,CLIP_Z1,z+half);
            visit_polygon(p,q);
        }
        if (q->mode == 3) {
            if (q->cur_strength == INFINITY) q->cur_strength = 0.0f;
            flush_contact(q);
        }
    }
}

static IceQuery query(const RegionData *reg, float x, float z, float half, float feet, float head,
                      int mode, IceContact *cells, int cap) {
    IceQuery q;
    memset(&q, 0, sizeof(q));
    q.mode = mode;
    q.feet = feet;
    q.head = head;
    q.result = mode==1 ? INFINITY : -INFINITY;
    q.top = -INFINITY;
    q.bottom = INFINITY;
    q.cells = cells;
    q.cell_cap = cap;
    if (!reg || half<=0) return q;
    if (!reg->stream) { visit_chunk(reg,x,z,half,&q,reg->center_x,reg->center_z); return q; }
    int cx0=(int)floorf((x-half+CHUNK_SIZE*.5f)/CHUNK_SIZE);
    int cx1=(int)floorf((x+half+CHUNK_SIZE*.5f)/CHUNK_SIZE);
    int cz0=(int)floorf((z-half+CHUNK_SIZE*.5f)/CHUNK_SIZE);
    int cz1=(int)floorf((z+half+CHUNK_SIZE*.5f)/CHUNK_SIZE);
    for (int dz=cz0;dz<=cz1;dz++) for (int dx=cx0;dx<=cx1;dx++) {
        float lx,lz;
        const RegionData *c=qlcg_region_chunk_at(reg,dx*CHUNK_SIZE,dz*CHUNK_SIZE,&lx,&lz);
        if (c) visit_chunk(c,x-dx*CHUNK_SIZE,z-dz*CHUNK_SIZE,half,&q,reg->center_x,reg->center_z);
    }
    return q;
}

float qlcg_region_ice_ground(const RegionData *r,float x,float z,float half,float feet) {
    return query(r,x,z,half,feet,0,0,NULL,0).result;
}
float qlcg_region_ice_ceiling(const RegionData *r,float x,float z,float half,float head) {
    return query(r,x,z,half,0,head,1,NULL,0).result;
}
bool qlcg_region_ice_blocked(const RegionData *r,float x,float y,float z,float height,float half) {
    return query(r,x,z,half,y,y+height,2,NULL,0).blocked;
}
float qlcg_region_ice_resolve(const RegionData *r,float x,float y,float z,float height,float half) {
    IceQuery q=query(r,x,z,half,y,y+height,2,NULL,0);
    if (!q.blocked) return NAN;
    float above=q.top+ICE_EPS, below=q.bottom-height-ICE_EPS;
    float bed=qlcg_region_height_at(r,x,z);
    return below>=bed && fabsf(y-below)<fabsf(y-above) ? below : above;
}

/* 足迹接触集合：只读，不得扣疲劳或破冰 */
int qlcg_region_ice_contacts(const RegionData *reg, float x, float z, float half, float feet,
                             IceContact *out, int cap) {
    if (!out || cap <= 0) return 0;
    return query(reg, x, z, half, feet, feet, 3, out, cap).cell_count;
}
