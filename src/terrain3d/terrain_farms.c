#include "terrain_farms.h"
#include "terrain_types.h"
#include "../systems/world/world_civ_economy.h"
#include "../systems/world/world_farm_surface.h"
#include "../systems/world/climate_season.h"
#include "../systems/world/model/plant_material.h"
#include "../systems/world/model/plant_def.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FARM_VISIBLE_CAP=16,FARM_MODEL_CAP=32,FARM_CROP_CAP=576 };
typedef struct {float p[3],n[3],color[3];} CropVertex;
typedef struct {
    int cx,cz;
    uint64_t serial;
    uint32_t mesh;
    double anchor_x,anchor_z;
    const MacroWorld *world;
    float plot[5];
    PropInstGPU crops[36];
    int crop_count;
    bool crops_valid;
    WaterFormState water_form;
} FarmPlacement;
typedef struct {
    GLuint vao,vbo,ebo,instances;
    char id[80];
    int stage,season,form,index_count,count,last_count;
    uint32_t hash;
    PropInstGPU crops[FARM_CROP_CAP];
} FarmModel;
struct TerrainFarms {
    FarmPlacement placements[FARM_VISIBLE_CAP];
    FarmModel models[FARM_MODEL_CAP];
    int count;
    const MacroWorld *world;
    const RegionStream *stream;
    FarmModel *submitted[FARM_VISIBLE_CAP];
    double anchor_x,anchor_z;
};
typedef struct {QlcgFarmPlot plot;int cx,cz,slot;bool player_owned;float distance;} FarmCandidate;

static void vertex_layout(void) {
    const int attributes[3]={0,1,5};
    const size_t offsets[3]={offsetof(CropVertex,p),offsetof(CropVertex,n),offsetof(CropVertex,color)};
    for(int i=0;i<3;i++) {glEnableVertexAttribArray((GLuint)attributes[i]);glVertexAttribPointer((GLuint)attributes[i],3,GL_FLOAT,GL_FALSE,sizeof(CropVertex),(void *)offsets[i]);}
}

static void placement_prepare(FarmPlacement *g,const FarmCandidate *candidate,const RegionData *chunk,TerrainRenderer *tr,const MacroWorld *w) {
    g->cx=candidate->cx;g->cz=candidate->cz;
    g->serial=tr->bound_stream?tr->bound_stream->slots[candidate->slot].serial:0;
    g->mesh=chunk->mesh_revision;g->anchor_x=tr->anchor_x;g->anchor_z=tr->anchor_z;g->world=w;
    g->plot[0]=candidate->plot.x;g->plot[1]=candidate->plot.z;g->plot[2]=candidate->plot.width;g->plot[3]=candidate->plot.depth;g->plot[4]=candidate->plot.yaw;
    g->crops_valid=false;g->water_form=chunk->water_form;
}

static bool placement_matches(const FarmPlacement *g,const FarmCandidate *candidate,const RegionData *chunk,const TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w) {
    return g->world==w&&g->cx==candidate->cx&&g->cz==candidate->cz&&g->serial==reg->stream->slots[candidate->slot].serial&&
        g->mesh==chunk->mesh_revision&&g->water_form==chunk->water_form&&g->anchor_x==tr->anchor_x&&g->anchor_z==tr->anchor_z&&
        g->plot[0]==candidate->plot.x&&g->plot[1]==candidate->plot.z&&g->plot[2]==candidate->plot.width&&g->plot[3]==candidate->plot.depth&&g->plot[4]==candidate->plot.yaw;
}

static bool farm_dry(const struct TerrainFarms *cache,const FarmCandidate *candidate,const RegionData *chunk,const TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w) {
    for(int i=0;i<FARM_VISIBLE_CAP;i++) if(placement_matches(cache->placements+i,candidate,chunk,tr,reg,w)) return true;
    if(!chunk->water_levels)return true;
    float points[QLCG_FARM_FOOTPRINT_POINTS][2];
    qlcg_farm_surface_footprint(&candidate->plot,points);
    for(int k=0;k<QLCG_FARM_FOOTPRINT_POINTS;k++) {
        float x=points[k][0],z=points[k][1];
        float ground=qlcg_region_height_at(chunk,x,z),water=qlcg_region_water_at(chunk,x,z);
        if(!qlcg_farm_surface_dry(ground,water))return false;
    }
    return true;
}

static void crop_positions(FarmPlacement *g,const FarmCandidate *candidate,const RegionData *chunk,const TerrainRenderer *tr) {
    if(g->crops_valid) return;
    g->crop_count=0;
    float c=cosf(candidate->plot.yaw),s=sinf(candidate->plot.yaw);
    for(int z=0;z<6;z++) for(int x=0;x<6;x++) {
        float u=(x-2.5f)*candidate->plot.width/6,v=(z-2.5f)*candidate->plot.depth/6;
        float lx=candidate->plot.x+u*c-v*s,lz=candidate->plot.z+u*s+v*c;
        float y=qlcg_region_height_at(chunk,lx,lz);if(!isfinite(y))continue;
        PropInstGPU p={0};p.x=(float)((double)chunk->center_x-tr->anchor_x)+lx;p.y=y;p.z=(float)((double)chunk->center_z-tr->anchor_z)+lz;
        p.sx=p.sy=p.sz=1;p.rot=candidate->plot.yaw;g->crops[g->crop_count++]=p;
    }
    g->crops_valid=true;
}

static bool crop_model_matches(const FarmModel *model,const QlcgFarmPlot *plot,bool player_owned,int form) {
    int stage=player_owned?plot->stage:PD_STATE_NORMAL;
    if(stage<=PD_STATE_PLANTED || !plot->plant_id) return model==NULL;
    int season=player_owned?PD_SEASON_NONE:plot->season;
    return model && model->vao && model->form==form && model->stage==stage && model->season==season &&
        !strcmp(model->id,plot->plant_id);
}

static FarmModel *crop_model(struct TerrainFarms *cache,const QlcgFarmPlot *plot,bool player_owned,int form) {
    int stage=player_owned?plot->stage:PD_STATE_NORMAL;
    int season=player_owned?PD_SEASON_NONE:plot->season;
    if(stage<=PD_STATE_PLANTED||!plot->plant_id) return NULL;
    FarmModel *slot=NULL;
    for(int i=0;i<FARM_MODEL_CAP;i++) {
        FarmModel *m=cache->models+i;
        if(crop_model_matches(m,plot,player_owned,form)) return m;
        if(!m->vao&&!slot) slot=m;
    }
    if(!slot) for(int i=0;i<FARM_MODEL_CAP;i++) if(!cache->models[i].count) {slot=cache->models+i;break;}
    if(!slot) return NULL;
    qlcg_gl_delete_vertex_arrays(1,&slot->vao);GLuint old[3]={slot->vbo,slot->ebo,slot->instances};qlcg_gl_delete_buffers(3,old);memset(slot,0,sizeof(*slot));
    LpGeometry geometry={0};
    int built=player_owned?qlcg_plant_build_growth(plot->plant_id,stage,form,&geometry)
                          :qlcg_plant_build(plot->plant_id,season,form,&geometry);
    if(!built) return NULL;
    CropVertex *vertices=calloc((size_t)geometry.vertex_count,sizeof(*vertices));
    if(!vertices) {qlcg_lp_geometry_free(&geometry);return NULL;}
    for(int i=0;i<geometry.vertex_count;i++) for(int k=0;k<3;k++) {
        vertices[i].p[k]=geometry.positions[i*3+k];vertices[i].n[k]=geometry.normals[i*3+k];
        vertices[i].color[k]=geometry.colors[i*3+k]/255.0f;
    }
    glGenVertexArrays(1,&slot->vao);glGenBuffers(1,&slot->vbo);glGenBuffers(1,&slot->ebo);glGenBuffers(1,&slot->instances);
    glBindVertexArray(slot->vao);glBindBuffer(GL_ARRAY_BUFFER,slot->vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)((size_t)geometry.vertex_count*sizeof(*vertices)),vertices,GL_STATIC_DRAW);vertex_layout();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,slot->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)((size_t)geometry.index_count*sizeof(uint32_t)),geometry.indices,GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,slot->instances);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(PropInstGPU),(void *)offsetof(PropInstGPU,x));
    glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,sizeof(PropInstGPU),(void *)offsetof(PropInstGPU,sx));
    glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,sizeof(PropInstGPU),(void *)offsetof(PropInstGPU,rot));
    for(int i=2;i<=4;i++) {glEnableVertexAttribArray((GLuint)i);glVertexAttribDivisor((GLuint)i,1);}
    slot->index_count=geometry.index_count;slot->stage=stage;slot->season=season;slot->form=form;
    snprintf(slot->id,sizeof(slot->id),"%s",plot->plant_id);
    free(vertices);qlcg_lp_geometry_free(&geometry);return slot;
}

static void consider_farm(struct TerrainFarms *cache,FarmCandidate *candidates,int *count,FarmCandidate candidate,
                          const RegionData *chunk,TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w) {
    if(*count==FARM_VISIBLE_CAP&&candidate.distance>=candidates[*count-1].distance) return;
    if(!farm_dry(cache,&candidate,chunk,tr,reg,w))return;
    int k=*count;if(k==FARM_VISIBLE_CAP) k--; else (*count)++;
    while(k>0&&candidate.distance<candidates[k-1].distance) {candidates[k]=candidates[k-1];k--;}
    candidates[k]=candidate;
}

static int farm_slot_order(const void *a,const void *b) {
    return ((const FarmCandidate *)a)->slot-((const FarmCandidate *)b)->slot;
}

void qlcg_terrain_farms_prepare(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w,int vegetation_form) {
    RegionStream *stream=reg->stream;
    if(!tr->farms) {
        if(!stream || !w || !w->opt.artificial_enabled) return;
        tr->farms=calloc(1,sizeof(*tr->farms));
        if(!tr->farms) return;
    }
    struct TerrainFarms *cache=tr->farms;
    if(cache->world!=w || cache->stream!=stream) {
        memset(cache->placements,0,sizeof(cache->placements));
        cache->world=w;
        cache->stream=stream;
    }
    FarmCandidate candidates[FARM_VISIBLE_CAP];
    int count=0,form=qlcg_plant_model_form(vegetation_form,0);
    if(stream && w && w->opt.artificial_enabled) for(int i=0;i<stream->count;i++) {
        int slot=stream->active[i];
        const RegionChunkSlot *resident=&stream->slots[slot];
        const RegionData *chunk=resident->data;
        if(!chunk || chunk->site_chunk_type<QLCG_CHUNK_FARM_FIRST || chunk->site_chunk_type>QLCG_CHUNK_FARM_LAST ||
           !tr->chunks[slot].terrain_ready || tr->chunks[slot].serial!=resident->serial) continue;
        FarmCandidate candidate={.cx=chunk->center_cx,.cz=chunk->center_cz,.slot=slot};
        float dx=(candidate.cx-reg->center_cx)*(float)CHUNK_SIZE-tr->x;
        float dz=(candidate.cz-reg->center_cz)*(float)CHUNK_SIZE-tr->z;
        candidate.distance=dx*dx+dz*dz;
        if(candidate.distance>256*256) continue;
        QlcgCivTerritory territory;
        if(!qlcg_civ_territory_at(w,candidate.cx,candidate.cz,&territory) ||
           !qlcg_civ_territory_farm(&territory,&candidate.plot)) continue;
        candidate.player_owned=territory.faction->is_player;
        if(!candidate.player_owned) {
            if(w->plant_season_forced) candidate.plot.season=tr->plant_season;
            else if(territory.owner->plant_calendar)
                candidate.plot.season=qlcg_climate_season_index(territory.owner,territory.cell,territory.owner->plant_calendar);
        }
        consider_farm(cache,candidates,&count,candidate,chunk,tr,reg,w);
    }

    /* 相机穿行农田时保持稳定合批顺序 */
    if(count>1) qsort(candidates,(size_t)count,sizeof(*candidates),farm_slot_order);
    bool changed=cache->count!=count;
    for(int i=0;i<count && !changed;i++) {
        const FarmCandidate *candidate=candidates+i;
        const RegionData *chunk=stream->slots[candidate->slot].data;
        changed=!placement_matches(cache->placements+i,candidate,chunk,tr,reg,w) ||
                !crop_model_matches(cache->submitted[i],&candidate->plot,candidate->player_owned,form);
    }
    if(!changed) return;
    cache->count=count;
    cache->anchor_x=tr->anchor_x;
    cache->anchor_z=tr->anchor_z;
    for(int i=0;i<FARM_MODEL_CAP;i++) cache->models[i].count=0;
    for(int i=0;i<count;i++) {
        const FarmCandidate *candidate=candidates+i;
        const RegionData *chunk=stream->slots[candidate->slot].data;
        FarmPlacement *g=cache->placements+i;
        if(!placement_matches(g,candidate,chunk,tr,reg,w)) placement_prepare(g,candidate,chunk,tr,w);
        FarmModel *model=crop_model(cache,&candidate->plot,candidate->player_owned,form);
        cache->submitted[i]=model;
        if(!model) continue;
        crop_positions(g,candidate,chunk,tr);
        for(int j=0;j<g->crop_count&&model->count<FARM_CROP_CAP;j++) model->crops[model->count++]=g->crops[j];
    }
    for(int i=0;i<FARM_MODEL_CAP;i++) {
        FarmModel *m=cache->models+i;if(!m->count) continue;
        uint32_t hash=2166136261u;const unsigned char *data=(const unsigned char *)m->crops;size_t bytes=(size_t)m->count*sizeof(*m->crops);
        for(size_t j=0;j<bytes;j++) hash=(hash^data[j])*16777619u;
        if(m->last_count!=m->count||m->hash!=hash) {glBindBuffer(GL_ARRAY_BUFFER,m->instances);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)bytes,m->crops,GL_DYNAMIC_DRAW);m->last_count=m->count;m->hash=hash;}
    }
}

void qlcg_terrain_farms_draw(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w) {
    struct TerrainFarms *cache=tr->farms;
    if(!cache || !w || cache->world!=w || !w->opt.artificial_enabled || !reg->stream) return;
    if(cache->stream!=reg->stream||cache->anchor_x!=tr->anchor_x||cache->anchor_z!=tr->anchor_z) return;
    for(int i=0;i<FARM_MODEL_CAP;i++) {const FarmModel *m=cache->models+i;if(m->count) {glBindVertexArray(m->vao);glDrawElementsInstanced(GL_TRIANGLES,m->index_count,GL_UNSIGNED_INT,0,m->count);}}
}

void qlcg_terrain_farms_free(TerrainRenderer *tr) {
    if(!tr->farms)return;
    for(int i=0;i<FARM_MODEL_CAP;i++) {FarmModel *m=tr->farms->models+i;qlcg_gl_delete_vertex_arrays(1,&m->vao);GLuint buffers[3]={m->vbo,m->ebo,m->instances};qlcg_gl_delete_buffers(3,buffers);}
    free(tr->farms);tr->farms=NULL;
}
