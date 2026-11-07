#include "world_infinite.h"
#include "river_archive.h"
#include "../../core/math_util.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <limits.h>

uint32_t qlcg_infinite_seed(uint32_t seed,int32_t x,int32_t z) {
    if(!x && !z) return seed;
    return qlcg_hash_u32(seed^qlcg_hash_u32((uint32_t)x^0x706c616eu)^
                        qlcg_hash_u32((uint32_t)z^0x776f726cu));
}

void qlcg_infinite_options(WorldCreatorOptions *o) {
    if(o->map_mode!=WORLD_MAP_INFINITE) return;
    if(!o->plane_x && !o->plane_z) o->world_seed=o->seed;
    o->seed=qlcg_infinite_seed(o->world_seed,o->plane_x,o->plane_z);
    uint32_t h=qlcg_hash_u32(o->seed^0x666c6174u);
    bool home=!o->plane_x && !o->plane_z;
    qlcg_apply_world_preset(o,home?WORLD_PRESET_HABITABLE:(WorldPresetId)(h%5));
    o->land_ratio=home ? .36f+(h>>8)/16777216.0f*.10f : .10f+(h>>8)/16777216.0f*.70f;
    o->tectonic_activity=home?5:(int)(qlcg_hash_u32(h)%10);
    uint32_t climate=qlcg_hash_u32(o->seed^0x636c696du);
    o->pole_temp=-40+100*(climate>>8)/16777216.0f;
    if(!home) {
        o->terrain_recipe=(MacroTerrainRecipe)(qlcg_hash_u32(o->seed^0x72656370u)%TERRAIN_RECIPE_CNT);
        o->equator_temp=15;
    }
    o->astronomy_enabled=true; o->astronomy_version=1u;
    o->calendar_mode=CALENDAR_DEFAULT; o->axial_tilt_deg=23.5f; o->prograde=true;
    o->monsoon_direction=home?90:(int)(qlcg_hash_u32(o->seed^0x77696e64u)%360);
    qlcg_sync_creator_dimensions(o);
}

float qlcg_infinite_climate_weight(int x,int y,int cols,int rows) {
    float r=hypotf((2.0f*x+1)/cols-1,(2.0f*y+1)/rows-1);
    float t=qlcg_clampf((.96f-r)/.24f,0,1);
    return t*t*(3-2*t);
}

float qlcg_infinite_climate_base(const WorldCreatorOptions *o,int x,int y,int cols,int rows) {
    uint32_t h=qlcg_hash_u32(o->seed^0x636c696du);
    bool home=!o->plane_x && !o->plane_z;
    float center=o->pole_temp;
    float rim=home?21+9*(qlcg_hash_u32(h)>>8)/16777216.0f:15;
    float r=hypotf((2.0f*x+1)/cols-1,(2.0f*y+1)/rows-1);
    float t=qlcg_clampf(r/.65f,0,1); t=t*t*(3-2*t);
    float inner=center+(rim-center)*t;
    return 15+(inner-15)*qlcg_infinite_climate_weight(x,y,cols,rows);
}

static size_t tile_at(const InfiniteWorld *p,int32_t x,int32_t z) {
    size_t lo=0,hi=p->count;
    while(lo<hi) {
        size_t m=lo+(hi-lo)/2; const WorldCreatorOptions *o=&p->items[m]->opt;
        if(o->plane_x<x || (o->plane_x==x && o->plane_z<z)) lo=m+1; else hi=m;
    }
    return lo;
}

MacroWorld *qlcg_infinite_find(const MacroWorld *w,int32_t x,int32_t z) {
    if(w->opt.plane_x==x && w->opt.plane_z==z) return (MacroWorld *)w;
    const InfiniteWorld *p=w->planes; if(!p) return NULL;
    size_t at=tile_at(p,x,z);
    if(at==p->count) return NULL;
    MacroWorld *v=p->items[at];
    return v->opt.plane_x==x && v->opt.plane_z==z ? v : NULL;
}

static MacroWorld *offset_tile(const MacroWorld *w,double dx,double dz) {
    double x=w->opt.plane_x+dx,z=w->opt.plane_z+dz;
    if(x<INT32_MIN || x>INT32_MAX || z<INT32_MIN || z>INT32_MAX)return NULL;
    return qlcg_infinite_find(w,(int32_t)x,(int32_t)z);
}

MacroWorld *qlcg_infinite_resolve(const MacroWorld *w,double wx,double wz,double *lx,double *lz) {
    if(!w || !isfinite(wx) || !isfinite(wz))return NULL;
    if(!qlcg_world_flat(w)){*lx=wx;*lz=wz;return (MacroWorld *)w;}
    double cell=(double)w->circumference/w->columns,half=w->circumference*.5,period=w->circumference+cell;
    double dx=floor(wx/period+.5),dz=floor(wz/period+.5);
    MacroWorld *v=offset_tile(w,dx,dz);
    if(v){*lx=wx-dx*period;*lz=wz-dz*period;return v;}
    double xs[2]={floor((wx+half)/period),floor((wx+half+cell)/period)};
    double zs[2]={floor((wz+half)/period),floor((wz+half+cell)/period)};
    for(int z=0;z<2;z++)for(int x=0;x<2;x++) {
        v=offset_tile(w,xs[x],zs[z]);
        if(v){*lx=wx-xs[x]*period;*lz=wz-zs[z]*period;return v;}
    }
    return NULL;
}

void qlcg_infinite_bounds(const MacroWorld *w,double b[4]) {
    double cell=(double)w->circumference/w->columns,period=w->circumference+cell;
    double edge=w->circumference*.5+cell;
    const InfiniteWorld *p=w->planes;
    b[0]=(p?(double)p->min_x-w->opt.plane_x:0)*period-edge;
    b[1]=(p?(double)p->min_z-w->opt.plane_z:0)*period-edge;
    b[2]=(p?(double)p->max_x-w->opt.plane_x:0)*period+edge;
    b[3]=(p?(double)p->max_z-w->opt.plane_z:0)*period+edge;
}

bool qlcg_infinite_attach(MacroWorld *w,MacroWorld *v) {
    if(!w->planes) {
        InfiniteWorld *p=calloc(1,sizeof(*p)); if(!p) return false;
        p->capacity=8; p->items=malloc(p->capacity*sizeof(*p->items));
        if(!p->items) { free(p); return false; }
        p->items[0]=w; p->count=1; w->planes=p;
        p->min_x=p->max_x=w->opt.plane_x;p->min_z=p->max_z=w->opt.plane_z;
    }
    InfiniteWorld *p=w->planes;
    if(qlcg_infinite_find(w,v->opt.plane_x,v->opt.plane_z)) return v==w;
    if(p->count==p->capacity) {
        size_t cap=p->capacity*2;
        if(cap<p->capacity || cap>SIZE_MAX/sizeof(*p->items)) return false;
        MacroWorld **next=realloc(p->items,cap*sizeof(*next)); if(!next) return false;
        p->items=next; p->capacity=cap;
    }
    size_t at=tile_at(p,v->opt.plane_x,v->opt.plane_z);
    memmove(p->items+at+1,p->items+at,(p->count-at)*sizeof(*p->items));
    p->items[at]=v; p->count++; v->planes=p;
    if(v->opt.plane_x<p->min_x)p->min_x=v->opt.plane_x;
    if(v->opt.plane_x>p->max_x)p->max_x=v->opt.plane_x;
    if(v->opt.plane_z<p->min_z)p->min_z=v->opt.plane_z;
    if(v->opt.plane_z>p->max_z)p->max_z=v->opt.plane_z;
    return true;
}

static MacroWorld *ensure_neighbor(MacroWorld *w,int dx,int dz) {
    int64_t x=(int64_t)w->opt.plane_x+dx,z=(int64_t)w->opt.plane_z+dz;
    if(x<INT32_MIN || x>INT32_MAX || z<INT32_MIN || z>INT32_MAX) return w;
    MacroWorld *next=qlcg_infinite_find(w,(int32_t)x,(int32_t)z);
    if(!next) {
        WorldCreatorOptions o=w->opt; o.plane_x=(int32_t)x; o.plane_z=(int32_t)z;
        o.seed=qlcg_infinite_seed(o.world_seed,o.plane_x,o.plane_z);
        struct RiverArchive *archive=qlcg_river_archive_create(qlcg_river_archive_terrain(w));
        if(!archive) return NULL;
        next=qlcg_world_generate_with_rivers(&o,archive);
        if(!next) return NULL;
        if(!qlcg_infinite_attach(w,next)) { qlcg_world_free(next); return NULL; }
    }
    return next;
}

MacroWorld *qlcg_infinite_explore(MacroWorld *w,float *wx,float *wz) {
    if(!w || !qlcg_world_flat(w) || !isfinite(*wx) || !isfinite(*wz)) return w;
    float half=w->circumference*.5f,cell=w->circumference/w->columns;
    int dx=*wx>=half?1:*wx<=-half?-1:0;
    int dz=*wz>=half?1:*wz<=-half?-1:0;
    if(!dx && !dz) return w;
    MacroWorld *next=ensure_neighbor(w,dx,dz);
    if(!next || next==w) return next;
    int sx=fabsf(*wx)>=half+cell*.5f?dx:0;
    int sz=fabsf(*wz)>=half+cell*.5f?dz:0;
    if(!sx && !sz) return w;
    if(sx!=dx || sz!=dz) {
        next=ensure_neighbor(w,sx,sz);
        if(!next || next==w) return next;
    }
    *wx-=sx*(w->circumference+cell); *wz-=sz*(w->circumference+cell);
    next->plant_calendar=w->plant_calendar; next->plant_season_forced=w->plant_season_forced;
    next->ice_time=w->ice_time; next->ice_time_valid=w->ice_time_valid;
    next->ice_forced_season=w->ice_forced_season;
    next->astronomy_broadcast=w->astronomy_broadcast;
    return next;
}

void qlcg_infinite_free_others(MacroWorld *w) {
    InfiniteWorld *p=w->planes; if(!p) return;
    for(size_t i=0;i<p->count;i++) p->items[i]->planes=NULL;
    for(size_t i=0;i<p->count;i++) if(p->items[i]!=w) qlcg_world_free(p->items[i]);
    free(p->items); free(p);
}
