/* 区域起伏、局部模板与河源低丘 */
#include "surface_detail.h"
#include "surface_coast.h"
#include "surface_islands.h"
#include "surface_lakes.h"
#include "surface_rivers.h"
#include "river_archive.h"
#include "gen/gen_noise.h"
#include "gen/gen_water.h"
#include "gen/gen_river.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef QLCG_RIVER_LANDFORMS
#define QLCG_RIVER_LANDFORMS 1
#endif

static const double PI=3.14159265358979323846;
static double clampd(double v,double a,double b) { return fmax(a,fmin(b,v)); }
static double smooth(double t) { t=clampd(t,0,1); return t*t*(3-2*t); }
static double mix(double a,double b,double t) { return a+(b-a)*t; }
static double mod(double x,double m) { return fmod(fmod(x,m)+m,m); }
static void sphere(const MacroWorld *w,double wx,double wz,double C,double p[3]) {
    if(qlcg_world_flat(w)) { p[0]=wx/w->radius; p[1]=1; p[2]=wz/w->radius; return; }
    double lon=wx/C*2*PI,lat=-wz/C*2*PI;
    p[0]=cos(lat)*cos(lon); p[1]=sin(lat); p[2]=cos(lat)*sin(lon);
}
typedef struct { int x,y,z,next; } Key;
typedef struct { int *buckets; Key *keys; int capacity,mask,count,cursor; } Index;
typedef struct {
    double x,y,z,normal[3],east[3],north[3],cosine,sine,rx,rz,amplitude;
    double cliff_kind,cliff_width,cliff_run,cliff_rise,cliff_skew,cliff_length;
} Feature;
typedef enum { VALLEY_PLAIN,VALLEY_GORGE,VALLEY_MEANDER } ValleyRecipe;
typedef struct { int dry,lake,solid; ValleyRecipe recipe; } Mask;
typedef struct { double shape,floodplain; } ValleyRelief;
typedef struct { double toe,spur,apron; } CliffMass;
typedef struct { double height,detail,bend,shape,noise,grade; CliffMass mass; } ReliefField;
typedef struct { double width,rounded,split; CliffMass mass; } CliffProfile;
struct TerrainDetailCache {
    uint32_t revision;
    Index nodes,features,masks,heads;
    int node_buckets[4096],feature_buckets[1024],mask_buckets[8192],head_buckets[8192];
    Key node_keys[2048],feature_keys[512],mask_keys[4096],head_keys[4096];
    double node_values[2048];
    ReliefField node_fields[2048],grid_fields[4];
    Feature feature_values[512],nearby[27];
    Mask mask_values[4096];
    MacroCellPlan head_values[4096];
    int last_feature,feature_x,feature_y,feature_z,last_grid,grid_x,grid_z;
    double grid[4],poles[5][2];
    int pole_ready[5];
    unsigned long long node_hits,node_misses,feature_builds;
};
static unsigned key_hash(int x,int y,int z) {
    return (uint32_t)x*73856093u^(uint32_t)y*19349663u^(uint32_t)z*83492791u;
}
static void init_index(Index *m,int cap,int buckets,int *heads,Key *keys) {
    m->capacity=cap; m->mask=buckets-1; m->buckets=heads; m->keys=keys;
    for (int i=0;i<buckets;i++) heads[i]=-1;
}
static int lookup(Index *m,int x,int y,int z,int *created) {
    unsigned bucket=key_hash(x,y,z)&m->mask;
    for (int k=m->buckets[bucket];k>=0;k=m->keys[k].next)
        if (m->keys[k].x==x && m->keys[k].y==y && m->keys[k].z==z) { *created=0; return k; }
    int at;
    if (m->count<m->capacity) at=m->count++;
    else {
        at=m->cursor; m->cursor=(m->cursor+1)%m->capacity;
        Key old=m->keys[at];
        int *link=&m->buckets[key_hash(old.x,old.y,old.z)&m->mask];
        while (*link!=at) link=&m->keys[*link].next;
        *link=old.next;
    }
    m->keys[at]=(Key){x,y,z,m->buckets[bucket]}; m->buckets[bucket]=at;
    *created=1; return at;
}
void qlcg_terrain_detail_free(MacroWorld *w) { free(w->terrain_detail); w->terrain_detail=NULL; }
static struct TerrainDetailCache *cache_at(const MacroWorld *w) {
    MacroWorld *mutable=(MacroWorld *)w;
    if (w->terrain_detail && w->terrain_detail->revision!=w->surface_revision) qlcg_terrain_detail_free(mutable);
    if (!w->terrain_detail) {
        struct TerrainDetailCache *c=calloc(1,sizeof(*c));
        if (!c) return NULL;
        c->revision=w->surface_revision;
        init_index(&c->nodes,2048,4096,c->node_buckets,c->node_keys);
        init_index(&c->features,512,1024,c->feature_buckets,c->feature_keys);
        init_index(&c->masks,4096,8192,c->mask_buckets,c->mask_keys);
        init_index(&c->heads,4096,8192,c->head_buckets,c->head_keys);
        mutable->terrain_detail=c;
    }
    return w->terrain_detail;
}
TerrainDetailStats qlcg_terrain_detail_stats(const MacroWorld *w) {
    const struct TerrainDetailCache *c=w->terrain_detail;
    if (!c) return (TerrainDetailStats){0};
    return (TerrainDetailStats){c->nodes.count,c->features.count,c->masks.count,c->heads.count,
                               c->node_hits,c->node_misses,c->feature_builds};
}
double qlcg_detail_pole(const MacroWorld *w,const float *values,int pole,double fallback) {
    const float *fields[5]={w->height,w->temperature,w->rainfall,w->moisture,w->evaporation};
    int field=-1;
    for (int k=0;k<5;k++) if (fields[k]==values) { field=k; break; }
    if (field<0) return fallback;
    struct TerrainDetailCache *c=cache_at(w); if (!c) return fallback;
    if (!c->pole_ready[field]) {
        for (int p=0;p<2;p++) {
            double sum=0; int row=p?w->rows-1:0;
            for (int x=0;x<w->columns;x++) sum+=values[row*w->columns+x];
            c->poles[field][p]=sum/w->columns;
        }
        c->pole_ready[field]=1;
    }
    return c->poles[field][pole];
}
static double random_at(int x,int y,int z,uint32_t seed) {
    uint32_t h=(uint32_t)x*374761393u^(uint32_t)y*668265263u^(uint32_t)z*1442695041u^seed;
    h=(h^(h>>13))*1274126177u;
    return (h^(h>>16))/4294967296.0;
}
static double region_noise(const MacroWorld *w,const double p[3]) {
    double span=fmax(1440,w->circumference/(double)w->columns*3),q[3],t[3]; int ids[3];
    for (int k=0;k<3;k++) { q[k]=p[k]*w->radius/span; ids[k]=(int)floor(q[k]); t[k]=smooth(q[k]-ids[k]); }
    double value=0;
    for (int z=0;z<2;z++) for (int y=0;y<2;y++) for (int x=0;x<2;x++)
        value+=random_at(ids[0]+x,ids[1]+y,ids[2]+z,w->opt.seed^0x6c616e64u)*
               (x?t[0]:1-t[0])*(y?t[1]:1-t[1])*(z?t[2]:1-t[2]);
    return value;
}
static double activity_field(const MacroWorld *w,const float *v,int ix,int iy,double tx,double ty) {
    if (!v) return 0;
    double a=v[qlcg_macro_index(w,ix,iy)]*(1-tx)+v[qlcg_macro_index(w,ix+1,iy)]*tx;
    double b=v[qlcg_macro_index(w,ix,iy+1)]*(1-tx)+v[qlcg_macro_index(w,ix+1,iy+1)]*tx;
    return clampd(a*(1-ty)+b*ty,0,1);
}
void qlcg_local_landform_profile(const MacroWorld *w,const double p[3],LocalLandformProfile *o) {
    double x=(atan2(p[2],p[0])/(2*PI)+.5)*w->columns-.5;
    double y=(.25-asin(clampd(p[1],-1,1))/(2*PI))*w->columns-.5;
    if(qlcg_world_flat(w)) {
        x=(p[0]*w->radius/w->circumference+.5)*w->columns-.5;
        y=(p[2]*w->radius/w->circumference+.5)*w->rows-.5;
    }
    int ix=(int)floor(x),iy=(int)floor(y); double tx=smooth(x-ix),ty=smooth(y-iy);
    o->uplift=activity_field(w,w->uplift,ix,iy,tx,ty);
    o->rift=activity_field(w,w->rift,ix,iy,tx,ty);
    o->shear=activity_field(w,w->shear,ix,iy,tx,ty);
    o->region=region_noise(w,p);
    double active=clampd(fmax(o->uplift,fmax(o->rift*.85,o->shear*.7)),0,1);
    double rugged=clampd(active*.8+(o->region-.5)*.5,0,1);
    o->weights[0]=.52-.36*rugged; o->weights[1]=.31-.11*rugged;
    o->weights[2]=.08+.22*rugged; o->weights[3]=.025+.38*o->uplift+.12*o->shear;
    o->weights[4]=.065; o->weights[5]=.28*o->rift;
    double total=0; for (int k=0;k<6;k++) total+=o->weights[k];
    for (int k=0;k<6;k++) o->weights[k]/=total;
}
static Feature build_feature(const MacroWorld *w,int a,int b,int d) {
    static const double shapes[6][4]={{0,0,.8,.8},{8,25,.9,.8},{25,60,.8,.65},
        {50,120,.95,.3},{-15,-5,.8,.8},{-50,-15,.95,.42}};
    double r[6];
    for (int k=0;k<6;k++) r[k]=random_at(a,b,d,w->opt.seed^((uint32_t)k*0x9e3779b1u));
    Feature f={0}; double angle=r[1]*2*PI;
    f.x=a+.5+(r[2]-.5)*.4; f.y=b+.5+(r[3]-.5)*.4; f.z=d+.5+(r[4]-.5)*.4;
    double n=hypot(hypot(f.x,f.y),f.z); if (!n) n=1;
    f.normal[0]=f.x/n; f.normal[1]=f.y/n; f.normal[2]=f.z/n;
    double sample[3]={f.normal[0],f.normal[1],f.normal[2]};
    if(qlcg_world_flat(w)) {
        sample[0]=f.x*240/w->radius; sample[1]=1; sample[2]=f.z*240/w->radius;
        f.normal[0]=0; f.normal[1]=1; f.normal[2]=0;
    }
    LocalLandformProfile profile; qlcg_local_landform_profile(w,sample,&profile);
    double pick=r[0]; int kind=0;
    for (int k=0;k<6;k++) { pick-=profile.weights[k]; if (pick<=0) { kind=k; break; } }
    double east=hypot(f.normal[0],f.normal[2]);
    f.east[0]=east>1e-8?-f.normal[2]/east:1;
    f.east[2]=east>1e-8?f.normal[0]/east:0;
    f.north[0]=f.normal[1]*f.east[2]-f.normal[2]*f.east[1];
    f.north[1]=f.normal[2]*f.east[0]-f.normal[0]*f.east[2];
    f.north[2]=f.normal[0]*f.east[1]-f.normal[1]*f.east[0];
    f.cosine=cos(angle); f.sine=sin(angle); f.rx=shapes[kind][2]; f.rz=shapes[kind][3];
    f.amplitude=shapes[kind][0]+(shapes[kind][1]-shapes[kind][0])*r[5];
    double pick_cliff=random_at(a,b,d,w->opt.seed^0x636c6633u);
    f.cliff_kind=pick_cliff<.22?1:pick_cliff<.48?2:0;
    f.cliff_width=f.cliff_kind==1?82+62*r[1]:44+42*r[1];
    f.cliff_run=150+62*r[3];f.cliff_rise=.28+.36*r[4];
    f.cliff_skew=(r[5]-.5)*.7;f.cliff_length=f.cliff_kind==1?85+125*r[2]:28+82*r[2];
    return f;
}
static double local_landform_height(const MacroWorld *w,const double p[3],CliffMass *mass,double gx,double gz) {
    struct TerrainDetailCache *c=cache_at(w); if (!c) return 0;
    double x=p[0]*w->radius/240,y=p[1]*w->radius/240,z=p[2]*w->radius/240;
    int ix=(int)floor(x),iy=(int)floor(y),iz=(int)floor(z);
    if (!c->last_feature || c->feature_x!=ix || c->feature_y!=iy || c->feature_z!=iz) {
        int at=0;
        for (int dz=-1;dz<=1;dz++) for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
            int created,k=lookup(&c->features,ix+dx,iy+dy,iz+dz,&created);
            if (created) { c->feature_values[k]=build_feature(w,ix+dx,iy+dy,iz+dz); c->feature_builds++; }
            c->nearby[at++]=c->feature_values[k];
        }
        c->feature_x=ix; c->feature_y=iy; c->feature_z=iz; c->last_feature=1;
    }
    double h=0;
    for (int k=0;k<27;k++) {
        const Feature *f=&c->nearby[k]; if (!f->amplitude && (!mass || !f->cliff_kind)) continue;
        double dx=x-f->x,dy=y-f->y,dz=z-f->z;
        double e=dx*f->east[0]+dy*f->east[1]+dz*f->east[2];
        double n=dx*f->north[0]+dy*f->north[1]+dz*f->north[2];
        if(mass && f->cliff_kind) {
            double along=(e*gx-n*gz)*240,across=(e*gz+n*gx)*240+along*f->cliff_skew;
            double width=f->cliff_width*(across<0?1+f->cliff_skew:1-f->cliff_skew);
            double radial=(dx*f->normal[0]+dy*f->normal[1]+dz*f->normal[2])/.78;
            double radius=across*across/(width*width)+along*along/(f->cliff_run*f->cliff_run)+radial*radial;
            double weight=1-smooth(radius);
            if(f->cliff_kind==1) { mass->toe+=weight*f->cliff_length;mass->apron+=weight*f->cliff_rise; }
            else mass->spur+=weight*f->cliff_length;
        }
        double u=(e*f->cosine+n*f->sine)/f->rx;
        double v=(dx*f->normal[0]+dy*f->normal[1]+dz*f->normal[2])/.95;
        double a=(-e*f->sine+n*f->cosine)/f->rz,r=u*u+v*v+a*a;
        if(r<1) h+=f->amplitude*pow(1-r,3);
    }
    return h;
}
double qlcg_local_landform_height(const MacroWorld *w,const double p[3]) {
    return local_landform_height(w,p,NULL,0,0);
}
static ValleyRecipe valley_recipe(const MacroWorld *w,int cell) {
    int r=w->receiver[cell];
    if (r<0 || r>=w->cell_count) return VALLEY_PLAIN;
    double span=w->circumference/w->columns;
    double grade=fmax(0,w->spill[cell]-w->spill[r])/(span*fmax(.01,w->height_scale));
    if (grade>.012 || w->uplift[cell]>.64) return VALLEY_GORGE;
    int next=w->receiver[r];
    if (grade<.006 && next>=0 && next<w->cell_count) {
        int ax=r%w->columns-cell%w->columns,bx=next%w->columns-r%w->columns;
        ax-=w->columns*(int)round((double)ax/w->columns);
        bx-=w->columns*(int)round((double)bx/w->columns);
        int az=r/w->columns-cell/w->columns,bz=next/w->columns-r/w->columns;
        if (ax*bz!=az*bx) return VALLEY_MEANDER;
    }
    return VALLEY_PLAIN;
}

static Mask *mask_at(const MacroWorld *w,struct TerrainDetailCache *c,int cell) {
    int created,k=lookup(&c->masks,cell,0,0,&created);
    if (created) {
        int x=cell%w->columns,y=cell/w->columns,lake=1;
        for (int dz=-1;dz<=1;dz++) for (int dx=-1;dx<=1;dx++)
            if (w->water[qlcg_macro_index(w,x+dx,y+dz)]==2) lake=0;
        int solid=QLCG_RIVER_LANDFORMS && (w->water[cell]==1 || qlcg_lake_overlaps_cell(w,cell));
        int dry=!qlcg_macro_cell_has_water(w,cell);
        ValleyRecipe recipe=QLCG_RIVER_LANDFORMS && !dry && !solid?valley_recipe(w,cell):VALLEY_PLAIN;
        c->mask_values[k]=(Mask){dry,lake,solid,recipe};
    }
    return &c->mask_values[k];
}
static void valley_section(const MacroWorld *w,int cell,ValleyRecipe recipe,double x,
                            double blend,ValleyRelief *out) {
    double jitter=random_at(cell,0,0,w->opt.seed^0x74657272u);
    double first=.30+.08*jitter,second=.52+.05*jitter;
    double h1=2.3+1.3*jitter,h2=jitter>.48?1.0+1.5*jitter:0;
    double shoulder=2.1+2.2*clampd(w->uplift[cell],0,1);
    double amplitude=.65;
    if (recipe==VALLEY_GORGE) {
        first-=.07; h1*=.42; h2=0; shoulder*=1.7; amplitude=.36;
    } else if (recipe==VALLEY_MEANDER) {
        first+=.055; h1*=.78; h2=0; shoulder*=.68; amplitude=.82;
    }
    double outer=1-smooth((x-.78)/.22);
    double steps=h1*smooth((x-first)/.12)+h2*smooth((x-second)/.12);
    double crest=smooth((x-.56)/.21)*outer;
    out->shape+=blend*(steps*outer+shoulder*crest);
    out->floodplain+=blend*amplitude*smooth(x/.14)*(1-smooth((x-first)/.20));
}

static double landform_fields(const MacroWorld *w,const SurfaceMacroContext *p,double wx,double wz,
                              ValleyRelief *relief) {
    struct TerrainDetailCache *c=cache_at(w); if (!c) return 0;
    int ids[4]={p->a,p->b,p->c,p->d};
    ValleyRecipe recipes[4];
    double tx=smooth(p->tx),tz=smooth(p->tz),distance=INFINITY,coast=INFINITY,lake=0;
    for (int k=0;k<4;k++) {
        int x=k%2,z=k/2; Mask *m=mask_at(w,c,ids[k]);
        recipes[k]=m->recipe;
        lake+=m->lake*(x?tx:1-tx)*(z?tz:1-tz);
        if (!m->dry || m->solid) {
            double d=hypot(fmax(0,fabs(p->tx-x)-.5),fmax(0,fabs(p->tz-z)-.5));
            if (!m->dry) distance=fmin(distance,d);
            if (m->solid) coast=fmin(coast,d);
        }
    }
    double old=smooth(distance/.25)*lake;
    if (!QLCG_RIVER_LANDFORMS || old==lake || lake==0) return old;
    double shore=smooth(coast/.25);
    double recover=shore*lake-old;
    if (recover<=0 || (!qlcg_world_flat(w) && fabs(wz)>=w->circumference*.25)) return old;
    double cosine=qlcg_world_flat(w)?1:cos(wz/w->circumference*2*PI);
    double span=w->circumference/w->columns*cosine;
    double supported=smooth((span-1536)/1536)*smooth((cosine-.15)/.15);
    if (supported==0) return old;
    int cell=qlcg_macro_cell_at(w,wx,wz);
    RiverDetailCandidates list;
    if (!qlcg_river_detail_candidates(w,cell,&list)) return old;
    RiverValleySample valley;
    double gate=qlcg_river_detail_gate(w,&list,wx,wz,&valley);
    double restored=recover*supported*gate;
    if (gate>0 && valley.position>0 && valley.position<1) {
        double blend=valley.protection*supported*shore*lake*(1-smooth(distance/.25));
        double x=valley.position;
        for (int k=0;k<4;k++)
            valley_section(w,ids[k],recipes[k],x,blend*(k%2?tx:1-tx)*(k/2?tz:1-tz),relief);
        double floor=smooth(x/.13)*(1-smooth((x-.32)/.53));
        restored*=1-.72*floor*blend;
    }
    return old+restored;
}
double qlcg_local_landform_weight(const MacroWorld *w,const SurfaceMacroContext *p,double wx,double wz) {
    ValleyRelief relief={0};
    return landform_fields(w,p,wx,wz,&relief);
}
static double plan_random(uint32_t *seed) {
    uint32_t t=(*seed+=0x6d2b79f5u);
    t=(t^(t>>15))*(t|1u); t^=t+(t^(t>>7))*(t|61u);
    return (t^(t>>14))/4294967296.0;
}
static double plan_width(const MacroWorld *w,int cell) {
    return w->main_river_width?fmax(w->river_width[cell],w->main_river_width[cell]):w->river_width[cell];
}
static void source_plan(const MacroWorld *w,int cell,MacroCellPlan *p) {
    memset(p,0,sizeof(*p)); p->cell=cell; p->lake_in=p->lake_out=-1;
    if (!w->rainfall || !w->receiver || !w->river_width || w->height[cell]<=0 || w->water[cell]==1) return;
    int r=w->receiver[cell],incoming=0;
    p->lake_out=qlcg_lake_outflow_at(w,cell);
    p->near_river=plan_width(w,cell)>0;
    for (int k=0;k<8;k++) {
        int i=w->neighbors[cell*8+k];
        if (plan_width(w,i)>0) {
            p->near_river=1;
            if (w->receiver[i]==cell) incoming++;
        }
        if (w->water[i]==2 && w->lake_id && w->lake_id[i]>=0) {
            int lake=w->lake_id[i];
            if (w->lakes[lake].outlet==cell) p->lake_out=lake;
        }
    }
    p->head=plan_width(w,cell)>0 && r>=0 && !incoming && p->lake_out<0 && w->water[cell]!=2;
    if (r>=0 && w->water[r]==2 && w->lake_id) p->lake_in=w->lake_id[r];
    double span=w->circumference/w->columns;
    if (w->water[cell]!=2 && (span<640 || !p->near_river || w->rainfall[cell]<=0)) return;
    SourcePlan *s=&p->source;
    s->frame=qlcg_surface_frame(w,cell);
    double edge_lat=fmin(PI/2,fabs(s->frame.lat)+span/(2*s->frame.radius));
    double east=s->frame.flat?span:span*fmax(.001,cos(edge_lat)),usable=fmin(span,east);
    if (usable<96) return;
    s->seed=w->opt.seed^((uint32_t)(cell+1)*0x706c616eu);
    uint32_t rng=s->seed;
    s->mirror=plan_random(&rng)<.5?-1:1;
    SurfacePoint target={1,0};
    if (r>=0) {
        SurfaceFrame f=qlcg_surface_frame(w,r);
        target=qlcg_frame_local(&s->frame,f.flat?f.anchor[0]:f.lon*f.radius,f.flat?f.anchor[2]:-f.lat*f.radius);
    }
    double length=fmax(1,hypot(target.x,target.z));
    s->dx=target.x/length; s->dz=target.z/length;
    s->slope=r>=0?fmax(0,w->spill[cell]-w->spill[r])/length/fmax(.01,w->height_scale):0;
    int snow=w->height[cell]>=1800 && w->temperature[cell]<=2;
    double rain=w->rainfall[cell]/fmax(150,w->evaporation[cell]);
    double weights[6]={snow?.30:0,rain>.55?.24:.04,s->slope>.02?.22:0,snow&&s->slope<.012?.20:.06,.14,.12};
    static const int kinds[6]={SRC_PLAN_SNOWPEAK,SRC_PLAN_TARN,SRC_PLAN_HANGING,SRC_PLAN_GENTLE,SRC_PLAN_STEP,SRC_PLAN_SPRING};
    double sum=0; for (int k=0;k<6;k++) sum+=weights[k];
    double pick=plan_random(&rng)*sum; s->kind=SRC_PLAN_SPRING;
    for (int k=0;k<6;k++) { pick-=weights[k]; if (pick<0) { s->kind=kinds[k]; break; } }
    if (p->head && snow) s->kind=SRC_PLAN_SNOWPEAK;
    if (w->water[cell]==2) s->kind=SRC_PLAN_LAKE_SHORE;
    if (r>=0 && w->height[r]>w->height[cell] && (s->kind==SRC_PLAN_TARN || s->kind==SRC_PLAN_HANGING)) s->kind=SRC_PLAN_SPRING;
    static const double radius[8][2]={{0,0},{120,260},{180,380},{480,1100},{320,800},{650,1600},{700,1800},{80,160}};
    static const double amp[8][2]={{0,0},{2,4},{4,8},{10,24},{6,20},{3,8},{40,120},{0,0}};
    s->radius=fmin(.28*usable,mix(radius[s->kind][0],radius[s->kind][1],plan_random(&rng)));
    s->amplitude=mix(amp[s->kind][0],amp[s->kind][1],plan_random(&rng));
    s->phase=plan_random(&rng)*2*PI; s->snow=s->kind==SRC_PLAN_SNOWPEAK;
    double along=p->head?-s->radius*.62:-span*.08;
    double across=p->head?s->mirror*s->radius*.18:s->mirror*usable*.20;
    s->x=clampd(s->dx*along-s->dz*across,-east*.18,east*.18);
    s->z=clampd(s->dz*along+s->dx*across,-span*.18,span*.18);
    /* 新增支流不改既有源头地形 */
    if(w->water[cell]!=2&&((w->rainfall[cell]<400&&!p->head)||
       qlcg_runoff_m3s(w->rainfall[cell],w->evaporation[cell],w->areas[cell])<.03)) s->amplitude=0;
    s->valid=1;
}
const MacroCellPlan *qlcg_macro_cell_plan(const MacroWorld *w,int cell) {
    if (cell<0 || cell>=w->cell_count) return NULL;
    struct TerrainDetailCache *c=cache_at(w); if (!c) return NULL;
    int created,k=lookup(&c->heads,cell,0,0,&created);
    if (created) source_plan(w,cell,&c->head_values[k]);
    return &c->head_values[k];
}
int qlcg_headwater_landform(const MacroWorld *w,int cell,HeadwaterLandform *out) {
    const MacroCellPlan *p=qlcg_macro_cell_plan(w,cell);
    if (!p || !p->head || !p->source.valid) return 0;
    const SourcePlan *s=&p->source;
    if (out) *out=(HeadwaterLandform){s->frame,s->dx,s->dz,s->radius,s->amplitude,s->phase};
    return 1;
}
double qlcg_headwater_relief(const MacroWorld *w,double wx,double wz) {
    int cell=qlcg_macro_cell_at(w,wx,wz);
    const MacroCellPlan *plan=qlcg_macro_cell_plan(w,cell);
    if (!plan || !plan->source.valid || plan->source.amplitude<=0) return 0;
    const SourcePlan *f=&plan->source;
    SurfacePoint p=qlcg_frame_local(&f->frame,wx,wz);
    double dx=p.x-f->x,dz=p.z-f->z,r=hypot(dx,dz)/f->radius;
    if (r>=1) return 0;
    double v=(-dx*f->dz+dz*f->dx)/f->radius;
    double crown=1-smooth(r),fold=1-.12*smooth(r)*sin(v*5+f->phase);
    return f->amplitude*crown*crown*fold;
}

static double terrace_level(double h,double step,double transition) {
    double phase=h/step;
    return (floor(phase)+smooth((phase-floor(phase)-.5)/transition+.5))*step;
}
static CliffProfile cliff_profile(double shape) {
    double rounded=smooth((shape-.05)/.45),split=1-smooth((shape+.45)/.40);
    return (CliffProfile){38+rounded*18+split*8,rounded,split};
}
static double cliff_section(double t,const CliffProfile *p) {
    if(t<=0) return 0;
    if(t>=1) return 1;
    double scarp=.18*smooth(t/.45)+.74*smooth((t-.35)/.35)+.08*smooth((t-.65)/.35);
    double split=.28*smooth(t/.30)+.72*smooth((t-.46)/.54);
    return mix(mix(scarp,split,p->split),smooth(t),p->rounded);
}
static double cliff_level(double h,double step,double transition,const CliffProfile *p) {
    if(!p) return terrace_level(h,step,transition);
    double phase=h/step;
    double t=(phase-floor(phase)-.5)/transition+.5;
    double spur=fmin(.25,transition*p->mass.spur/p->width)/transition;
    double toe=fmin(.32,transition*p->mass.toe/p->width)/transition;
    double face=.70*cliff_section(t+spur,p)+.30*cliff_section(t+spur*.12,p);
    double apron=smooth((t+toe)/(.55+toe))*clampd(p->mass.apron,0,.70);
    if(p->mass.spur==0 && p->mass.apron==0) face=cliff_section(t,p);
    return (floor(phase)+face+apron*(1-face))*step;
}
static double drainage_field(const MacroWorld *w,const SurfaceMacroContext *c,double wz,
                             double noise,double strength,double *height,const CliffProfile *profile) {
    if(!w->receiver||!w->river_width||!w->flow||!w->spill||!w->water||!w->level) return 0;
    int ids[4]={c->a,c->b,c->c,c->d};
    double scale=qlcg_world_flat(w)?1:cos(wz/w->circumference*2*PI);
    double span=w->circumference/w->columns,sum=0,total=0,gate=0;
    double step=fmax(1,fmin(160,60*w->height_scale));
    for(int k=0;k<4;k++) {
        int i=ids[k],r=w->receiver[i];
        if(r<0||r>=w->cell_count||r==i||w->height[i]<=0||w->height[r]<=0) continue;
        double width=fmax(w->river_width[i],w->main_river_width?w->main_river_width[i]:0);
        if(width<=0) continue;
        double dx=r%w->columns-i%w->columns,dz=r/w->columns-i/w->columns;
        if(!qlcg_world_flat(w)) dx-=round(dx/w->columns)*w->columns;
        double px=(c->tx-k%2)*scale,pz=c->tz-k/2;
        dx*=scale;
        double t=clampd((px*dx+pz*dz)/fmax(1e-9,dx*dx+dz*dz),0,1);
        double distance=hypot(px-dx*t,pz-dz*t);
        double reach=.20+.04*scale,mask=1-smooth((distance-reach)/.12);
        mask*=smooth((qlcg_river_discharge_width(w->flow[i])-80)/120);
        if(mask<=0) continue;
        gate=fmax(gate,mask);
        if(!height) continue;
        double a=w->water[i]==2?w->level[i]:w->spill[i];
        double b=w->water[r]==2?w->level[r]:w->spill[r];
        double grade=fmax(0,a-b)/fmax(1,span*hypot(dx,dz));
        double transition=clampd((profile?profile->width:32)*grade/step,.00001,profile?.18:.06);
        double first=cliff_level(a,step,transition,profile),last=cliff_level(b,step,transition,profile);
        double bend=grade*400*noise*smooth(t/.03)*smooth((1-t)/.03);
        double middle=cliff_level(mix(a,b,t)+bend,step,transition,profile);
        double part=first>last+.01?clampd((first-middle)/(first-last),0,1):t;
        double valley=mix(a,b,mix(t,part,strength));
        double weight=mask/pow(.02+distance,4);
        sum+=weight*valley;total+=weight;
    }
    if(height&&total>0) *height=sum/total;
    return gate*(c->pole>=0?c->pole:1);
}
static ReliefField relief_field(const MacroWorld *w,double wx,double wz) {
    SurfaceMacroContext c=qlcg_macro_context(w,wx,wz);double p[3];sphere(w,wx,wz,w->circumference,p);
    double base=qlcg_read_macro(w,&c,w->height),g=w->height_scale;
    double uplift=clampd(qlcg_read_macro(w,&c,w->uplift),0,1),roughness=(base>0?18+uplift*150:12)*g;
    const double scales[3]={12000,3000,750},weights[3]={.55,.30,.15};
    const uint32_t seeds[3]={0x72656731u,0x72656732u,0x72656733u};
    double broad=0,bands[3];
    for(int k=0;k<3;k++) {
        bands[k]=qlcg_base_noise_d(p[0]*w->radius/scales[k],p[1]*w->radius/scales[k],p[2]*w->radius/scales[k],w->opt.seed^seeds[k]);
        broad+=bands[k]*weights[k];
    }
    double pole=c.pole>=0?c.pole:1,coast=smooth((base-4)/(40*g));
    double dx=mix(w->height[c.b]-w->height[c.a],w->height[c.d]-w->height[c.c],c.tz);
    double dz=mix(w->height[c.c]-w->height[c.a],w->height[c.d]-w->height[c.b],c.tx);
    double grade=hypot(dx,dz)*pole*w->columns/w->circumference+roughness*.15/750;
    ReliefField field={base+smooth(fabs(base)/(160*g))*broad*roughness,0,
        fmin(160,60*g)*(bands[0]*.04+bands[1]*.025),bands[2],bands[0],grade};
    if(base>0) {
        double east=qlcg_world_flat(w)?1:fmax(.01,cos(wz/w->radius));
        double slope=hypot(dx/east,dz),gx=slope>1e-8?dx/east/slope:1,gz=slope>1e-8?dz/slope:0;
        CliffMass *mass=qlcg_river_archive_terrain(w)>=3?&field.mass:NULL;
        field.detail=local_landform_height(w,p,mass,gx,gz)*fmin(g,1)*coast*pole+
            qlcg_headwater_relief(w,wx,wz)*g*coast;
    }
    return field;
}
static double formed_relief(const MacroWorld *w,double wx,double wz,const ReliefField *f) {
    SurfaceMacroContext c=qlcg_macro_context(w,wx,wz);
    double base=qlcg_read_macro(w,&c,w->height),g=w->height_scale;
    double uplift=clampd(qlcg_read_macro(w,&c,w->uplift),0,1);
    double strength=smooth((uplift-.18)/.5)*smooth((base-4)/(40*g))*(c.pole>=0?c.pole:1);
    CliffProfile profile=cliff_profile(f->shape);
    profile.mass=f->mass;
    double step=fmax(1,fmin(160,60*g)),transition=clampd(profile.width*f->grade/step,.015,.18);
    double terrace=cliff_level(f->height,step,transition,&profile)+f->bend;
    double valley=base,gate=drainage_field(w,&c,wz,f->noise,strength,&valley,&profile);
    double detail=1-gate;
    if(qlcg_river_archive_terrain(w)>=3 && gate>0) {
        RiverDetailCandidates rivers;
        RiverValleySample valley;
        if(qlcg_river_detail_candidates(w,qlcg_macro_cell_at(w,wx,wz),&rivers))
            detail=fmax(detail,qlcg_river_detail_gate(w,&rivers,wx,wz,&valley));
    }
    return mix(mix(f->height,terrace,strength),valley,gate)-base+f->detail*detail;
}
double qlcg_regional_relief(const MacroWorld *w,double wx,double wz) {
    if(qlcg_river_archive_terrain(w)>=2) {
        ReliefField field=relief_field(w,wx,wz);
        return formed_relief(w,wx,wz,&field);
    }
    SurfaceMacroContext context=qlcg_macro_context(w,wx,wz); double p[3]; sphere(w,wx,wz,w->circumference,p);
    double base=qlcg_read_macro(w,&context,w->height),g=w->height_scale;
    double uplift=clampd(qlcg_read_macro(w,&context,w->uplift),0,1);
    double roughness=(base>0?18+uplift*150:12)*g;
    const double scales[3]={12000,3000,750},weights[3]={.55,.30,.15};
    const uint32_t seeds[3]={0x72656731u,0x72656732u,0x72656733u};
    double broad=0,bands[3];
    for (int k=0;k<3;k++) {
        bands[k]=qlcg_base_noise_d(p[0]*w->radius/scales[k],p[1]*w->radius/scales[k],p[2]*w->radius/scales[k],w->opt.seed^seeds[k]);
        broad+=bands[k]*weights[k];
    }
    broad*=roughness;
    double pole=context.pole>=0?context.pole:1;
    double coast=smooth((base-4)/(40*g));
    double terrain=base+smooth(fabs(base)/(160*g))*broad;
    double step=fmin(160,60*g),bend=step*(bands[0]*.04+bands[1]*.025);
    double phase=terrain/fmax(1,step),transition=.045+.0125*(bands[2]+1);
    double terrace=(floor(phase)+smooth((phase-floor(phase)-.5)/transition+.5))*step+bend;
    double strength=smooth((uplift-.18)/.5)*coast*pole;
    double valley=base,gate=qlcg_river_archive_terrain(w)?drainage_field(w,&context,wz,bands[0],strength,&valley,NULL):0;
    return mix(mix(terrain,terrace,strength),valley,gate)-base+
        (base>0&&gate<1?qlcg_local_landform_height(w,p)*fmin(g,1)*coast*pole*(1-gate):0)+
        (base>0&&gate<1?qlcg_headwater_relief(w,wx,wz)*g*coast*(1-gate):0);
}

static double relief_node(const MacroWorld *w,struct TerrainDetailCache *c,int x,int z) {
    double W=w->circumference/32.0; if(!qlcg_world_flat(w)) x=(int)(mod(x+W/2,W)-W/2);
    int created,k=lookup(&c->nodes,x,0,z,&created);
    if (created) {
        c->node_misses++;
        c->node_values[k]=qlcg_regional_relief(w,x*32.0,clampd(z*32.0,-qlcg_world_depth(w)/2,qlcg_world_depth(w)/2));
    } else c->node_hits++;
    return c->node_values[k];
}
static ReliefField field_node(const MacroWorld *w,struct TerrainDetailCache *c,int x,int z) {
    double W=w->circumference/32.0;if(!qlcg_world_flat(w)) x=(int)(mod(x+W/2,W)-W/2);
    int created,k=lookup(&c->nodes,x,0,z,&created);
    if(created) {
        c->node_misses++;
        c->node_fields[k]=relief_field(w,x*32.0,clampd(z*32.0,-qlcg_world_depth(w)/2,qlcg_world_depth(w)/2));
    } else c->node_hits++;
    return c->node_fields[k];
}
static ReliefField mix_field(ReliefField a,ReliefField b,double t) {
    return (ReliefField){mix(a.height,b.height,t),mix(a.detail,b.detail,t),mix(a.bend,b.bend,t),
        mix(a.shape,b.shape,t),mix(a.noise,b.noise,t),mix(a.grade,b.grade,t),
        {mix(a.mass.toe,b.mass.toe,t),mix(a.mass.spur,b.mass.spur,t),mix(a.mass.apron,b.mass.apron,t)}};
}
double qlcg_sampled_regional_relief(const MacroWorld *w,double wx,double wz) {
    double C=w->circumference;
    if (!qlcg_world_flat(w) && (wz<-C/4 || wz>C/4)) {
        wz=mod(wz+C/2,C)-C/2;
        if (wz>C/4) { wz=C/2-wz; wx+=C/2; }
        if (wz<-C/4) { wz=-C/2-wz; wx+=C/2; }
    }
    if (!qlcg_world_flat(w) && (wx<-C/2 || wx>=C/2)) wx=mod(wx+C/2,C)-C/2;
    int ix=(int)floor(wx/32),iz=(int)floor(wz/32);
    double tx=wx/32-ix,tz=wz/32-iz;
    struct TerrainDetailCache *c=cache_at(w); if (!c) return qlcg_regional_relief(w,wx,wz);
    if(qlcg_river_archive_terrain(w)>=2) {
        if(!c->last_grid || c->grid_x!=ix || c->grid_z!=iz) {
            c->grid_fields[0]=field_node(w,c,ix,iz);c->grid_fields[1]=field_node(w,c,ix+1,iz);
            c->grid_fields[2]=field_node(w,c,ix,iz+1);c->grid_fields[3]=field_node(w,c,ix+1,iz+1);
            c->grid_x=ix;c->grid_z=iz;c->last_grid=1;
        }
        ReliefField field=mix_field(mix_field(c->grid_fields[0],c->grid_fields[1],tx),
            mix_field(c->grid_fields[2],c->grid_fields[3],tx),tz);
        return formed_relief(w,wx,wz,&field);
    }
    if (!c->last_grid || c->grid_x!=ix || c->grid_z!=iz) {
        c->grid[0]=relief_node(w,c,ix,iz); c->grid[1]=relief_node(w,c,ix+1,iz);
        c->grid[2]=relief_node(w,c,ix,iz+1); c->grid[3]=relief_node(w,c,ix+1,iz+1);
        c->grid_x=ix; c->grid_z=iz; c->last_grid=1;
    }
    return mix(mix(c->grid[0],c->grid[1],tx),mix(c->grid[2],c->grid[3],tx),tz);
}
double qlcg_natural_detail_coast(const MacroWorld *w,double wx,double wz,CoastSample *out) {
    if(qlcg_world_flat(w) && fmax(fabs(wx),fabs(wz))>=w->circumference*.5) {
        if(out) *out=(CoastSample){.height=-3000};
        return -3000;
    }
    SurfaceMacroContext c=qlcg_macro_context(w,wx,wz); double p[3]; sphere(w,wx,wz,w->circumference,p);
    double base=qlcg_read_macro(w,&c,w->height),g=w->height_scale,r=w->radius;
    double fine=qlcg_base_noise_d(p[0]*r/42,p[1]*r/42,p[2]*r/42,w->opt.seed^0x64657461u)*1.5*g*smooth(fabs(base)/(30*g));
    if(qlcg_river_archive_terrain(w)) fine*=1-drainage_field(w,&c,wz,0,0,NULL,NULL);
    double warp=clampd(fine,-8,8);
    double height=base+qlcg_sampled_regional_relief(w,wx+warp*.8,wz-warp*.6)+fine;
    /* 单格海岛模板在这一层替换，3D、舆图与探针同源 */
    double island=0;
    CoastSample coast={.height=island};
    if(!qlcg_island_coast(w,wx,wz,height,&coast.height))
        coast=qlcg_coast_surface(w,&c,wx,wz,base,height);
    if(out) *out=coast;
    return coast.height;
}

double qlcg_natural_detail_height(const MacroWorld *w,double wx,double wz) {
    return qlcg_natural_detail_coast(w,wx,wz,NULL);
}
