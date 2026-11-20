/* 舆图植被：气候代表、实丛与散生乔木 */
#include "atlas_vegetation.h"
#include "../terrain3d/prop_sprites.h"
#include "../systems/world/surface_guild.h"
#include "../systems/world/surface_stream.h"
#include "../systems/world/surface_internal.h"
#include "../systems/world/world_infinite.h"
#include "../systems/world/climate_zone.h"
#include "../systems/world/model/plant_material.h"
#include "../systems/world/model/plant_def.h"
#include "../core/dev_tools.h"
#include "../core/math_util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GUILD_CACHE 4096
#define TREE_CACHE 256
#define VISIT_CAP 8192

typedef struct {
    float x,z;
    int species;
} SingleTree;
typedef struct {
    const MacroWorld *world;
    int x,z,species;
    uint32_t delta,surface;
    float wx,wz;
} GuildMarker;
typedef struct {
    const MacroWorld *world;
    int x,z,count;
    uint32_t delta,surface;
    uint64_t touched;
    SingleTree *trees;
} TreeMarkers;
typedef struct {
    const MacroWorld *world;
    int x,z;
    uint32_t generation;
} Visit;
struct AtlasVegetation {
    PropSprites sprites;
    GuildMarker guilds[GUILD_CACHE];
    TreeMarkers chunks[TREE_CACHE];
    Visit visited[VISIT_CAP];
    uint32_t visit_generation;
    RegionBuildJob *job;
    const MacroWorld *job_world;
    int job_x,job_z;
    uint32_t job_delta,job_surface;
    uint64_t frame,job_frame;
};

int qlcg_atlas_guild_species(const QlcgGuildDef *guild) {
    if(!guild) return -1;
    if(guild->mono) return qlcg_plant_species_category(guild->species)==PLANT_CAT_SHU?guild->species:-1;
    for(int i=0;i<guild->member_count;i++) {
        int species=guild->members[i].species;
        if(qlcg_plant_species_category(species)==PLANT_CAT_SHU) return species;
    }
    return -1;
}

void qlcg_atlas_vegetation_release(AtlasRenderer *ar) {
    struct AtlasVegetation *v=ar->vegetation;
    if(!v) return;
    qlcg_chunk_build_cancel(v->job);
    for(int i=0;i<TREE_CACHE;i++) free(v->chunks[i].trees);
    qlcg_prop_sprites_free(&v->sprites);
    free(v);ar->vegetation=NULL;
}

static uint32_t point_hash(const MacroWorld *w,int x,int z) {
    return qlcg_hash_u32((uint32_t)x*73856093u^(uint32_t)z*19349663u^w->opt.seed);
}

static bool visit(struct AtlasVegetation *v,const MacroWorld *w,int x,int z) {
    unsigned slot=point_hash(w,x,z)%VISIT_CAP;
    for(unsigned i=0;i<VISIT_CAP;i++,slot=(slot+1)%VISIT_CAP) {
        Visit *p=&v->visited[slot];
        if(!p->world || p->generation!=v->visit_generation) { *p=(Visit){w,x,z,v->visit_generation}; return true; }
        if(p->world==w && p->x==x && p->z==z) return false;
    }
    return false;
}

static bool inside(const MacroWorld *w,double x,double z) {
    return x>=-w->circumference*.5 && x<w->circumference*.5 &&
           z>=-qlcg_world_depth(w)*.5 && z<qlcg_world_depth(w)*.5;
}

static bool explored(const MacroWorld *w,double x,double z) {
    return qlcg_world_chunk_explored(w,(int)floor(x/CHUNK_SIZE),(int)floor(z/CHUNK_SIZE));
}

static void icon(struct AtlasVegetation *v,const AtlasRenderer *ar,const MacroWorld *view,
                 const MacroWorld *source,const CalendarSnapshot *date,UiContext *ui,
                 double wx,double wz,double ox,double oz,int species,bool guild,float alpha,
                 float x,float y,float width,float height) {
    if(species<0 || alpha<=.001f || !explored(source,wx,wz)) return;
    float sx,sy;
    if(!qlcg_atlas_world_to_screen_vis(ar,view,wx+ox,wz+oz,x,y,width,height,&sx,&sy)) return;
    int cell=qlcg_world_cell_at_wxz(source,(float)wx,(float)wz);
    int season=qlcg_climate_season_index(source,cell,date);
    PropUvRect uv;
    if(!qlcg_prop_sprites_plant_uv(&v->sprites,species,season,&uv)) return;
    float size=22.0f*ui->met.size*(guild?1.2f:1.0f);
    float h=size,w=size*uv.fw/fmaxf(.01f,uv.fh);
    if(w>size) { h*=size/w;w=size; }
    ui_draw_premult_sprite(ui,sx-w*.5f,sy-h,w,h,uv.u0,uv.v1,uv.u1,uv.v0,
                          ui_rgba(255,255,255,(uint8_t)(255*qlcg_clampf(alpha,0,1))));
}

static GuildMarker *guild_marker(struct AtlasVegetation *v,const MacroWorld *w,int x,int z,double deadline) {
    GuildMarker *p=&v->guilds[point_hash(w,x,z)%GUILD_CACHE];
    if(p->world==w && p->x==x && p->z==z && p->delta==w->delta_version && p->surface==w->surface_revision) return p;
    if(qlcg_time_ms()>=deadline) return NULL;
    *p=(GuildMarker){.world=w,.x=x,.z=z,.species=-1,.delta=w->delta_version,.surface=w->surface_revision};
    QlcgGuildShape shape;
    const QlcgGuildDef *def=qlcg_pg_guild(qlcg_guild_query(w,x,z,&shape));
    int species=qlcg_atlas_guild_species(def);
    if(species<0 || !inside(w,shape.cx,shape.cz)) return p;
    SurfaceMapPoint surface;
    qlcg_sample_surface_detail_map(w,shape.cx,shape.cz,&surface);
    if(surface.h<=surface.wl+.2f) return p;
    p->wx=shape.cx;p->wz=shape.cz;p->species=species;
    return p;
}

static TreeMarkers *tree_markers(struct AtlasVegetation *v,const MacroWorld *w,int x,int z) {
    for(int i=0;i<TREE_CACHE;i++) {
        TreeMarkers *p=&v->chunks[i];
        if(p->world!=w || p->x!=x || p->z!=z) continue;
        if(p->delta!=w->delta_version || p->surface!=w->surface_revision) {
            free(p->trees);memset(p,0,sizeof(*p));return NULL;
        }
        p->touched=v->frame;return p;
    }
    if(v->job && v->job_world==w && v->job_x==x && v->job_z==z) v->job_frame=v->frame;
    if(!v->job) {
        v->job=qlcg_chunk_plants_begin(w,x,z);
        v->job_world=w;v->job_x=x;v->job_z=z;
        v->job_delta=w->delta_version;v->job_surface=w->surface_revision;
        v->job_frame=v->frame;
    }
    return NULL;
}

static void advance_trees(struct AtlasVegetation *v,double deadline) {
    if(!v->job) return;
    const MacroWorld *w=v->job_world;
    if(v->job_delta!=w->delta_version || v->job_surface!=w->surface_revision) {
        qlcg_chunk_build_cancel(v->job);v->job=NULL;return;
    }
    while(qlcg_time_ms()<deadline) {
        if(!qlcg_chunk_build_step(v->job)) continue;
        RegionData *reg=qlcg_chunk_build_take(v->job);v->job=NULL;
        SingleTree *trees=reg->tree_count?malloc((size_t)reg->tree_count*sizeof(*trees)):NULL;
        if(reg->tree_count && !trees) { qlcg_region_free(reg);return; }
        for(int i=0;i<reg->tree_count;i++)
            trees[i]=(SingleTree){reg->trees[i].x,reg->trees[i].z,reg->trees[i].species};
        int slot=0;
        for(int i=0;i<TREE_CACHE;i++)
            if(!v->chunks[i].world) { slot=i;break; }
            else if(v->chunks[i].touched<v->chunks[slot].touched) slot=i;
        TreeMarkers *p=&v->chunks[slot];free(p->trees);
        *p=(TreeMarkers){.world=w,.x=v->job_x,.z=v->job_z,.count=reg->tree_count,
            .delta=v->job_delta,.surface=v->job_surface,.touched=v->frame,.trees=trees};
        qlcg_region_free(reg);
        return;
    }
}

static void draw_grid(struct AtlasVegetation *v,const AtlasRenderer *ar,const MacroWorld *w,
                      const CalendarSnapshot *date,UiContext *ui,int mode,float alpha,double deadline,
                      float x,float y,float width,float height) {
    if(alpha<=.001f) return;
    float scale=ar->planet_view==PLANET_VIEW_GLOBE?
        qlcg_atlas_globe_radius(ar,width,height)*6.2831853f/w->circumference:calc_zoom_scale(ar,w,width);
    double unit=mode==0?w->circumference/w->columns:mode==1?QLCG_GUILD_CELL:CHUNK_SIZE;
    int stride=1;
    double step=unit;
    float spacing=mode==2?110.0f:44.0f;
    if(mode==0) spacing*=ui->met.size;
    while(step*scale<spacing && stride<(1<<24)) { step*=2;stride*=2; }
    if(mode==0) while(step*scale>spacing*2 && step>1) step*=.5;
    if(++v->visit_generation==0) {
        memset(v->visited,0,sizeof(v->visited));v->visit_generation=1;
    }
    int nx=qlcg_clampi((int)ceilf(width/12),1,256),nz=qlcg_clampi((int)ceilf(height/12),1,144);
    double columns[259],rows[147];
    bool planar=ar->planet_view!=PLANET_VIEW_GLOBE;
    if(planar) {
        double half=qlcg_world_depth(w)*.5;
        for(int ix=-1;ix<=nx;ix++) {
            float px=(x+ix*width/nx)-(x+width*.5f);
            double wx=ar->cam_wx+px/(double)scale;
            if(!qlcg_world_flat(w))wx-=floor(wx/w->circumference+.5)*w->circumference;
            columns[ix+1]=wx;
        }
        for(int iz=-1;iz<=nz;iz++) {
            float py=(y+iz*height/nz)-(y+height*.5f);
            double wz=ar->cam_wz+py/(double)scale;
            if(ar->planet_view==PLANET_VIEW_MERCATOR) {
                double merc=fmax(-.98,fmin(.98,wz/half))*1.45;
                wz=(2*atan(exp(merc))-1.5707963267948966)/3.141592653589793*(w->circumference*.5);
            } else if(!qlcg_world_flat(w) && (wz < -half || wz > half))wz=NAN;
            rows[iz+1]=wz;
        }
    }
    for(int iz=-1;iz<=nz;iz++) for(int ix=-1;ix<=nx;ix++) {
        double wx,wz,lx,lz;
        if(planar) {
            wx=columns[ix+1];wz=rows[iz+1];if(!isfinite(wz))continue;
        } else if(!qlcg_atlas_screen_to_world_d(ar,w,x+ix*width/nx,y+iz*height/nz,x,y,width,height,&wx,&wz))continue;
        const MacroWorld *source=qlcg_infinite_resolve(w,wx,wz,&lx,&lz);
        if(!source) continue;
        int gx=(int)floor(lx/step),gz=(int)floor(lz/step);
        if(!visit(v,source,gx,gz)) continue;
        uint32_t hash=point_hash(source,gx,gz);
        double ox=wx-lx,oz=wz-lz;
        double px=(gx+(.15+.7*(hash&65535)/65536.0))*step;
        double pz=(gz+(.15+.7*(hash>>16)/65536.0))*step;
        if(!inside(source,px,pz)) continue;
        int species=-1;
        if(mode==0) {
            int cell=qlcg_world_cell_at_wxz(source,(float)px,(float)pz);
            if(cell<0 || source->water[cell] || !explored(source,px,pz)) continue;
            float h,wl;
            qlcg_sample_surface_map(source,px,pz,&h,&wl,NULL,NULL);
            if(wl>h+.02f) continue;
            int zone=qlcg_climate_zone_cell(source,cell);
            species=qlcg_atlas_guild_species(qlcg_pg_guild(qlcg_pg_pick(zone,-1,hash)));
        } else if(mode==1) {
            int cx=gx*stride+(int)(hash%(unsigned)stride);
            int cz=gz*stride+(int)((hash>>16)%(unsigned)stride);
            GuildMarker *p=guild_marker(v,source,cx,cz,deadline);
            if(!p) continue;
            px=p->wx;pz=p->wz;species=p->species;
        } else {
            int cx=gx*stride+(int)(hash%(unsigned)stride);
            int cz=gz*stride+(int)((hash>>16)%(unsigned)stride);
            if(!inside(source,(cx+.5)*CHUNK_SIZE,(cz+.5)*CHUNK_SIZE) ||
               !explored(source,(cx+.5)*CHUNK_SIZE,(cz+.5)*CHUNK_SIZE)) continue;
            TreeMarkers *p=tree_markers(v,source,cx,cz);
            if(!p) continue;
            int count=stride==1?p->count:(p->count?1:0);
            for(int i=0;i<count;i++) {
                const SingleTree *tree=&p->trees[i];
                icon(v,ar,w,source,date,ui,(cx+.5)*CHUNK_SIZE+tree->x,(cz+.5)*CHUNK_SIZE+tree->z,
                     ox,oz,tree->species,false,alpha,x,y,width,height);
            }
            continue;
        }
        icon(v,ar,w,source,date,ui,px,pz,ox,oz,species,true,alpha,x,y,width,height);
    }
}

void qlcg_atlas_draw_vegetation(AtlasRenderer *ar,const MacroWorld *w,
                               const CalendarClock *clock,UiContext *ui,float opacity,
                               float x,float y,float width,float height) {
    if(!ar->vegetation) {
        ar->vegetation=calloc(1,sizeof(*ar->vegetation));
        if(!ar->vegetation) return;
        if(!qlcg_prop_sprites_init(&ar->vegetation->sprites,qlcg_dev_sprite_dir("src/systems/world/model"))) {
            qlcg_atlas_vegetation_release(ar);return;
        }
    }
    struct AtlasVegetation *v=ar->vegetation;v->frame++;
    CalendarSnapshot date;
    const CalendarSnapshot *local=NULL;
    if(clock) { qlcg_calendar_snapshot(clock,0,&date);local=&date; }
    else local=w->plant_calendar;
    double deadline=qlcg_time_ms()+2.0;
    float detail=qlcg_smoothf(17.5f,20.0f,ar->zoom);
    float singles=qlcg_smoothf(22.0f,24.0f,ar->zoom);
    if(singles>0) advance_trees(v,deadline-1.0);
    else {
        qlcg_chunk_build_cancel(v->job);v->job=NULL;
        for(int i=0;i<TREE_CACHE;i++) {
            free(v->chunks[i].trees);memset(&v->chunks[i],0,sizeof(v->chunks[i]));
        }
    }
    GLuint previous=ui->cur_sprite_tex;
    ui_use_sprite_tex(ui,v->sprites.tex);
    draw_grid(v,ar,w,local,ui,0,opacity*(1-detail),deadline,x,y,width,height);
    draw_grid(v,ar,w,local,ui,1,opacity*detail,deadline,x,y,width,height);
    draw_grid(v,ar,w,local,ui,2,opacity*singles,deadline,x,y,width,height);
    if(v->job && v->job_frame!=v->frame) { qlcg_chunk_build_cancel(v->job);v->job=NULL; }
    ui_flush(ui);ui->cur_sprite_tex=previous;
}
