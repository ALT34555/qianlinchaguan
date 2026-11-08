#include "core/dev_tools.h"
/* 32m 网格与共享物种实例缓冲 */
#include "terrain_view.h"
#include "terrain_types.h"
#include "terrain_cliffs.h"
#include "../systems/world/surface_internal.h"
#include "../systems/world/ice_runtime.h"
#include "terrain_ice_cells.h"
#include "../systems/world/model/plant_material.h"
#include "../systems/world/model/rock_material.h"
#include "../core/math_util.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stddef.h>

static void release_mesh(TerrainChunkGPU *c) {
    if (c->cliff_vao) qlcg_gl_delete_vertex_arrays(1, &c->cliff_vao);
    if (c->cliff_vbo) qlcg_gl_delete_buffers(1, &c->cliff_vbo);
    if (c->water_vao) qlcg_gl_delete_vertex_arrays(1, &c->water_vao);
    if (c->water_vbo) qlcg_gl_delete_buffers(1, &c->water_vbo);
    if (c->water_ebo) qlcg_gl_delete_buffers(1, &c->water_ebo);
    if (c->ice_ebo) qlcg_gl_delete_buffers(1, &c->ice_ebo);
    memset(c, 0, sizeof(*c));
}

void qlcg_terrain_clear_chunks(TerrainRenderer *tr) {
    qlcg_terrain_cliffs_reset();
    if (tr->chunks) for (int i = 0; i < QLCG_STREAM_CAP; i++) release_mesh(&tr->chunks[i]);
    tr->terrain_next_slot = tr->terrain_free_count = 0;
    tr->bound_stream = NULL;
    qlcg_weather_bind_world(&tr->weather_sys, NULL);
    tr->props_revision = 0;
    tr->props_ms = 0.0;
    free(tr->probe_plants);
    tr->probe_plants = NULL;
    tr->probe_plant_count = 0;
    tr->tree_inst_count = tr->rock_inst_count = tr->flag_inst_count = 0;
    for (int i = 0; i < tr->plant_batch_count; i++) tr->plants[i].inst_count = 0;
    for (int i = 0; i < tr->rock_batch_count; i++) tr->rocks[i].inst_count = 0;
    tr->plant_inst_total = 0;
    tr->rock_inst_total = 0;
    tr->far_cross_count = 0;
    tr->far_bill_count = 0;
}

enum {
    WATER_GRID_N=CHUNK_CELLS+1,
    WATER_MAX_VERTS=WATER_GRID_N*WATER_GRID_N+4*CHUNK_CELLS*CHUNK_CELLS,
    WATER_MAX_INDICES=12*CHUNK_CELLS*CHUNK_CELLS
};

typedef struct {
    WaterVert vertices[WATER_MAX_VERTS];
    uint32_t water[WATER_MAX_INDICES],ice[WATER_MAX_INDICES];
    int vert_count,water_count,ice_count;
} ChunkWaterMesh;

static float wet_neighbor(const RegionData *reg,int i,float center) {
    return reg->water_levels[i]>reg->heights[i]+.02f?reg->water_levels[i]:center;
}
static void build_water_verts(const RegionData *reg, WaterVert *wv) {
    enum { N = CHUNK_CELLS + 1 };
    for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
        int src = (z+1) * reg->width + x+1, dst = z*N+x;
        float lx = x*reg->step - CHUNK_SIZE*0.5f;
        float lz = z*reg->step - CHUNK_SIZE*0.5f;
        wv[dst].x = lx;
        wv[dst].z = lz;
        wv[dst].y = qlcg_chunk_water_vertex(reg,x+1,z+1);
        wv[dst].h_ground = reg->heights[src];
        wv[dst].flow_x = reg->flow_x ? reg->flow_x[src] : 0.0f;
        wv[dst].flow_z = reg->flow_z ? reg->flow_z[src] : 0.0f;
        wv[dst].fall = 0;
        if(reg->water_kind && (reg->water_kind[src]==WATER_KIND_FALL || reg->water_kind[src]==WATER_KIND_RIVER)) {
            float fx=wv[dst].flow_x,fz=wv[dst].flow_z,speed=fmaxf(.01f,hypotf(fx,fz));
            float gx=(reg->heights[src+1]-reg->heights[src-1])/(2*reg->step);
            float gz=(reg->heights[src+reg->width]-reg->heights[src-reg->width])/(2*reg->step);
            float level=wv[dst].y;
            float wx=(wet_neighbor(reg,src+1,level)-wet_neighbor(reg,src-1,level))/(2*reg->step);
            float wz=(wet_neighbor(reg,src+reg->width,level)-wet_neighbor(reg,src-reg->width,level))/(2*reg->step);
            float grade=fminf(-(gx*fx+gz*fz)/speed,-(wx*fx+wz*fz)/speed);
            grade=fmaxf(grade,fminf(hypotf(gx,gz),hypotf(wx,wz)));
            wv[dst].fall=qlcg_smoothf(.4f,1.1f,grade);
        }
        wv[dst].ice_h = reg->ice ? reg->ice->h[src] : 0.0f;
        wv[dst].ice_q = reg->ice ? reg->ice->q[src] : 1.0f;
        wv[dst].ice_cover = reg->ice_coverage ? reg->ice_coverage[src] : 0.0f;
        SourceWaterEffects effects=reg->source_effects?reg->source_effects[src]:(SourceWaterEffects){0};
        wv[dst].brine=effects.salt==255?1.0f:0.0f;
        wv[dst].spring=effects.spring/255.0f;
        const uint8_t *coast=reg->coast_material?reg->coast_material+src*3:NULL;
        wv[dst].coast_mud=coast?coast[0]/255.0f:0;
        wv[dst].coast_sand=coast?coast[1]/255.0f:0;
        wv[dst].coast_rock=coast?coast[2]/255.0f:0;
    }
}

static float water_edge_depth(const WaterVert *v) {
    return v->y-v->h_ground-.02f;
}

/* 共享边使用固定插值方向 */
static uint32_t water_crossing(ChunkWaterMesh *mesh,uint32_t a,uint32_t b) {
    if(a>b) { uint32_t swap=a; a=b; b=swap; }
    const WaterVert *p=&mesh->vertices[a],*q=&mesh->vertices[b];
    float da=water_edge_depth(p),db=water_edge_depth(q);
    float t=da/(da-db);
    WaterVert v;
    float left[sizeof(WaterVert)/sizeof(float)],right[sizeof(WaterVert)/sizeof(float)];
    memcpy(left,p,sizeof(v));
    memcpy(right,q,sizeof(v));
    for(size_t k=0;k<sizeof(left)/sizeof(left[0]);k++)
        left[k]=qlcg_lerpf(left[k],right[k],t);
    memcpy(&v,left,sizeof(v));
    v.y=v.h_ground+.02f;
    uint32_t idx=mesh->vert_count++;
    mesh->vertices[idx]=v;
    return idx;
}

static void clip_water_triangle(ChunkWaterMesh *mesh,const uint32_t tri[3],
                                uint32_t *indices,int *count) {
    bool wet[3];
    for(int i=0;i<3;i++) wet[i]=water_edge_depth(&mesh->vertices[tri[i]])>0;
    uint32_t polygon[4];
    int n=0;
    for(int i=0;i<3;i++) {
        int next=(i+1)%3;
        if(wet[i]) polygon[n++]=tri[i];
        if(wet[i]!=wet[next]) polygon[n++]=water_crossing(mesh,tri[i],tri[next]);
    }
    for(int i=1;i+1<n;i++) {
        indices[(*count)++]=polygon[0];
        indices[(*count)++]=polygon[i];
        indices[(*count)++]=polygon[i+1];
    }
}

static void build_water_mesh(const RegionData *reg,ChunkWaterMesh *mesh) {
    mesh->vert_count=WATER_GRID_N*WATER_GRID_N;
    mesh->water_count=mesh->ice_count=0;
    build_water_verts(reg,mesh->vertices);
    for(int z=0;z<CHUNK_CELLS;z++) for(int x=0;x<CHUNK_CELLS;x++) {
        if(!qlcg_cell_is_wet(reg,x,z)) continue;
        uint32_t a=z*WATER_GRID_N+x,b=a+WATER_GRID_N;
        const uint32_t triangles[2][3]={{a,b,a+1},{a+1,b,b+1}};
        bool ice=qlcg_cell_draws_ice(reg,x,z);
        for(int k=0;k<2;k++) {
            const uint32_t *tri=triangles[k];
            if(!ice) {
                clip_water_triangle(mesh,tri,mesh->water,&mesh->water_count);
                continue;
            }
            int start=mesh->ice_count;
            clip_water_triangle(mesh,tri,mesh->ice,&mesh->ice_count);
            if(mesh->vertices[tri[0]].ice_cover<.5f || mesh->vertices[tri[1]].ice_cover<.5f ||
               mesh->vertices[tri[2]].ice_cover<.5f) {
                int count=mesh->ice_count-start;
                memcpy(mesh->water+mesh->water_count,mesh->ice+start,(size_t)count*sizeof(uint32_t));
                mesh->water_count+=count;
            }
        }
    }
}

static void upload_water_mesh(TerrainChunkGPU *gpu,const ChunkWaterMesh *mesh) {
    glBindVertexArray(gpu->water_vao);
    glBindBuffer(GL_ARRAY_BUFFER,gpu->water_vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)mesh->vert_count*sizeof(WaterVert),
                 mesh->vertices,GL_STATIC_DRAW);
    if(!gpu->ice_ebo) glGenBuffers(1,&gpu->ice_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gpu->ice_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)mesh->ice_count*sizeof(uint32_t),
                 mesh->ice,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gpu->water_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)mesh->water_count*sizeof(uint32_t),
                 mesh->water,GL_STATIC_DRAW);
    gpu->ice_idx_count=mesh->ice_count;
    gpu->water_idx_count=mesh->water_count;
}

static void upload_ice(TerrainRenderer *tr,TerrainChunkGPU *gpu,const RegionData *reg) {
    static ChunkWaterMesh mesh;
    build_water_mesh(reg,&mesh);
    upload_water_mesh(gpu,&mesh);
    gpu->ice_rev=reg->ice_revision;
    (void)tr;
}

/* 扩容时在显卡内复制旧顶点 */
static void reserve_terrain(TerrainRenderer *tr, const RegionStream *st) {
    int min_side = 2 * (st->radius + 1) + 1;
    if (tr->terrain_capacity >= min_side * min_side && tr->terrain_capacity >= st->count) return;
    int side = qlcg_clampi(min_side + 2, 1, QLCG_STREAM_SIDE);
    int capacity = side * side;
    if (capacity < st->count) capacity = st->count;
    const GLsizeiptr stride = (CHUNK_CELLS + 1) * (CHUNK_CELLS + 1) * sizeof(TerrainVert);
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    if (!buffer) return;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    glBufferData(GL_COPY_WRITE_BUFFER, capacity * stride, NULL, GL_DYNAMIC_DRAW);
    GLint allocated = 0;
    glGetBufferParameteriv(GL_COPY_WRITE_BUFFER, GL_BUFFER_SIZE, &allocated);
    if (allocated != capacity * stride) {
        qlcg_gl_delete_buffers(1, &buffer);
        return;
    }
    if (tr->terrain_next_slot) {
        glBindBuffer(GL_ARRAY_BUFFER, tr->terrain_vbo);
        glCopyBufferSubData(GL_ARRAY_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, tr->terrain_next_slot * stride);
    }
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    glBindVertexArray(tr->terrain_vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    for (GLuint i = 0; i < 3; i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVert),
                              (void *)(size_t)(i * 3 * sizeof(float)));
    }
    qlcg_gl_delete_buffers(1, &tr->terrain_vbo);
    tr->terrain_vbo = buffer;
    tr->terrain_capacity = capacity;
}

static void upload_chunk(TerrainRenderer *tr, TerrainChunkGPU *gpu,
                         const RegionData *reg, double ox, double oz) {
    enum { N = CHUNK_CELLS + 1 };
    TerrainVert tv[N*N];
    TerrainCliffMesh cliffs={0};
    TerrainCliffSite cliff_sites[4]={0};
    static ChunkWaterMesh mesh;
    gpu->cliff_detail=false;
    memset(gpu->fall_emitters,0,sizeof(gpu->fall_emitters));
    for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
        int src = (z+1) * reg->width + x+1, dst = z*N+x;
        float dx = (reg->heights[src+1] - reg->heights[src-1]) / (2*reg->step);
        float dz = (reg->heights[src+reg->width] - reg->heights[src-reg->width]) / (2*reg->step);
        if (reg->heights[src]>=reg->water_levels[src]-.02f ||
            (reg->water_kind && reg->water_kind[src]==WATER_KIND_FALL))
            qlcg_terrain_cliff_site(cliff_sites,x,z,dx,dz);
        if(dx*dx+dz*dz>1.0f) gpu->cliff_detail=true;
        if(x<16 && z<16 && reg->water_kind && reg->water_kind[src]==WATER_KIND_RIVER &&
           reg->water_levels[src]>reg->heights[src]+.05f) {
            int neighbors[4]={src-1,src+1,src-reg->width,src+reg->width};
            float impact=0;
            for(int j=0;j<4;j++) if(reg->water_kind[neighbors[j]]==WATER_KIND_FALL &&
                reg->heights[neighbors[j]]>reg->heights[src]+1)
                impact=fmaxf(impact,reg->water_levels[neighbors[j]]-reg->water_levels[src]);
            float *emitter=gpu->fall_emitters[(x>=8)+(z>=8)*2];
            if(impact>emitter[3] && impact>1) {
                emitter[0]=x*reg->step-16;emitter[1]=reg->water_levels[src];
                emitter[2]=z*reg->step-16;emitter[3]=impact;
            }
        }
        float nx = -dx, ny = 1, nz = -dz;
        qlcg_vec3_norm(&nx, &ny, &nz);
        float lx = x*reg->step - CHUNK_SIZE*0.5f;
        float lz = z*reg->step - CHUNK_SIZE*0.5f;
        float h = reg->heights[src];
        tv[dst] = (TerrainVert){(float)(ox+lx), h, (float)(oz+lz), nx,ny,nz,
            reg->colors[src*3]/255.0f,reg->colors[src*3+1]/255.0f,reg->colors[src*3+2]/255.0f};
    }
    qlcg_terrain_cliffs_build(&cliffs,reg,tr->bound_stream->world,cliff_sites,ox,oz);
    gpu->cliff_vertex_count=cliffs.count;
    if (cliffs.count) {
        gpu->cliff_detail=true;
        if (!gpu->cliff_vao) glGenVertexArrays(1,&gpu->cliff_vao);
        if (!gpu->cliff_vbo) glGenBuffers(1,&gpu->cliff_vbo);
        glBindVertexArray(gpu->cliff_vao);
        glBindBuffer(GL_ARRAY_BUFFER,gpu->cliff_vbo);
        glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)cliffs.count*sizeof(TerrainVert),cliffs.vertices,GL_STATIC_DRAW);
        for (int i=0;i<3;i++) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i,3,GL_FLOAT,GL_FALSE,sizeof(TerrainVert),(void *)(size_t)(i*3*sizeof(float)));
        }
    } else if (gpu->cliff_vao) {
        qlcg_gl_delete_vertex_arrays(1,&gpu->cliff_vao);
        qlcg_gl_delete_buffers(1,&gpu->cliff_vbo);
        gpu->cliff_vao=gpu->cliff_vbo=0;
    }
    free(cliffs.vertices);
    build_water_mesh(reg,&mesh);
    glBindBuffer(GL_ARRAY_BUFFER, tr->terrain_vbo);
    glBufferSubData(GL_ARRAY_BUFFER,
                    (GLintptr)((size_t)gpu->terrain_slot * sizeof(tv)),
                    (GLsizeiptr)sizeof(tv), tv);
    gpu->terrain_ready = true;
    if (!gpu->water_vao) {
        glGenVertexArrays(1, &gpu->water_vao);
        glGenBuffers(1, &gpu->water_vbo);
        glGenBuffers(1, &gpu->water_ebo);
    }
    upload_water_mesh(gpu,&mesh);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(WaterVert),(void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,1,GL_FLOAT,GL_FALSE,sizeof(WaterVert),(void *)(3*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(WaterVert),(void *)(4*sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3,1,GL_FLOAT,GL_FALSE,sizeof(WaterVert),(void *)(6*sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,sizeof(WaterVert),(void *)(7*sizeof(float)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5,1,GL_FLOAT,GL_FALSE,sizeof(WaterVert),(void *)(8*sizeof(float)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6,1,GL_FLOAT,GL_FALSE,sizeof(WaterVert),(void *)(9*sizeof(float)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7,2,GL_FLOAT,GL_FALSE,sizeof(WaterVert),(void *)(10*sizeof(float)));
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8,3,GL_FLOAT,GL_FALSE,sizeof(WaterVert),(void *)offsetof(WaterVert,coast_mud));
    gpu->ice_rev = reg->ice_revision;
    gpu->revision = reg->mesh_revision;
}

/* 近景全模型（乔灌 12 区块，其余 330m），更远回退简模 */
static int plant_form_at(const PropInstance *p, int setting, float px, float pz,
                         float full_r2, float near_r2) {
    float dx = p->x - px, dz = p->z - pz;
    float d2 = dx * dx + dz * dz;
    bool wide = p->category == PLANT_CAT_SHU || p->category == PLANT_CAT_GUAN;
    if (d2 > (wide ? full_r2 : near_r2)) return VEG_FORM_SIMPLE;
    return qlcg_plant_model_form(setting, p->shape_variant);
}

static void upload_plants(TerrainRenderer *tr, const RegionData *reg, int setting,
                          float px, float pz) {
    float near_r = qlcg_dev_prop_radius(QLCG_PROP_NEAR_M);
    float near_r2 = near_r * near_r;
    float full_r2 = (float)(QLCG_PROP_FULL_CHUNKS * CHUNK_SIZE) *
                    (float)(QLCG_PROP_FULL_CHUNKS * CHUNK_SIZE);
    PlantBatch *batches[QLCG_MAX_PLANT_BATCH] = {0};
    int counts[QLCG_MAX_PLANT_BATCH] = {0};
    int offsets[QLCG_MAX_PLANT_BATCH];
    double heights[QLCG_MAX_PLANT_BATCH];
    for (int i = 0; i < tr->plant_batch_count; i++) {
        PlantBatch *b = &tr->plants[i];
        b->inst_count = 0;
        batches[qlcg_plant_batch_key(b->species, b->season, b->form)] = b;
    }
    tr->plant_inst_total = 0;
    if (!reg->tree_count) return;
    PlantInstGPU *instances = malloc((size_t)reg->tree_count * sizeof(*instances));
    if (!instances) return;

    for (int i = 0; i < reg->tree_count; i++) {
        const PropInstance *p = &reg->trees[i];
        if (p->species >= 255 || p->visual_season >= PD_SEASON_COUNT) continue;
        int form = plant_form_at(p, setting, px, pz, full_r2, near_r2);
        counts[qlcg_plant_batch_key(p->species, p->visual_season, form)]++;
    }
    int next = 0;
    for (int key = 0; key < QLCG_MAX_PLANT_BATCH; key++) {
        if (!counts[key]) continue;
        int form = key % QLCG_PLANT_FORM_SLOTS;
        int species_season = key / QLCG_PLANT_FORM_SLOTS;
        int species = species_season / PD_SEASON_COUNT;
        int season = species_season % PD_SEASON_COUNT;
        PlantBatch *b = batches[key];
        bool fresh = b == NULL;
        if (fresh && tr->plant_batch_count < QLCG_MAX_PLANT_BATCH)
            b = &tr->plants[tr->plant_batch_count];
        if (!b || !qlcg_plant_batch_setup(b, species, form, season)) {
            batches[key] = NULL;
            continue;
        }
        if (fresh) tr->plant_batch_count++;
        batches[key] = b;
        heights[key] = qlcg_plant_geometry(species, form, season)->height;
        offsets[key] = next;
        next += counts[key];
    }
    for (int i = 0; i < reg->tree_count; i++) {
        const PropInstance *p = &reg->trees[i];
        if (p->species >= 255 || p->visual_season >= PD_SEASON_COUNT) continue;
        int form = plant_form_at(p, setting, px, pz, full_r2, near_r2);
        int key = qlcg_plant_batch_key(p->species, p->visual_season, form);
        if (!batches[key]) continue;
        instances[offsets[key]++] = (PlantInstGPU){
            p->x, p->y, p->z, p->rot_y,
            qlcg_plant_instance_scale(p->scale_y, heights[key]), (float)p->wind_level
        };
    }
    for (int key = 0; key < QLCG_MAX_PLANT_BATCH; key++) {
        if (!counts[key] || !batches[key]) continue;
        qlcg_plant_batch_instances(batches[key], instances + offsets[key] - counts[key], counts[key]);
        tr->plant_inst_total += counts[key];
    }
    free(instances);
}

static void upload_rocks(TerrainRenderer *tr, const RegionData *reg) {
    PlantBatch *batches[QLCG_MAX_ROCK_BATCH] = {0};
    int counts[QLCG_MAX_ROCK_BATCH] = {0};
    int offsets[QLCG_MAX_ROCK_BATCH];
    double radii[QLCG_MAX_ROCK_BATCH];
    for (int i = 0; i < tr->rock_batch_count; i++) {
        PlantBatch *b = &tr->rocks[i];
        b->inst_count = 0;
        batches[b->species] = b;
    }
    tr->rock_inst_total = 0;
    if (!reg->rock_count) return;
    int rock_count = qlcg_rock_material_init();
    if (!rock_count) return;
    PlantInstGPU *instances = malloc((size_t)reg->rock_count * sizeof(*instances));
    if (!instances) return;

    for (int i = 0; i < reg->rock_count; i++) {
        int index = reg->rocks[i].archetype;
        if (index < rock_count) counts[index]++;
    }
    int next = 0;
    for (int index = 0; index < rock_count; index++) {
        if (!counts[index]) continue;
        PlantBatch *b = batches[index];
        bool fresh = b == NULL;
        if (fresh) b = &tr->rocks[tr->rock_batch_count];
        if (!qlcg_rock_batch_setup(b, index, ROCK_FORM_OUTCROP, tr->plant_season, 1)) {
            counts[index] = 0;
            continue;
        }
        if (fresh) tr->rock_batch_count++;
        batches[index] = b;
        radii[index] = qlcg_rock_geometry(index, ROCK_FORM_OUTCROP, tr->plant_season, 1)->radius;
        offsets[index] = next;
        next += counts[index];
    }
    for (int i = 0; i < reg->rock_count; i++) {
        const PropInstance *p = &reg->rocks[i];
        int index = p->archetype;
        if (index >= rock_count || !counts[index]) continue;
        instances[offsets[index]++] = (PlantInstGPU){p->x, p->y, p->z, p->rot_y,
            qlcg_rock_instance_scale(p->scale_x, radii[index]), 0.0f};
    }
    for (int index = 0; index < rock_count; index++) {
        int n = counts[index];
        if (!n) continue;
        qlcg_plant_batch_instances(batches[index], instances + offsets[index] - n, n);
        tr->rock_inst_total += n;
    }
    free(instances);
}

static int upload_prop_fallback(GLuint vbo, const PropInstance *props, int count, float scale) {
    if (count <= 0) return 0;
    size_t bytes = (size_t)count * sizeof(PropInstGPU);
    PropInstGPU *instances = malloc(bytes);
    if (!instances) return 0;
    for (int i = 0; i < count; i++) {
        const PropInstance *p = &props[i];
        instances[i] = (PropInstGPU){p->x, p->y, p->z, p->scale_x * scale, p->scale_y * scale,
                                    p->scale_z * scale, p->rot_y, p->r, p->g, p->b};
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, instances, GL_DYNAMIC_DRAW);
    free(instances);
    return count;
}

static void upload_props(TerrainRenderer *tr, const RegionData *reg, const GameSettings *s,
                         float px, float pz) {
    upload_plants(tr, reg, s->vegetation_form, px, pz);
    float scale = s->vegetation_form == VEG_FORM_SIMPLE ? 0.85f : 1.0f;
    tr->tree_inst_count = tr->plant_inst_total ? 0
        : upload_prop_fallback(tr->tree_inst_vbo, reg->trees, reg->tree_count, scale);
    upload_rocks(tr, reg);
    tr->rock_inst_count = tr->rock_inst_total ? 0
        : upload_prop_fallback(tr->rock_inst_vbo, reg->rocks, reg->rock_count, 1.0f);

    FlagInstGPU fi[sizeof(reg->flags) / sizeof(reg->flags[0])];
    for (int i = 0; i < reg->flag_count; i++) {
        const PropInstance *p = &reg->flags[i];
        uint32_t tc = p->trim_color;
        fi[i] = (FlagInstGPU){p->x, p->y, p->z, 1.0f, 0.0f,
                              p->r, p->g, p->b,
                              (float)((tc >> 16) & 0xFF) / 255.0f,
                              (float)((tc >> 8) & 0xFF) / 255.0f,
                              (float)(tc & 0xFF) / 255.0f,
                              (float)p->wind_level};
    }
    glBindBuffer(GL_ARRAY_BUFFER, tr->flag_inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(reg->flag_count * (int)sizeof(FlagInstGPU)), fi, GL_DYNAMIC_DRAW);
    tr->flag_inst_count = reg->flag_count;
}

static bool in_radius(const RegionStream *st, const RegionData *c, int radius) {
    int dx = c->center_cx - st->center_cx, dz = c->center_cz - st->center_cz;
    return dx*dx + dz*dz <= (radius+0.5f)*(radius+0.5f);
}

/* 区块风等级：天气等级与事件狂风取大 */
static int chunk_wind_level(const TerrainRenderer *tr, const RegionData *c) {
    if (!c || !c->temperatures || c->width <= 0 || c->height <= 0) return 0;
    int idx = (c->height / 2) * c->width + c->width / 2;
    float temp = c->temperatures[idx];
    int eq = (c->stream && c->stream->world) ? c->stream->world->equator_chunks : 0;
    ChunkWeather cw = qlcg_sample_chunk_weather(&tr->weather_sys, tr->weather_unix_ms,
                                                c->center_cx, c->center_cz, temp, eq);
    int lv = qlcg_natural_physics_level(cw.weather_id);
    if (tr->wind_strength >= 2.0f) { if (lv < 5) lv = 5; }
    else if (tr->wind_strength >= 1.0f) { if (lv < 4) lv = 4; }
    return lv;
}

/* 远景稳定抽稀：区块坐标与序号定取舍 */
static bool far_keep(const RegionData *c, int j, int step) {
    uint32_t h = qlcg_hash_u32((uint32_t)c->center_cx * 73856093u ^
                               (uint32_t)c->center_cz * 19349663u ^
                               ((uint32_t)j + 1u) * 2654435761u);
    return h % (uint32_t)step == 0;
}

/* 分带结果：3D 与交叉面片在 11–12 区块重叠淡入 */
typedef struct { bool near3d, cross, bill; } PropBands;

static PropBands plant_bands(const TerrainRenderer *tr, const RegionData *c, int j,
                             float px, float pz, float full_r2, float cross_start_r2,
                             float cross_r2, float wide_r2) {
    PropBands b = { false, false, false };
    const PropInstance *p = &c->trees[j];
    bool wide = p->category == PLANT_CAT_SHU || p->category == PLANT_CAT_GUAN;
    float dx = c->center_x + p->x - px, dz = c->center_z + p->z - pz;
    float d2 = dx * dx + dz * dz;
    b.near3d = d2 <= (wide ? full_r2 : QLCG_PROP_NEAR_M * QLCG_PROP_NEAR_M);
    if (!wide) return b;
    PropUvRect uv;
    if (!qlcg_prop_sprites_plant_uv(&tr->sprites, p->species, p->visual_season, &uv))
        return b;
    if (d2 > cross_start_r2 && d2 <= cross_r2) b.cross = true;
    else if (d2 > cross_r2 && far_keep(c, j, d2 <= wide_r2 ? 1 : 2)) b.bill = true;
    return b;
}

static PropBands rock_bands(const TerrainRenderer *tr, const RegionData *c, int j,
                            float px, float pz, float full_r2, float cross_start_r2,
                            float cross_r2, float wide_r2) {
    PropBands b = { false, false, false };
    const PropInstance *p = &c->rocks[j];
    float dx = c->center_x + p->x - px, dz = c->center_z + p->z - pz;
    float d2 = dx * dx + dz * dz;
    b.near3d = d2 <= full_r2;
    PropUvRect uv;
    if (!qlcg_prop_sprites_rock_uv(&tr->sprites, p->archetype, tr->plant_season, &uv))
        return b;
    if (d2 > cross_start_r2 && d2 <= cross_r2) b.cross = true;
    else if (d2 > cross_r2 && far_keep(c, j + c->tree_count, d2 <= wide_r2 ? 1 : 2)) b.bill = true;
    return b;
}

/* 两批远景实例上传 */
static void upload_far_props(TerrainRenderer *tr, const FarPropInst *cross, int nc,
                             const FarPropInst *bill, int nb) {
    if (nc > 0 && cross && tr->far_cross_inst) {
        glBindBuffer(GL_ARRAY_BUFFER, tr->far_cross_inst);
        if (nc > tr->far_cross_cap) {
            tr->far_cross_cap = nc;
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)nc * sizeof(FarPropInst)),
                         cross, GL_DYNAMIC_DRAW);
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            (GLsizeiptr)((size_t)nc * sizeof(FarPropInst)), cross);
        }
        tr->far_cross_count = nc;
    } else {
        tr->far_cross_count = 0;
    }
    if (nb > 0 && bill && tr->far_bill_inst) {
        glBindBuffer(GL_ARRAY_BUFFER, tr->far_bill_inst);
        if (nb > tr->far_bill_cap) {
            tr->far_bill_cap = nb;
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)nb * sizeof(FarPropInst)),
                         bill, GL_DYNAMIC_DRAW);
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            (GLsizeiptr)((size_t)nb * sizeof(FarPropInst)), bill);
        }
        tr->far_bill_count = nb;
    } else {
        tr->far_bill_count = 0;
    }
}

/* 远景实例 key：区块坐标 + 序号 + 类型，跨分带保持稳定 */
static unsigned long long far_fade_key(int32_t cx, int32_t cz, int j, int kind) {
    unsigned long long h = 1469598103934665603ull;
    uint32_t v[4] = { (uint32_t)cx, (uint32_t)cz, (uint32_t)j, (uint32_t)kind };
    for (int i = 0; i < 4; i++)
        for (int b = 0; b < 4; b++) { h ^= (v[i] >> (b * 8)) & 0xFFu; h *= 1099511628211ull; }
    return h ? h : 1ull;
}

/* 表项：key 0 空、1 墓碑；按代清理 */
static void far_fade_rehash(TerrainRenderer *tr, int cap) {
    PropFadeEnt *old = tr->far_fade;
    int old_cap = tr->far_fade_cap;
    tr->far_fade = (PropFadeEnt *)calloc((size_t)cap, sizeof(PropFadeEnt));
    tr->far_fade_cap = tr->far_fade ? cap : 0;
    tr->far_fade_dead = 0;
    if (!tr->far_fade) { free(old); return; }
    uint32_t mask = (uint32_t)cap - 1u;
    for (int i = 0; i < old_cap; i++) {
        if (old[i].key <= 1ull) continue;
        uint32_t j = (uint32_t)old[i].key & mask;
        while (tr->far_fade[j].key) j = (j + 1u) & mask;
        tr->far_fade[j] = old[i];
    }
    free(old);
}

static void far_fade_reset(TerrainRenderer *tr, int count) {
    int need = 64;
    while (need < count * 2) need <<= 1;
    if (tr->far_fade_cap != need) {
        free(tr->far_fade);
        tr->far_fade = NULL;
        tr->far_fade_cap = 0;
        far_fade_rehash(tr, need);
        tr->far_fade_gen = 1;
        return;
    }
    if (++tr->far_fade_gen == 0) tr->far_fade_gen = 1;
    if (tr->far_fade_dead > need / 4) far_fade_rehash(tr, need);
}

/* 查首次出现时间；新实例从当前时刻开始淡入 */
static float far_fade_birth(TerrainRenderer *tr, unsigned long long key, double now_ms) {
    if (!tr->far_fade || tr->far_fade_cap <= 0) return (float)(now_ms * 0.001);
    uint32_t mask = (uint32_t)tr->far_fade_cap - 1u;
    uint32_t i = (uint32_t)key & mask;
    uint32_t first_dead = 0xFFFFFFFFu;
    for (;;) {
        PropFadeEnt *e = &tr->far_fade[i];
        if (e->key == key) {
            e->gen = tr->far_fade_gen;
            return (float)(e->t0 * 0.001);
        }
        if (e->key == 0) {
            if (first_dead != 0xFFFFFFFFu) e = &tr->far_fade[first_dead];
            e->key = key; e->t0 = now_ms; e->gen = tr->far_fade_gen;
            return (float)(now_ms * 0.001);
        }
        if (e->key == 1ull && first_dead == 0xFFFFFFFFu) first_dead = i;
        i = (i + 1u) & mask;
    }
}

static void far_fade_prune(TerrainRenderer *tr) {
    if (!tr->far_fade) return;
    for (int i = 0; i < tr->far_fade_cap; i++) {
        PropFadeEnt *e = &tr->far_fade[i];
        if (e->key > 1ull && e->gen != tr->far_fade_gen) { e->key = 1ull; tr->far_fade_dead++; }
    }
}

/* 填一条远景实例：尺寸按内容包围盒比例还原，避免被拉宽 */
static void far_fill(FarPropInst *out, const PropInstance *p,
                     float ox, float oz, const PropUvRect *uv, uint32_t hsh, float span) {
    out->x = ox + p->x;
    out->y = p->y;
    out->z = oz + p->z;
    out->w = uv->fw * span;
    out->h = uv->fh * span;
    out->u0 = uv->u0; out->v0 = uv->v0; out->u1 = uv->u1; out->v1 = uv->v1;
    out->rot = p->rot_y;
    out->shade = 0.92f + (float)(hsh & 0xFFu) / 255.0f * 0.16f;
    out->birth = 0.0f;
}

static void collect_props(TerrainRenderer *tr, const RegionData *reg, const GameSettings *s) {
    RegionStream *st = reg->stream;
    int vd = qlcg_clampi(s->vegetation_distance, 0, st->radius);
    float px = reg->center_x + tr->x, pz = reg->center_z + tr->z;
    float full_r2 = (float)(QLCG_PROP_FULL_CHUNKS * CHUNK_SIZE) * (float)(QLCG_PROP_FULL_CHUNKS * CHUNK_SIZE);
    float cross_r2 = (float)(QLCG_PROP_SPRITE_CHUNKS * CHUNK_SIZE) * (float)(QLCG_PROP_SPRITE_CHUNKS * CHUNK_SIZE);
    float cross_start_r2 = (float)(QLCG_PROP_CROSS_START_CHUNKS * CHUNK_SIZE) *
                           (float)(QLCG_PROP_CROSS_START_CHUNKS * CHUNK_SIZE);
    float wide_r2 = (24.0f * CHUNK_SIZE) * (24.0f * CHUNK_SIZE);
    if (qlcg_dev_far_all()) { full_r2 = 0.0f; cross_start_r2 = 0.0f; }
    RegionData merged = {0};
    int nt = 0, nr = 0, nc = 0, nb = 0;
    for (int i = 0; i < st->count; i++) {
        RegionData *c = st->slots[st->active[i]].data;
        if (!in_radius(st,c,st->radius) || !in_radius(st,c,vd)) continue;
        for (int j = 0; j < c->tree_count; j++) {
            PropBands b = plant_bands(tr, c, j, px, pz, full_r2, cross_start_r2,
                                      cross_r2, wide_r2);
            nt += b.near3d ? 1 : 0;
            nc += b.cross ? 1 : 0;
            nb += b.bill ? 1 : 0;
        }
        for (int j = 0; j < c->rock_count; j++) {
            PropBands b = rock_bands(tr, c, j, px, pz, full_r2, cross_start_r2,
                                     cross_r2, wide_r2);
            nr += b.near3d ? 1 : 0;
            nc += b.cross ? 1 : 0;
            nb += b.bill ? 1 : 0;
        }
    }
    merged.trees = malloc((size_t)(nt+1)*sizeof(PropInstance));
    merged.rocks = malloc((size_t)(nr+1)*sizeof(PropInstance));
    FarPropInst *cross = nc > 0 ? malloc((size_t)nc * sizeof(FarPropInst)) : NULL;
    FarPropInst *bill  = nb > 0 ? malloc((size_t)nb * sizeof(FarPropInst)) : NULL;
    if (!merged.trees || !merged.rocks || (nc > 0 && !cross) || (nb > 0 && !bill)) {
        free(merged.trees); free(merged.rocks); free(cross); free(bill);
        return;
    }
    int ci = 0, bi = 0;
    if (tr->fade_origin_ms <= 0.0) tr->fade_origin_ms = qlcg_time_ms();
    double now_ms = qlcg_time_ms() - tr->fade_origin_ms;
    far_fade_reset(tr, nc + nb);
    /* 近景入 3D 列表，远景入贴图两批，旗帜不随植被裁 */
    for (int i = 0; i < st->count; i++) {
        RegionData *c = st->slots[st->active[i]].data;
        if (!in_radius(st,c,st->radius)) continue;
        float ox = (float)((double)c->center_x - tr->anchor_x);
        float oz = (float)((double)c->center_z - tr->anchor_z);
        int lv = 0;
        bool lv_ok = false;
        if (in_radius(st,c,vd)) {
            for (int j = 0; j < c->tree_count; j++) {
                PropBands b = plant_bands(tr, c, j, px, pz, full_r2, cross_start_r2,
                                          cross_r2, wide_r2);
                const PropInstance *p = &c->trees[j];
                if (b.near3d) {
                    if (!lv_ok) { lv = chunk_wind_level(tr, c); lv_ok = true; }
                    PropInstance v = *p; v.x += ox; v.z += oz; v.wind_level = (uint8_t)lv;
                    merged.trees[merged.tree_count++] = v;
                }
                if (!b.cross && !b.bill) continue;
                PropUvRect uv;
                if (!qlcg_prop_sprites_plant_uv(&tr->sprites, p->species, p->visual_season, &uv))
                    continue;
                uint32_t hsh = qlcg_hash_u32((uint32_t)c->center_cx * 73856093u ^
                                             (uint32_t)c->center_cz * 19349663u ^
                                             (uint32_t)j * 2654435761u);
                float birth = far_fade_birth(tr, far_fade_key(c->center_cx, c->center_cz, j, 0), now_ms);
                float span = 1.05f * fmaxf(p->scale_x, p->scale_y);
                if (b.cross) {
                    far_fill(&cross[ci], p, ox, oz, &uv, hsh, span);
                    cross[ci++].birth = birth;
                }
                if (b.bill) {
                    far_fill(&bill[bi], p, ox, oz, &uv, hsh, span);
                    bill[bi].rot = 0.0f;
                    bill[bi].birth = birth;
                    bi++;
                }
            }
            for (int j = 0; j < c->rock_count; j++) {
                PropBands b = rock_bands(tr, c, j, px, pz, full_r2, cross_start_r2,
                                         cross_r2, wide_r2);
                const PropInstance *p = &c->rocks[j];
                if (b.near3d) {
                    PropInstance v = *p; v.x += ox; v.z += oz;
                    merged.rocks[merged.rock_count++] = v;
                }
                if (!b.cross && !b.bill) continue;
                PropUvRect uv;
                if (!qlcg_prop_sprites_rock_uv(&tr->sprites, p->archetype, tr->plant_season, &uv))
                    continue;
                uint32_t hsh = qlcg_hash_u32((uint32_t)c->center_cx * 19349663u ^
                                             (uint32_t)c->center_cz * 73856093u ^
                                             ((uint32_t)j + 7u) * 2654435761u);
                FarPropInst fi;
                const RockGeometry *g = qlcg_rock_geometry(p->archetype, ROCK_FORM_OUTCROP, tr->plant_season, 1);
                float scale = qlcg_rock_instance_scale(p->scale_x, g ? g->radius : 1.0);
                far_fill(&fi, p, ox, oz, &uv, hsh, qlcg_rock_sprite_span(g) * scale);
                fi.birth = far_fade_birth(tr, far_fade_key(c->center_cx, c->center_cz, j, 1), now_ms);
                if (b.cross) cross[ci++] = fi;
                if (b.bill) { fi.rot = 0.0f; bill[bi++] = fi; }
            }
        }
        if (c->flag_count > 0 && !lv_ok) { lv = chunk_wind_level(tr, c); lv_ok = true; }
        for (int j = 0; j < c->flag_count && merged.flag_count < 32; j++) {
            PropInstance v = c->flags[j]; v.x += ox; v.z += oz; v.wind_level = (uint8_t)lv;
            merged.flags[merged.flag_count++] = v;
        }
    }
    upload_props(tr, &merged, s,
                 (float)((double)reg->center_x + tr->x - tr->anchor_x),
                 (float)((double)reg->center_z + tr->z - tr->anchor_z));
    far_fade_prune(tr);
    upload_far_props(tr, cross, ci, bill, bi);
    free(cross);
    free(bill);
    free(tr->probe_plants);
    tr->probe_plants = merged.trees;
    tr->probe_plant_count = merged.tree_count;
    free(merged.rocks);
    tr->props_revision = st->revision;
    tr->props_distance = vd;
    tr->props_form = s->vegetation_form;
}

void qlcg_terrain_upload_region(TerrainRenderer *tr, const RegionData *reg, const GameSettings *s) {
    if (!reg || !reg->stream || !tr->chunks) return;
    double t0 = qlcg_time_ms();
    RegionStream *st = reg->stream;
    uint32_t wstamp = (uint32_t)(tr->weather_unix_ms / 60000.0) * 8u
                    + (uint32_t)(tr->weather_sys.forced_id + 1);
    int vd = qlcg_clampi(s->vegetation_distance, 0, st->radius);
    bool props_fresh = (tr->bound_stream == st && tr->props_revision == st->revision &&
                        tr->props_distance == vd &&
                        tr->props_form == s->vegetation_form &&
                        tr->props_weather_stamp == wstamp);
    if (tr->bound_stream != st) {
        qlcg_terrain_clear_chunks(tr);
        tr->bound_stream = st;
        props_fresh = false;
    }
    if (st->world) {
        qlcg_weather_set_seed(&tr->weather_sys, st->world->opt.seed);
        qlcg_weather_bind_world(&tr->weather_sys, st->world);
    }
    reserve_terrain(tr, st);
    for (int i = 0; i < QLCG_STREAM_CAP; i++) {
        TerrainChunkGPU *g = &tr->chunks[i];
        RegionChunkSlot *c = &st->slots[i];
        if (g->serial && (!c->data || g->serial != c->serial)) {
            tr->terrain_free_slots[tr->terrain_free_count++] = (uint16_t)g->terrain_slot;
            release_mesh(g);
        }
    }
    /* 宏观格锚点：顶点坐标基准，只有跨格才整体重传 */
    double cell = 1.0;
    if (st->world && st->world->columns > 0 && st->world->circumference > 1.0f)
        cell = (double)st->world->circumference / (double)st->world->columns;
    if (cell < 32.0) cell = 32.0;
    int acx = (int)floor(reg->center_x / cell);
    int acz = (int)floor(reg->center_z / cell);
    if (acx != tr->anchor_cell_x || acz != tr->anchor_cell_z || tr->anchor_x == 0.0) {
        tr->anchor_cell_x = acx;
        tr->anchor_cell_z = acz;
        tr->anchor_x = ((double)acx + 0.5) * cell;
        tr->anchor_z = ((double)acz + 0.5) * cell;
        for (int i = 0; i < QLCG_STREAM_CAP; i++) tr->chunks[i].terrain_ready = false;
    }
    int ice_uploads = 0;
    for (int i = 0; i < st->count; i++) {
        int slot = st->active[i];
        RegionChunkSlot *c = &st->slots[slot];
        TerrainChunkGPU *g = &tr->chunks[slot];
        const RegionData *d = c->data;
        if (!g->serial) {
            if (tr->terrain_free_count) {
                g->terrain_slot = tr->terrain_free_slots[--tr->terrain_free_count];
            } else {
                if (tr->terrain_next_slot >= tr->terrain_capacity) continue;
                g->terrain_slot = tr->terrain_next_slot++;
            }
        }
        if (!g->serial || g->revision != d->mesh_revision || !g->terrain_ready) {
            double ox = (double)d->center_x - tr->anchor_x;
            double oz = (double)d->center_z - tr->anchor_z;
            upload_chunk(tr, g, d, ox, oz);
            g->serial = c->serial;
        } else if (g->ice_rev != d->ice_revision && ice_uploads < 6) {
            /* 投影之外的独立冰更新：不触发地形与植被重传 */
            upload_ice(tr, g, d);
            ice_uploads++;
        }
    }
    if (!props_fresh) {
        /* 流式装载中限频重建，避免逐帧重传实例 */
        bool only_revision = tr->bound_stream == st && tr->props_distance == vd &&
                             tr->props_form == s->vegetation_form &&
                             tr->props_weather_stamp == wstamp;
        double now = qlcg_time_ms();
        if (!only_revision || now - tr->props_ms >= QLCG_PROP_REBUILD_MS) {
            collect_props(tr, reg, s);
            tr->props_weather_stamp = wstamp;
            tr->props_ms = now;
        }
    }
    tr->upload_ms = qlcg_time_ms()-t0;
}

void qlcg_terrain_stream_update(TerrainRenderer *tr, RegionData *reg, const GameSettings *s) {
    if (!reg || !reg->stream) return;
    qlcg_region_stream_update(reg,tr->x,tr->z,tr->plant_season,s,1.5);
    RegionStream *st = reg->stream;
    float nx = (st->center_cx+0.5f)*CHUNK_SIZE;
    float nz = (st->center_cz+0.5f)*CHUNK_SIZE;
    float dx = nx-reg->center_x, dz = nz-reg->center_z;
    if (dx != 0 || dz != 0) {
        tr->x -= dx; tr->z -= dz;
        tr->hover_lx -= dx; tr->hover_lz -= dz;
        for (int i = 0; i < tr->river_pt_count; i++) {
            tr->river_pts_x[i] -= dx; tr->river_pts_z[i] -= dz;
        }
        reg->center_x = nx; reg->center_z = nz;
        reg->center_cx = st->center_cx; reg->center_cz = st->center_cz;
        st->revision++;
    }
    qlcg_terrain_upload_region(tr,reg,s);
}
