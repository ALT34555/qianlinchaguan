#include "systems/world/world_lot.h"
#include "systems/world/model/editor_buildings.h"
#include <limits.h>
#include "terrain_settlement.h"
#include "../systems/world/world_settlement.h"
#include "../systems/world/world_delta.h"
#include "../systems/world/model/building_model.h"
#include "../systems/world/model/plant_forms.h"
#include "../systems/world/climate_season.h"
#include "../systems/world/world_infinite.h"
#include "../systems/dolls/doll_life.h"
#include <math.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

typedef struct { float p[3],n[3],scale[3],zero[4];unsigned char color[3]; } SettlementVertex;
typedef struct {
    GLuint vao,vbo;
    int count, building_count, season;
    bool has_plants,has_chair;
    float chair_pose[3];
    uint64_t serial;
    uint32_t mesh,territory;
    double anchor_x,anchor_z;
    const MacroWorld *world;
    const TerritoryFaction *faction;
} SettlementChunk;
typedef struct SettlementPlant {
    const char *id;
    LpGeometry geometry[4];
    unsigned attempted;
    struct SettlementPlant *next;
} SettlementPlant;
struct TerrainSettlementCache {
    SettlementChunk chunks[QLCG_STREAM_CAP];
    int slots[QLCG_STREAM_CAP],count;
    SettlementPlant *plants;
    LpGeometry chair;
};

static void build_chair(LpGeometry *geometry) {
    LpBuilder b;qlcg_lp_init(&b);
    LpRgb frame=qlcg_lp_hex("89603B",137,96,59),seat=qlcg_lp_hex("AA7849",170,120,73);
    double height=QLCG_DOLL_CHAIR_HEIGHT;
    for(int x=-1;x<=1;x+=2) for(int z=-1;z<=1;z+=2) {
        double top=z<0?height+.40:height;
        qlcg_lp_box(&b,(double[]){x*.19,top*.5,z*.18},(double[]){.0225,top*.5,.0225},frame,NULL);
    }
    qlcg_lp_box(&b,(double[]){0,height-.022,0},(double[]){.23,.022,.22},seat,NULL);
    for(int i=0;i<2;i++)
        qlcg_lp_box(&b,(double[]){0,height+.19+i*.145,-.18},(double[]){.19,.0325,.019},frame,NULL);
    const double table=QLCG_LIFE_TEA_TABLE_M,table_height=.44;
    for(int x=-1;x<=1;x+=2) for(int z=-1;z<=1;z+=2)
        qlcg_lp_box(&b,(double[]){x*.235,table_height*.5,table+z*.15},(double[]){.0225,table_height*.5,.0225},frame,NULL);
    qlcg_lp_box(&b,(double[]){0,table_height,table},(double[]){.27,.0225,.19},seat,NULL);
    LpRgb cup=qlcg_lp_hex("CCD3B6",204,211,182);
    for(int i=-1;i<=1;i+=2)
        qlcg_lp_prism(&b,(double[]){i*.14,0,table},.038,.038,table_height+.0225,table_height+.074,8,cup,NULL);
    qlcg_lp_build(&b,geometry);qlcg_lp_free(&b);
}

static const QlcgDollLifeCamp *camp_life(const MacroWorld *world,const RegionData *chunk,const TerritoryFaction *f) {
    if(!f) return NULL;
    double x,z;
    const MacroWorld *owner=qlcg_infinite_resolve(world,chunk->center_x,chunk->center_z,&x,&z);
    QlcgSettlementPart chair;
    if(owner && (qlcg_lot_local(owner,f->cx,f->cz) || qlcg_lot_facility(owner,f,true,&chair))) return NULL;
    return owner?qlcg_doll_life_camp(owner,f):NULL;
}

static bool chair_changed(const SettlementChunk *gpu,const QlcgDollLifeCamp *life) {
    if(gpu->has_chair!=(life!=NULL)) return true;
    return life&&(gpu->chair_pose[0]!=life->chair_x||gpu->chair_pose[1]!=life->chair_z||gpu->chair_pose[2]!=life->chair_yaw);
}

static int settlement_season(const MacroWorld *world,const RegionData *chunk,
                             const TerrainRenderer *tr) {
    if(world->plant_season_forced || !world->plant_calendar) return tr->plant_season;
    return qlcg_climate_season_index(world,chunk->site_cell,world->plant_calendar);
}

static const LpGeometry *plant_geometry(SettlementPlant *plant,int season) {
    if(!(plant->attempted & (1u << season))) {
        plant->attempted |= 1u << season;
        qlcg_plant_build(plant->id,season,VEG_FORM_NORMAL,&plant->geometry[season]);
    }
    return &plant->geometry[season];
}

static SettlementPlant *settlement_plant(struct TerrainSettlementCache *cache,const char *id) {
    for(SettlementPlant *p=cache->plants;p;p=p->next) {
        if(!strcmp(p->id,id)) return p;
    }
    SettlementPlant *plant=calloc(1,sizeof(*plant));
    if(!plant) return NULL;
    plant->id=id;
    plant->next=cache->plants;
    cache->plants=plant;
    return plant;
}

static void append_plants(SettlementChunk *gpu,SettlementVertex *vertices,TerrainRenderer *tr,
                          const RegionData *chunk,const QlcgSettlementPart *part,
                          float base,float dx,float dz) {
    const QlcgBuildingModel *model=qlcg_lot_model(part->model);
    float c=cosf(part->yaw),s=sinf(part->yaw);
    for(int i=0;i<model->socket_count;i++) {
        const QlcgBuildingSocket *socket=&model->sockets[i];
        if(!socket->plant_model) continue;
        SettlementPlant *plant=settlement_plant(tr->settlements,socket->plant_model);
        if(!plant) continue;
        const LpGeometry *summer=plant_geometry(plant,1);
        const LpGeometry *g=plant_geometry(plant,gpu->season);
        if(summer->height<=0 || !g->index_count) continue;
        float scale=part->scale*socket->plant_height/summer->height;
        float local_x=(float)(socket->position[0]*c-socket->position[2]*s)*part->scale;
        float local_z=(float)(socket->position[0]*s+socket->position[2]*c)*part->scale;
        float x=(float)((double)chunk->center_x-tr->anchor_x)+part->x+local_x;
        float z=(float)((double)chunk->center_z-tr->anchor_z)+part->z+local_z;
        float y=base+part->y+(float)socket->position[1]*part->scale+local_x*dx+local_z*dz;
        const double *q=socket->quaternion;
        float angle=part->yaw-(float)atan2(2*(q[3]*q[1]+q[0]*q[2]),1-2*(q[1]*q[1]+q[0]*q[0]));
        float pc=cosf(angle),ps=sinf(angle);
        for(int j=0;j<g->index_count;j++) {
            unsigned k=g->indices[j]*3;
            const float *p=g->positions+k,*n=g->normals+k;
            SettlementVertex *v=vertices+gpu->count++;
            *v=(SettlementVertex){
                .p={x+(p[0]*pc-p[2]*ps)*scale,y+p[1]*scale,z+(p[0]*ps+p[2]*pc)*scale},
                .n={n[0]*pc-n[2]*ps,n[1],n[0]*ps+n[2]*pc},
                .scale={1,1,1},
                .color={g->colors[k],g->colors[k+1],g->colors[k+2]},
            };
        }
    }
}

static void append_building(SettlementChunk *gpu,SettlementVertex *vertices,TerrainRenderer *tr,
                            const RegionData *chunk,const QlcgBuildingModel *model,QlcgSettlementPart p,
                            float base,float dx,float dz) {
    const LpGeometry *g=model->geometry;
    float c=cosf(p.yaw),s=sinf(p.yaw);
    int first=gpu->count;
    for(int j=0;j<g->index_count;j++) {
        int k=(int)g->indices[j];SettlementVertex *v=vertices+gpu->count++;
        float x=g->positions[k*3]*c-g->positions[k*3+2]*s,z=g->positions[k*3]*s+g->positions[k*3+2]*c;
        x*=p.scale;z*=p.scale;
        v->p[0]=(float)((double)chunk->center_x-tr->anchor_x)+p.x+x;
        v->p[1]=base+p.y+g->positions[k*3+1]*p.scale+x*dx+z*dz;
        v->p[2]=(float)((double)chunk->center_z-tr->anchor_z)+p.z+z;
        float nx=g->normals[k*3]*c-g->normals[k*3+2]*s,ny=g->normals[k*3+1],nz=g->normals[k*3]*s+g->normals[k*3+2]*c;
        v->n[0]=nx-dx*ny;v->n[1]=ny;v->n[2]=nz-dz*ny;
        for(int q=0;q<3;q++) {v->color[q]=g->colors[k*3+q];v->scale[q]=1;}
        for(int q=0;q<4;q++) v->zero[q]=0;
    }
    for(int j=0;j<model->draw_count;j++) {
        QlcgBuildingDraw draw=model->draws[j];
        if(draw.side==0)continue;
        for(int k=0;k<draw.count;k+=3) {
            SettlementVertex *front=vertices+first+draw.start+k;
            SettlementVertex back[]={front[2],front[1],front[0]};
            for(int t=0;t<3;t++)for(int a=0;a<3;a++)back[t].n[a]=-back[t].n[a];
            if(draw.side==2) {
                memcpy(vertices+gpu->count,back,sizeof(back));
                gpu->count+=3;
            } else memcpy(front,back,sizeof(back));
        }
    }
}

typedef struct {
    SettlementChunk *gpu;
    SettlementVertex *vertices;
    TerrainRenderer *renderer;
    const RegionData *chunk;
    const QlcgBuildingModel *model;
} WorkbenchMeshes;

static void append_workbench(float wx,float wz,int kind,int state,void *context) {
    (void)kind; (void)state;
    WorkbenchMeshes *draw=context;
    const RegionData *chunk=draw->chunk;
    QlcgSettlementPart part={.x=wx-chunk->center_x,.z=wz-chunk->center_z,.scale=QLCG_WORKBENCH_SCALE};
    float base=qlcg_region_height_at(chunk,part.x,part.z);
    if(isfinite(base)) append_building(draw->gpu,draw->vertices,draw->renderer,chunk,draw->model,part,base,0,0);
}

static void build_chunk(SettlementChunk *gpu,const RegionData *chunk,const MacroWorld *world,const TerritoryFaction *f,
                        const QlcgDollLifeCamp *life,TerrainRenderer *tr) {
    gpu->count=0;
    gpu->building_count=0;
    gpu->has_plants=false;
    gpu->faction=f;
    unsigned benches=qlcg_chd_workbench_count(world,chunk->center_cx,chunk->center_cz);
    const QlcgLot *lot=qlcg_lot_at(world,chunk->center_cx,chunk->center_cz);
    if(!f && !lot && !benches) return;
    qlcg_lot_models_init();
    const QlcgBuildingModel *bench=benches?qlcg_building_model_find(QLCG_WORKBENCH_MODEL):NULL;
    QlcgSettlementPart generated[QLCG_SETTLEMENT_PARTS];
    const QlcgSettlementPart *parts=lot?lot->parts:generated;
    int count=lot?lot->count:qlcg_settlement_layout(f,generated);
    size_t capacity=bench?(int)benches*bench->geometry->index_count:0;
    LpGeometry *chair=&tr->settlements->chair;
    if(life) {
        if(!chair->index_count) build_chair(chair);
        capacity+=chair->index_count;
    }
    if(bench) for(int j=0;j<bench->draw_count;j++) if(bench->draws[j].side==2)
        capacity+=(int)benches*bench->draws[j].count;
    for(int i=0;i<count;i++) {
        const QlcgBuildingModel *m=qlcg_lot_model(parts[i].model);
        if(!qlcg_extra_load(parts[i].model)) return;
        capacity+=m->geometry->index_count;
        for(int j=0;j<m->draw_count;j++)if(m->draws[j].side==2)capacity+=m->draws[j].count;
        for(int j=0;j<m->socket_count;j++) {
            const char *id=m->sockets[j].plant_model;
            if(!id) continue;
            SettlementPlant *plant=settlement_plant(tr->settlements,id);
            if(!plant) return;
            gpu->has_plants=true;
            capacity+=plant_geometry(plant,gpu->season)->index_count;
        }
    }
    if(capacity>INT_MAX || capacity>SIZE_MAX/sizeof(SettlementVertex)) return;
    SettlementVertex *vertices=malloc((size_t)capacity*sizeof(*vertices));
    if(!vertices) return;
    float *bases=count?malloc((size_t)count*3*sizeof(float)):NULL;
    if(count && !bases) {free(vertices);return;}
    float *baseY=bases,*slopeX=count?bases+count:NULL,*slopeZ=count?bases+2*count:NULL;
    for(int i=0;i<count;i++) {
        QlcgSettlementPart p=parts[i];
        const QlcgBuildingModel *model=qlcg_lot_model(p.model);
        float base,dx,dz;
        if(p.parent<0) {
            if(!qlcg_settlement_base(chunk,p.x,p.z,&base,&dx,&dz))base=NAN;
            if(p.flags&QLCG_PART_FLAT) dx=dz=0;
        } else {
            int parent=p.parent;
            dx=slopeX[parent];dz=slopeZ[parent];
            base=baseY[parent]+(p.x-parts[parent].x)*dx+(p.z-parts[parent].z)*dz;
        }
        baseY[i]=base;slopeX[i]=dx;slopeZ[i]=dz;
        if(!isfinite(base))continue;
        append_building(gpu,vertices,tr,chunk,model,p,base,dx,dz);
    }
    if(bench) {
        WorkbenchMeshes meshes={gpu,vertices,tr,chunk,bench};
        qlcg_chd_visit_workbenches(world,chunk->center_cx,chunk->center_cz,append_workbench,&meshes);
    }
    if(life&&chair->index_count) {
        QlcgBuildingModel chair_model={.geometry=chair};
        QlcgSettlementPart part={.x=life->chair_x,.z=life->chair_z,.yaw=-life->chair_yaw,.scale=1};
        float base=qlcg_region_height_at(chunk,part.x,part.z);
        if(isfinite(base)) append_building(gpu,vertices,tr,chunk,&chair_model,part,base,0,0);
    }
    gpu->building_count=gpu->count;
    for(int i=0;i<count;i++) {
        if(isfinite(baseY[i])) append_plants(gpu,vertices,tr,chunk,&parts[i],baseY[i],slopeX[i],slopeZ[i]);
    }
    free(bases);
    if(!gpu->vao) {
        glGenVertexArrays(1,&gpu->vao);glGenBuffers(1,&gpu->vbo);
        glBindVertexArray(gpu->vao);glBindBuffer(GL_ARRAY_BUFFER,gpu->vbo);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(SettlementVertex),(void *)offsetof(SettlementVertex,p));
        glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(SettlementVertex),(void *)offsetof(SettlementVertex,n));
        glEnableVertexAttribArray(2);glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(SettlementVertex),(void *)offsetof(SettlementVertex,zero));
        glEnableVertexAttribArray(4);glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,sizeof(SettlementVertex),(void *)(offsetof(SettlementVertex,zero)+3*sizeof(float)));
        glEnableVertexAttribArray(3);glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,sizeof(SettlementVertex),(void *)offsetof(SettlementVertex,scale));
        glEnableVertexAttribArray(5);glVertexAttribPointer(5,3,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(SettlementVertex),(void *)offsetof(SettlementVertex,color));
    }
    glBindBuffer(GL_ARRAY_BUFFER,gpu->vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)((size_t)gpu->count*sizeof(*vertices)),vertices,GL_STATIC_DRAW);
    free(vertices);
}

int qlcg_terrain_settlement_visible(const TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w,int *slots,const TerritoryFaction **factions,int capacity) {
    if(!w||!w->opt.artificial_enabled||!reg||!reg->stream||!tr->settlements) return 0;
    int count=0;
    for(int i=0;i<tr->settlements->count&&count<capacity;i++) {
        int slot=tr->settlements->slots[i];const SettlementChunk *g=tr->settlements->chunks+slot;
        const RegionData *chunk=reg->stream->slots[slot].data;
        if(!chunk||!tr->chunks[slot].terrain_ready||g->world!=w||g->serial!=reg->stream->slots[slot].serial) continue;
        if(g->mesh!=chunk->mesh_revision||g->territory!=w->territory_revision||!g->faction||!g->faction->active) continue;
        slots[count]=slot;factions[count++]=g->faction;
    }
    return count;
}

bool qlcg_terrain_settlement_needs_refresh(const TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w) {
    if(!w||!w->opt.artificial_enabled||!reg->stream||!tr->settlements) return false;
    for(int i=0;i<tr->settlements->count;i++) {
        int slot=tr->settlements->slots[i];
        const SettlementChunk *gpu=&tr->settlements->chunks[slot];
        const RegionData *chunk=reg->stream->slots[slot].data;
        if(gpu->world!=w||!chunk||gpu->serial!=reg->stream->slots[slot].serial) continue;
        if((gpu->has_plants&&gpu->season!=settlement_season(w,chunk,tr))||chair_changed(gpu,camp_life(w,chunk,gpu->faction)))
            return true;
    }
    return false;
}

void qlcg_terrain_settlement_begin(TerrainRenderer *tr) {
    if(!tr->settlements) tr->settlements=calloc(1,sizeof(*tr->settlements));
    if(tr->settlements) tr->settlements->count=0;
}

void qlcg_terrain_settlement_add(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w,int slot) {
    if(!tr->settlements || tr->settlements->count>=QLCG_STREAM_CAP) return;
    RegionStream *st=reg->stream;
    const RegionData *chunk=st->slots[slot].data;
    SettlementChunk *gpu=tr->settlements->chunks+slot;
    int season=settlement_season(w,chunk,tr);
    const TerritoryFaction *f=qlcg_settlement_chunk(w,chunk->center_cx,chunk->center_cz);
    const QlcgDollLifeCamp *life=camp_life(w,chunk,f);
    if(gpu->world!=w||gpu->serial!=st->slots[slot].serial||gpu->mesh!=chunk->mesh_revision||gpu->territory!=w->territory_revision||gpu->anchor_x!=tr->anchor_x||gpu->anchor_z!=tr->anchor_z||(gpu->has_plants&&gpu->season!=season)||chair_changed(gpu,life)) {
        gpu->season=season;
        build_chunk(gpu,chunk,w,f,life,tr);
        gpu->has_chair=life!=NULL;
        if(life) {
            gpu->chair_pose[0]=life->chair_x;gpu->chair_pose[1]=life->chair_z;gpu->chair_pose[2]=life->chair_yaw;
        }
        gpu->world=w;gpu->serial=st->slots[slot].serial;gpu->mesh=chunk->mesh_revision;gpu->territory=w->territory_revision;
        gpu->anchor_x=tr->anchor_x;gpu->anchor_z=tr->anchor_z;
    }
    if(gpu->count || f) tr->settlements->slots[tr->settlements->count++]=slot;
}

void qlcg_terrain_settlement_prepare_editor(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *world) {
    RegionStream *stream=reg->stream;
    if(!stream || !tr->chunks) return;
    qlcg_terrain_settlement_begin(tr);
    for(int i=0;i<stream->count;i++) {
        int slot=stream->active[i];
        const RegionData *chunk=stream->slots[slot].data;
        if(!chunk || !tr->chunks[slot].terrain_ready) continue;
        if(chunk->flag_count || qlcg_lot_at(world,chunk->center_cx,chunk->center_cz) ||
           qlcg_chd_workbench_count(world,chunk->center_cx,chunk->center_cz))
            qlcg_terrain_settlement_add(tr,reg,world,slot);
    }
}

void qlcg_terrain_settlement_lights(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *world,float night) {
    tr->lot_light_count=0;
    if(night<=0 || !tr->settlements || !reg->stream) return;
    float dist2[4];
    for(int i=0;i<tr->settlements->count;i++) {
        int slot=tr->settlements->slots[i];
        const RegionData *chunk=reg->stream->slots[slot].data;
        if(!chunk) continue;
        const QlcgLot *lot=qlcg_lot_at(world,chunk->center_cx,chunk->center_cz);
        if(!lot) continue;
        for(int j=0;j<lot->count;j++) {
            const QlcgSettlementPart *part=&lot->parts[j];
            const QlcgBuildingModel *model=qlcg_lot_model(part->model);
            float x=part->x,z=part->z,y=1.2f*part->scale;
            if(part->model!=LOT_LAMP) {
                int k=0;
                while(k<model->socket_count && strcmp(model->sockets[k].name,"socket_fire")) k++;
                if(k==model->socket_count) continue;
                const double *p=model->sockets[k].position;
                float cs=cosf(part->yaw)*part->scale,sn=sinf(part->yaw)*part->scale;
                x+=(float)(cs*p[0]-sn*p[2]);z+=(float)(sn*p[0]+cs*p[2]);
                y=(float)(p[1]*part->scale)+.35f;
            }
            y+=qlcg_region_height_at(chunk,part->x,part->z)+part->y-tr->frame_camera[1];
            x+=(chunk->center_cx-reg->center_cx)*32-tr->frame_camera[0];
            z+=(chunk->center_cz-reg->center_cz)*32-tr->frame_camera[2];
            float d=x*x+y*y+z*z;
            int n=tr->lot_light_count;
            if(d>120*120 || (n==4 && d>=dist2[3])) continue;
            int at=n<4?n:3;
            while(at && d<dist2[at-1]) {
                dist2[at]=dist2[at-1];memcpy(tr->lot_lights[at],tr->lot_lights[at-1],sizeof(tr->lot_lights[at]));at--;
            }
            dist2[at]=d;memcpy(tr->lot_lights[at],(float[]){x,y,z,night},sizeof(tr->lot_lights[at]));
            if(n<4) tr->lot_light_count++;
        }
    }
}

void qlcg_terrain_settlement_draw(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w,GLuint program) {
    if(!reg->stream||!tr->settlements||!tr->settlements->count) return;
    GLboolean culling=glIsEnabled(GL_CULL_FACE);
    glEnable(GL_CULL_FACE);
    glUniform1f(qlcg_gl_uniform(program,"uWind"),0);
    for(int i=0;i<tr->settlements->count;i++) {
        int slot=tr->settlements->slots[i];
        SettlementChunk *gpu=tr->settlements->chunks+slot;
        const RegionData *chunk=reg->stream->slots[slot].data;
        if(!chunk||!tr->chunks[slot].terrain_ready||gpu->mesh!=chunk->mesh_revision||gpu->territory!=w->territory_revision) continue;
        if(gpu->world!=w||gpu->serial!=reg->stream->slots[slot].serial||gpu->anchor_x!=tr->anchor_x||gpu->anchor_z!=tr->anchor_z) continue;
        glBindVertexArray(gpu->vao);
        glDrawArrays(GL_TRIANGLES,0,gpu->building_count);
        if(gpu->count>gpu->building_count) {
            glDisable(GL_CULL_FACE);
            glDrawArrays(GL_TRIANGLES,gpu->building_count,gpu->count-gpu->building_count);
            glEnable(GL_CULL_FACE);
        }
    }
    if(!culling)glDisable(GL_CULL_FACE);
}

void qlcg_terrain_settlement_free(TerrainRenderer *tr) {
    if(!tr->settlements) return;
    for(int i=0;i<QLCG_STREAM_CAP;i++) {
        SettlementChunk *g=tr->settlements->chunks+i;
        qlcg_gl_delete_vertex_arrays(1,&g->vao);qlcg_gl_delete_buffers(1,&g->vbo);
    }
    SettlementPlant *plant=tr->settlements->plants;
    while(plant) {
        SettlementPlant *next=plant->next;
        for(int season=0;season<4;season++) qlcg_lp_geometry_free(&plant->geometry[season]);
        free(plant);
        plant=next;
    }
    qlcg_lp_geometry_free(&tr->settlements->chair);
    qlcg_lot_models_free();
    free(tr->settlements);tr->settlements=NULL;
}
