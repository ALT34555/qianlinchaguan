/* 湖体模板、连接段与湖床拼接 */
#include "surface_lakes.h"
#include "surface_macro.h"
#include "surface_frame.h"
#include "gen/gen_noise.h"
#include "gen/gen_height.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <limits.h>
#include "lake_atlas_data.inc"

typedef struct {
    int anchor, min_x, max_x, min_z, max_z, count;
    double cx, cz, a, d;
    int tile;
} LakeBody;
typedef struct {
    int lake, tile;
    double cx, cz, a, b, c, d;
    int port,river;
    double river_x,river_z;
} LakePart;
typedef struct { int part, next; } LakeRef;
struct LakeTemplateCache {
    LakeBody *bodies;
    LakePart *parts;
    LakeRef *refs;
    int *buckets, columns, rows, count, capacity, refs_n, refs_cap, body_cap;
};

static uint32_t hash(uint32_t n) {
    n=(n^(n>>16))*0x7feb352du;
    n=(n^(n>>15))*0x846ca68bu;
    return n^(n>>16);
}
static double smooth(double x) {
    x=fmax(0,fmin(1,x)); return x*x*(3-2*x);
}
static double read_atlas(int tile, double u, double v) {
    if (u<0 || u>1 || v<0 || v>1) return -1;
    double x=u*(LAKE_SIDE-1), z=v*(LAKE_SIDE-1);
    int ix=(int)x, iz=(int)z;
    if (ix>=LAKE_SIDE-1) ix=LAKE_SIDE-2;
    if (iz>=LAKE_SIDE-1) iz=LAKE_SIDE-2;
    double tx=x-ix, tz=z-iz;
    int at=tile*LAKE_SIDE*LAKE_SIDE+iz*LAKE_SIDE+ix;
    double a=lake_atlas[at]+(lake_atlas[at+1]-lake_atlas[at])*tx;
    double b=lake_atlas[at+LAKE_SIDE]+(lake_atlas[at+LAKE_SIDE+1]-lake_atlas[at+LAKE_SIDE])*tx;
    return (a+(b-a)*tz)/8192;
}
static double part_field(const LakePart *p, double x, double z, int columns) {
    double dx=x-p->cx, dz=z-p->cz;
    dx-=round(dx/columns)*columns;
    return read_atlas(p->tile,.5+dx*p->a+dz*p->b,.5+dx*p->c+dz*p->d);
}
int qlcg_lake_channel_target(const MacroWorld *w,int cell,double from_x,double from_z,double *wx,double *wz) {
    if (!w->lake_templates || !w->lake_id || cell<0 || cell>=w->cell_count) return 0;
    int id=w->lake_id[cell]; if (id<0 || id>=w->lake_count) return 0;
    SurfaceFrame frame=qlcg_surface_frame(w,cell);
    SurfacePoint from=qlcg_frame_local(&frame,from_x,from_z);
    double angle=atan2(-from.z,-from.x),span=w->circumference/w->columns*fmax(.01,cos(frame.lat));
    static const double rings[7]={0,.02,.06,.15,.35,.65,.9};
    static const int turns[8]={0,1,-1,2,-2,3,-3,4};
    for (int ring=0;ring<7;ring++) for (int k=0;k<(ring?8:1);k++) {
        double a=angle+turns[k]*.7853981633974483,r=span*rings[ring];
        SurfacePoint p=qlcg_frame_coordinates(&frame,cos(a)*r,sin(a)*r);
        int target=qlcg_macro_cell_at(w,p.x,p.z),dx=target%w->columns-cell%w->columns;
        dx-=(int)round(dx/(double)w->columns)*w->columns;
        if (abs(dx)>1 || abs(target/w->columns-cell/w->columns)>1) continue;
        LakeShoreField f=qlcg_lake_field(w,p.x,p.z);
        if (f.lake && fabs(f.level-w->lakes[id].level)<.02 && f.d<0 && f.bed<0 && f.d*f.bed>.04) {
            *wx=p.x; *wz=p.z; return 1;
        }
    }
    return 0;
}
int qlcg_lake_outflow_at(const MacroWorld *w,int cell) {
    const struct LakeTemplateCache *c=w->lake_templates;
    if(!c||cell<0||cell>=w->cell_count) return -1;
    int x=cell%w->columns,z=cell/w->columns,bucket=(z/4)*c->columns+x/4;
    for(int k=c->buckets[bucket];k>=0;k=c->refs[k].next) {
        int id=c->parts[c->refs[k].part].lake;
        const MacroLake *lake=w->lakes+id;
        if(lake->outlet>=0&&(lake->exit_cell==cell||lake->outlet==cell)) return id;
    }
    return -1;
}
int qlcg_lake_connection(const MacroWorld *w,int cell,int outlet,double *wx,double *wz) {
    const struct LakeTemplateCache *c=w->lake_templates;
    if (!c || cell<0 || cell>=w->cell_count) return 0;
    int bucket=(cell/w->columns/4)*c->columns+(cell%w->columns)/4;
    for (int k=c->buckets[bucket];k>=0;k=c->refs[k].next) {
        const LakePart *p=c->parts+c->refs[k].part;
        if (p->port!=cell+1 || !isfinite(p->river_x)) continue;
        if (outlet && (w->lakes[p->lake].exit_cell!=cell || w->lakes[p->lake].outlet<0)) continue;
        *wx=p->river_x; *wz=p->river_z; return 1;
    }
    return 0;
}
int qlcg_lake_river_candidates(const MacroWorld *w,int cell,int *edges,int capacity) {
    const struct LakeTemplateCache *c=w->lake_templates;
    if (!c || cell<0 || cell>=w->cell_count) return 0;
    int bucket=(cell/w->columns/4)*c->columns+(cell%w->columns)/4,n=0;
    for (int k=c->buckets[bucket];k>=0;k=c->refs[k].next) {
        const LakePart *p=c->parts+c->refs[k].part;
        if (!p->port || !p->river || !isfinite(p->river_x)) continue;
        int owner=p->river-1,duplicate=0;
        for (int j=0;j<n;j++) if (edges[j]==owner) duplicate=1;
        if (!duplicate && n<capacity) edges[n++]=owner;
    }
    return n;
}
int qlcg_lake_overlaps_cell(const MacroWorld *w, int cell) {
    const struct LakeTemplateCache *c=w->lake_templates;
    if (w->water[cell]==2) return 1;
    if (!c) return 0;
    int x=cell%w->columns,z=cell/w->columns;
    int bucket=(z/4)*c->columns+x/4;
    for (int k=c->buckets[bucket];k>=0;k=c->refs[k].next) {
        const LakePart *p=&c->parts[c->refs[k].part];
        if (p->port) continue;
        double dx=x+.5-p->cx,dz=z+.5-p->cz;
        dx-=round(dx/w->columns)*w->columns;
        if (fabs(dx*p->a+dz*p->b)<=.5*(1+fabs(p->a)+fabs(p->b)) &&
            fabs(dx*p->c+dz*p->d)<=.5*(1+fabs(p->c)+fabs(p->d))) return 1;
    }
    return 0;
}
void qlcg_lakes_free(MacroWorld *w) {
    struct LakeTemplateCache *c=w->lake_templates;
    if (!c) return;
    free(c->bodies); free(c->parts); free(c->refs); free(c->buckets); free(c);
    w->lake_templates=NULL;
}
void qlcg_lakes_record(MacroWorld *w, int lake, int cell) {
    struct LakeTemplateCache *c=w->lake_templates;
    if (!c) {
        c=calloc(1,sizeof(*c)); if (!c) return;
        c->body_cap=w->lake_count+1;                   /* 记录期湖数还在长，留足再扩容 */
        c->bodies=calloc((size_t)c->body_cap,sizeof(LakeBody));
        c->columns=(w->columns+3)/4; c->rows=(w->rows+3)/4;
        c->buckets=malloc((size_t)c->columns*c->rows*sizeof(int));
        if (!c->bodies || !c->buckets) {
            free(c->bodies); free(c->buckets); free(c); return;
        }
        for (int i=0;i<c->columns*c->rows;i++) c->buckets[i]=-1;
        w->lake_templates=c;
    } else if (lake>=c->body_cap) {
        int cap=lake+1;
        LakeBody *nb=(LakeBody *)realloc(c->bodies,(size_t)cap*sizeof(LakeBody));
        if (!nb) return;
        memset(nb+c->body_cap,0,(size_t)(cap-c->body_cap)*sizeof(LakeBody));
        c->bodies=nb; c->body_cap=cap;
    }
    LakeBody *b=&c->bodies[lake];
    int x=cell%w->columns, z=cell/w->columns;
    if (!b->count) {
        b->anchor=x; b->min_x=b->max_x=x; b->min_z=b->max_z=z;
    }
    x-=(int)round((x-b->anchor)/(double)w->columns)*w->columns;
    if (x<b->min_x) b->min_x=x;
    if (x>b->max_x) b->max_x=x;
    if (z<b->min_z) b->min_z=z;
    if (z>b->max_z) b->max_z=z;
    b->count++;
}
static void index_part(MacroWorld *w, LakePart p, double min_x, double max_x,
                        double min_z, double max_z) {
    struct LakeTemplateCache *c=w->lake_templates;
    if (c->count==c->capacity) {
        int cap=c->capacity?c->capacity*2:32;
        LakePart *next=realloc(c->parts,(size_t)cap*sizeof(*next));
        if (!next) return;
        c->parts=next; c->capacity=cap;
    }
    int part=c->count++; c->parts[part]=p;
    int y0=(int)fmax(0,floor(min_z/4)), y1=(int)fmin(c->rows-1,floor(max_z/4));
    int x0=(int)floor(min_x/4), x1=(int)floor(max_x/4);
    if (x1-x0>=c->columns) x1=x0+c->columns-1;
    for (int y=y0;y<=y1;y++) for (int x=x0;x<=x1;x++) {
        if (c->refs_n==c->refs_cap) {
            int cap=c->refs_cap?c->refs_cap*2:128;
            LakeRef *next=realloc(c->refs,(size_t)cap*sizeof(*next));
            if (!next) return;
            c->refs=next; c->refs_cap=cap;
        }
        int bucket=y*c->columns+(x%c->columns+c->columns)%c->columns;
        c->refs[c->refs_n]=(LakeRef){part,c->buckets[bucket]};
        c->buckets[bucket]=c->refs_n++;
    }
}
void qlcg_lakes_attach_river(MacroWorld *w,int lake,int cell,int edge) {
    struct LakeTemplateCache *c=w->lake_templates;
    if (!c || lake<0 || lake>=w->lake_count || cell<0) return;
    LakeBody *b=&c->bodies[lake]; if (!b->count) return;
    if (edge<0 && w->lakes[lake].exit_cell==cell && w->lakes[lake].outlet>=0) edge=cell;
    int bucket=(cell/w->columns/4)*c->columns+(cell%w->columns)/4;
    for (int k=c->buckets[bucket];k>=0;k=c->refs[k].next) {
        LakePart *old=c->parts+c->refs[k].part;
        if (old->lake!=lake || old->port!=cell+1) continue;
        if (edge<0 || old->river==edge+1) return;
        if (!old->river) { old->river=edge+1; return; }
        LakePart copy=*old; copy.river=edge+1;
        double ex=cell%w->columns+.5,ez=cell/w->columns+.5;
        double gx=isfinite(copy.river_x)?(copy.river_x/w->circumference+.5)*w->columns:b->cx;
        double gz=isfinite(copy.river_z)?(copy.river_z/w->circumference+qlcg_world_z_offset(w))*w->columns:b->cz;
        gx-=round((gx-ex)/w->columns)*w->columns;
        index_part(w,copy,fmin(ex,gx)-1.25,fmax(ex,gx)+1.25,fmin(ez,gz)-1.25,fmax(ez,gz)+1.25);
        return;
    }
    double ex=cell%w->columns+.5,ez=cell/w->columns+.5;
    double dx=ex-b->cx, dz=ez-b->cz;
    dx-=round(dx/w->columns)*w->columns;
    double cx=ex-dx,cz=b->cz,gx=cx,gz=cz;
    LakePart p={0}; p.lake=lake; p.tile=-1; p.port=cell+1; p.river=edge+1; p.river_x=p.river_z=NAN;
    double previous=0;
    for (int k=0;k<=48;k++) {
        double t=k/32.0,x=ex+(cx-ex)*t,z=ez+(cz-ez)*t;
        double wx=(x/w->columns-.5)*w->circumference,wz=(z/w->columns-qlcg_world_z_offset(w))*w->circumference;
        LakeShoreField f=qlcg_lake_field(w,wx,wz);
        if (f.lake && fabs(f.level-w->lakes[lake].level)<.02 && f.d<0 && f.bed<0 && f.d*f.bed>.25) {
            double lo=previous,hi=t;
            for (int j=0;j<9 && k;j++) {
                double m=(lo+hi)*.5;
                LakeShoreField q=qlcg_lake_field(w,((ex+(cx-ex)*m)/w->columns-.5)*w->circumference,
                    ((ez+(cz-ez)*m)/w->columns-qlcg_world_z_offset(w))*w->circumference);
                if (q.lake && fabs(q.level-w->lakes[lake].level)<.02 && q.d<0 && q.bed<0 && q.d*q.bed>.25) hi=m; else lo=m;
            }
            gx=ex+(cx-ex)*hi; gz=ez+(cz-ez)*hi;
            p.river_x=(gx/w->columns-.5)*w->circumference; p.river_z=(gz/w->columns-qlcg_world_z_offset(w))*w->circumference;
            break;
        }
        previous=t;
    }
    index_part(w,p,fmin(ex,gx)-1.25,fmax(ex,gx)+1.25,fmin(ez,gz)-1.25,fmax(ez,gz)+1.25);
}
void qlcg_lakes_connect(MacroWorld *w,int lake,int cell) {
    qlcg_lakes_attach_river(w,lake,cell,-1);
}
void qlcg_lakes_prepare(MacroWorld *w) {
    struct LakeTemplateCache *c=w->lake_templates; if (!c) return;
    for (int i=0;i<w->lake_count;i++) {
        LakeBody *b=&c->bodies[i]; if (!b->count) continue;
        uint32_t seed=hash((uint32_t)(i+1)*31u^w->opt.seed);
        double width=(b->max_x-b->min_x+1)*1.28+.3;
        double height=(b->max_z-b->min_z+1)*1.28+.3;
        b->cx=(b->min_x+b->max_x+1)*.5; b->cz=(b->min_z+b->max_z+1)*.5;
        b->a=(seed&1?-1:1)/width; b->d=(seed&2?-1:1)/height;
        b->tile=(seed>>8)%LAKE_BODIES;
        LakePart p={i,b->tile,b->cx,b->cz,b->a,0,0,b->d};
        index_part(w,p,b->cx-width/2,b->cx+width/2,b->cz-height/2,b->cz+height/2);
        if (w->lakes[i].outlet>=0) qlcg_lakes_connect(w,i,w->lakes[i].exit_cell);
    }
}
/* base 湖心岛形：椭圆、脊链与冲沟 */
typedef struct { double x, z, rx, rz; } IsleBlob;
typedef struct { double ax, az, bx, bz, w; } IsleCut;
typedef struct {
    int count, ribbon, cuts;
    IsleBlob blobs[7];
    IsleCut channels[2];
} IsleShape;
typedef struct { double mask, dome; } IsleField;
static const IsleShape ISLE_SHAPES[7] = {
    {1,0,0,{{.70,.50,.13,.17}}},
    {2,0,0,{{.29,.36,.12,.15},{.73,.66,.10,.13}}},
    {3,0,0,{{.28,.30,.10,.12},{.72,.34,.12,.10},{.65,.74,.13,.10}}},
    {7,5,0,{{.27,.46,.075,.11},{.32,.34,.09,.09},{.44,.29,.11,.08},{.57,.31,.10,.08},
            {.66,.38,.09,.09},{.72,.48,.065,.10},{.76,.69,.045,.06}}},
    {2,0,0,{{.37,.44,.065,.27},{.58,.73,.035,.06}}},
    {2,0,1,{{.30,.69,.15,.19},{.62,.80,.035,.035}},{{.30,.69,.52,.61,.045}}},
    {7,4,2,{{.25,.34,.09,.12},{.35,.27,.12,.09},{.49,.26,.14,.08},{.61,.30,.12,.09},
            {.70,.38,.09,.12},{.80,.54,.04,.035},{.85,.67,.055,.035}},
           {{.39,.28,.39,.10,.03},{.60,.32,.75,.18,.03}}},
};

/* 菱形度量，对齐 base impactRadius */
static double isle_metric(double x, double z) {
    return .7 * (fabs(x + .2 * z) + fabs(z)) + .3 * hypot(x, z);
}

/* 岛形遮罩与缓丘核，返回 0~1 */
static IsleField isle_field(int shape, double u, double v) {
    const IsleShape *s = &ISLE_SHAPES[shape];
    double mask = 0, dome = 0, r, m;
    for (int i = 0; i < s->count; i++) {
        const IsleBlob *b = &s->blobs[i];
        r = isle_metric((u - b->x) / b->rx, (v - b->z) / b->rz);
        m = 1 - smooth((r - .3) / .7);
        if (m > mask) mask = m;
        if (1 - r > dome) dome = 1 - r;
    }
    for (int k = 1; k <= s->ribbon; k++) {
        const IsleBlob *a = &s->blobs[k - 1], *b = &s->blobs[k];
        double dx = b->x - a->x, dz = b->z - a->z;
        double t = ((u - a->x) * dx + (v - a->z) * dz) / (dx * dx + dz * dz);
        if (t < 0) t = 0; else if (t > 1) t = 1;
        r = hypot(u - a->x - t * dx, v - a->z - t * dz) /
            fmin(fmin(a->rx, a->rz), fmin(b->rx, b->rz));
        m = 1 - smooth((r - .45) / .55);
        if (m > mask) mask = m;
        if (1 - r > dome) dome = 1 - r;
    }
    for (int k = 0; k < s->cuts; k++) {
        const IsleCut *c = &s->channels[k];
        double dx = c->bx - c->ax, dz = c->bz - c->az, len = hypot(dx, dz);
        double along = ((u - c->ax) * dx + (v - c->az) * dz) / len;
        double across = ((u - c->ax) * dz - (v - c->az) * dx) / len;
        double t = along / len;
        if (t < 0) t = 0; else if (t > 1) t = 1;
        r = fabs(across) / (c->w * (.4 + .8 * t));
        r += fmax(0.0, fmax(-along, along - len) / c->w);
        m = smooth((r - .4) / .6);
        mask *= m; dome *= m;
    }
    if (shape >= 3) {
        m = smooth((hypot(u - .5, v - .5) - .06) / .09);
        mask *= m; dome *= m;
    }
    if (dome < 0) dome = 0;
    if (dome > 1) dome = 1;
    return (IsleField){mask, dome};
}

/* 逐格抽签：深水格按活动度定岛形 */
static int isle_pick(const MacroWorld *w, const LakeBody *b, int lake, int activity,
                     double gx, double gz, int *out_shape, uint32_t *out_seed) {
    static const double chances[10] = {.02,.05,.08,.10,.12,.14,.16,.18,.19,.20};
    int columns = w->columns;
    double fx = floor(gx), fz = floor(gz);
    int row = (int)fz;
    if (row < 0) row = 0; else if (row >= w->rows) row = w->rows - 1;
    int cell = row * columns + ((int)fx % columns + columns) % columns;
    uint32_t seed = (uint32_t)(cell + 1) * 374761393u ^ w->opt.seed ^ (uint32_t)lake;
    if (!(hash(seed) / 4294967296.0 < chances[activity])) return 0;
    LakePart body = {lake, b->tile, b->cx, b->cz, b->a, 0, 0, b->d};
    if (part_field(&body, fx + .5, fz + .5, columns) <= .55) return 0;
    if (out_shape) *out_shape = (int)(hash(seed ^ 0x9e3779b9u) % 7u);
    if (out_seed) *out_seed = seed;
    return 1;
}

/* 格内镜像后取岛形，再套岸线与格边包络 */
static IsleField isle_cell_field(int shape, uint32_t seed, double gx, double gz, double field) {
    double tx = gx - floor(gx), tz = gz - floor(gz), u = tx, v = tz, t;
    int sym = (seed >> 16) & 7;
    if (sym & 4) { t = u; u = v; v = t; }
    if (sym & 1) u = 1 - u;
    if (sym & 2) v = 1 - v;
    IsleField f = isle_field(shape, u, v);
    double edge = fmin(fmin(tx, 1 - tx), fmin(tz, 1 - tz));
    double gate = smooth(field / .5) * smooth(edge / .08);
    f.mask *= gate;
    f.dome *= gate;
    return f;
}

/* 床形局部起伏：波长与幅度按格宽包络 */
static double bed_wobble(const MacroWorld *w, double wx, double wz, double bed, double span) {
    double amp = span * 0.0015;
    if (amp < 1.0) amp = 1.0;
    if (amp > 10.0) amp = 10.0;
    if (bed >= 0.0) {
        /* 岛丘：两档细节，避免平顶 */
        double n1 = qlcg_base_noise((float)(wx / (span * 0.012)), 0.0f,
                                    (float)(wz / (span * 0.012)), w->opt.seed ^ 0x1abe1u);
        double n2 = qlcg_base_noise((float)(wx / (span * 0.004)), 0.0f,
                                    (float)(wz / (span * 0.004)), w->opt.seed ^ 0x1abe6u);
        double k = 0.55 + 0.32 * n1 + 0.13 * n2;
        if (k < 0.25) k = 0.25;
        return bed * k;
    }
    double n1 = qlcg_base_noise((float)(wx / (span * 0.05)), 0.0f,
                                (float)(wz / (span * 0.05)), w->opt.seed ^ 0x1abe2u);
    double n2 = qlcg_base_noise((float)(wx / (span * 0.015)), 0.0f,
                                (float)(wz / (span * 0.015)), w->opt.seed ^ 0x1abe3u);
    double v = bed + (n1 * 0.7 + n2 * 0.3) * amp;
    if (v > -0.8) v = -0.8;          /* 噪声不得把湖底抬出水面 */
    return v;
}

/* 岸坡局部起伏：让湖外坡面不平 */
static double shore_wobble(const MacroWorld *w, double wx, double wz, double span, double rise) {
    double amp = rise * 0.28;
    if (amp < 1.0) amp = 1.0;
    if (amp > 12.0) amp = 12.0;
    double n1 = qlcg_base_noise((float)(wx / (span * 0.06)), 0.0f,
                                (float)(wz / (span * 0.06)), w->opt.seed ^ 0x1abe4u);
    double n2 = qlcg_base_noise((float)(wx / (span * 0.02)), 0.0f,
                                (float)(wz / (span * 0.02)), w->opt.seed ^ 0x1abe5u);
    return (n1 * 0.65 + n2 * 0.35) * amp;
}

LakeShoreField qlcg_lake_field(const MacroWorld *w, double wx, double wz) {
    LakeShoreField out={0,-1e9f,1,0,0};
    struct LakeTemplateCache *c=w->lake_templates;
    if (!c) {
        int cell=qlcg_macro_cell_at(w,wx,wz);
        if (w->water[cell]==2) out=(LakeShoreField){1,w->level[cell],-1,w->height[cell]-w->level[cell],4};
        return out;
    }
    double C=w->circumference;
    if(!qlcg_world_flat(w)) {
    wz=fmod(fmod(wz+C/2,C)+C,C)-C/2;
    if (wz>C/4) { wz=C/2-wz; wx+=C/2; }
    if (wz<-C/4) { wz=-C/2-wz; wx+=C/2; }
    }
    double gx=(wx/C+.5)*w->columns;
    gx-=floor(gx/w->columns)*w->columns;
    double gz=(wz/C+qlcg_world_z_offset(w))*w->columns;
    int cell=qlcg_macro_cell_at(w,wx,wz);
    if (w->water[cell]==1) return out;
    int by=(int)floor(gz/4); if (by>=c->rows) by=c->rows-1;
    int bucket=by*c->columns+(int)floor(gx/4);
    double best=-.85;
    for (int k=c->buckets[bucket];k>=0;k=c->refs[k].next) {
        const LakePart *p=&c->parts[c->refs[k].part];
        if (p->port) continue;
        if (w->water[cell]==2 && w->lake_id[cell]>=0 && w->lake_id[cell]!=p->lake) continue;
        double field=part_field(p,gx,gz,w->columns);
        if (field<=best) continue;
        best=field;
        const LakeBody *b=&c->bodies[p->lake];
        static const double chances[10]={.02,.05,.08,.10,.12,.14,.16,.18,.19,.20};
        static const int plain[7]={0,1,2,10,11,12,13};
        int activity=w->opt.tectonic_activity;
        activity=activity<0?0:activity>9?9:activity;
        double span=(double)C/w->columns;
        /* eq_chunks=0 走 base 逐位 parity */
        int detail=(w->opt.equator_chunks>0);
        uint32_t lake_seed=hash((uint32_t)(p->lake+1)*31u^w->opt.seed);
        uint32_t variant=hash(lake_seed^0x9e3779b9u);
        int islands=0, tile=0, symmetry=0;
        double u=0.5+(gx-b->cx)*b->a, v=0.5+(gz-b->cz)*b->d, t;
        if (detail) {
            /* 真实世界：一湖一张中心模板，床形按湖体坐标连续铺满 */
            tile=plain[variant%7];
            symmetry=(lake_seed>>16)&7;
        } else {
            /* 对照夹具：完全照 base 的按格逻辑 */
            uint32_t seed=(uint32_t)(cell+1)*374761393u^w->opt.seed^(uint32_t)p->lake;
            LakePart body={p->lake,b->tile,b->cx,b->cz,b->a,0,0,b->d};
            double x=floor(gx), z=floor(gz);
            int pure=part_field(&body,x,z,w->columns)>.25 && part_field(&body,x+1,z,w->columns)>.25 &&
                part_field(&body,x,z+1,w->columns)>.25 && part_field(&body,x+1,z+1,w->columns)>.25;
            islands=pure && hash(seed)/4294967296.0<chances[activity];
            uint32_t cv=hash(seed^0x9e3779b9u);
            tile=islands ? 3+cv%7 : plain[cv%7];
            symmetry=(seed>>16)&7;
            u=gx-x; v=gz-z;
        }
        if (symmetry&4) { t=u; u=v; v=t; }
        if (symmetry&1) u=1-u;
        if (symmetry&2) v=1-v;
        double depth=6+(((uint32_t)(p->lake+1)*31u^w->opt.seed)%110)/10.0;
        double bedv=read_atlas(LAKE_BODIES+tile,u,v)*depth;
        if (detail) bedv=bed_wobble(w,wx,wz,bedv,span);       /* 局部床形 */
        double wob=(detail && field<0.0)
            ? shore_wobble(w,wx,wz,span,fmax(4,C/w->columns*.002)*w->height_scale)
            : 0.0;                                             /* 湖外岸坡起伏 */
        /* 湖心岛：逐格抽签套 base 岛形；对照夹具不开 */
        if (detail && field>0.2) {
            int shape=0; uint32_t iseed=0;
            if (isle_pick(w,b,p->lake,activity,gx,gz,&shape,&iseed)) {
                IsleField isl=isle_cell_field(shape,iseed,gx,gz,field);
                if (isl.mask>0) {
                    double lift=span*0.0006;
                    if (lift<6.0) lift=6.0;
                    if (lift>15.0) lift=15.0;
                    double peak=2.5+lift*isl.dome;
                    bedv+=(peak-bedv)*isl.mask;
                }
            }
        }
        out=(LakeShoreField){1,w->lakes[p->lake].level,(float)-field,
            (float)(bedv*w->height_scale),
            (float)(fmax(4,C/w->columns*.002)*w->height_scale),
            (float)(wob*w->height_scale)};
    }
    return out;
}
/* 调试与回归：该湖首座岛的世界坐标 */
int qlcg_lake_island_at(const MacroWorld *w, int lake, double *ox, double *oz) {
    struct LakeTemplateCache *c=w->lake_templates;
    if (!c || lake<0 || lake>=c->body_cap) return 0;
    const LakeBody *b=&c->bodies[lake];
    if (!b->count || b->a==0.0 || b->d==0.0) return 0;
    int x0=b->min_x-2,x1=b->max_x+2,z0=b->min_z-2,z1=b->max_z+2;
    LakePart body={lake,b->tile,b->cx,b->cz,b->a,0,0,b->d};
    int activity=w->opt.tectonic_activity;
    activity=activity<0?0:activity>9?9:activity;
    for (int z=z0;z<=z1;z++) for (int x=x0;x<=x1;x++) {
        if (z<0 || z>=w->rows) continue;
        if (part_field(&body,x+.5,z+.5,w->columns)<=.2) continue;
        int shape=0; uint32_t seed=0;
        if (!isle_pick(w,b,lake,activity,x+.5,z+.5,&shape,&seed)) continue;
        int col=(x%w->columns+w->columns)%w->columns;
        int sym=(seed>>16)&7;
        double u=ISLE_SHAPES[shape].blobs[0].x, v=ISLE_SHAPES[shape].blobs[0].z, t;
        if (sym&4) { t=u; u=v; v=t; }
        if (sym&1) u=1-u;
        if (sym&2) v=1-v;
        if (ox) *ox=((col+u)/(double)w->columns-0.5)*(double)w->circumference;
        if (oz) *oz=((z+v)/(double)w->columns-qlcg_world_z_offset(w))*(double)w->circumference;
        return 1;
    }
    return 0;
}

float qlcg_lake_height(const LakeShoreField *f, float natural) {    if (!f->lake) return natural;
    if (f->d<=0) return f->level+f->bed*(-f->d);
    double target=f->level+f->rise*smooth(f->d)+f->wob*smooth(f->d);
    double weight=1-smooth((f->d-.25)/.6);
    return (float)(natural+(target-natural)*weight);
}
