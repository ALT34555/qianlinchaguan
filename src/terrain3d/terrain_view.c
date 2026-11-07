/* 山川资源：上下文、实例布局与上传入口 */
#include "terrain_view.h"
#include "terrain_types.h"
#include "ground_detail.h"
#include "../core/math_util.h"
#include "../core/measurements.h"
#include "../core/dev_tools.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "terrain_shaders.h"
#include "../systems/world/model/plant_material.h"
#include "../systems/world/model/rock_material.h"
#include "../systems/world/model/cliff_rock.h"
#include "../systems/world/model/flag_model.h"

static void setup_instanced_vao(GLuint *vao, GLuint *vbo, GLuint *ebo, GLuint *inst_vbo,
                                const PropMeshVert *verts, int vcnt,
                                const uint16_t *indices, int icnt) {
    glGenVertexArrays(1, vao);
    glGenBuffers(1, vbo);
    glGenBuffers(1, ebo);
    glGenBuffers(1, inst_vbo);

    glBindVertexArray(*vao);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vcnt * (int)sizeof(PropMeshVert)), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(icnt * (int)sizeof(uint16_t)), indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PropMeshVert), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PropMeshVert), (void *)(3 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, *inst_vbo);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(PropInstGPU), (void *)offsetof(PropInstGPU, x));
    glVertexAttribDivisor(2, 1);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(PropInstGPU), (void *)offsetof(PropInstGPU, sx));
    glVertexAttribDivisor(3, 1);

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(PropInstGPU), (void *)offsetof(PropInstGPU, rot));
    glVertexAttribDivisor(4, 1);

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(PropInstGPU), (void *)offsetof(PropInstGPU, r));
    glVertexAttribDivisor(5, 1);
}

/* 远景贴图 VAO：aCorner(0) + aDir(6) + 实例 1..5 */
static void setup_far_vao(GLuint *vao, GLuint *vbo, GLuint *ebo, GLuint *inst,
                          const float *verts, int vcount, const uint16_t *indices, int icount) {
    glGenVertexArrays(1, vao);
    glGenBuffers(1, vbo);
    glGenBuffers(1, ebo);
    glGenBuffers(1, inst);
    glBindVertexArray(*vao);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vcount * 4u * sizeof(float)),
                 verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)icount * sizeof(uint16_t)),
                 indices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, *inst);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(64 * sizeof(FarPropInst)), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(FarPropInst),
                          (void *)offsetof(FarPropInst, x));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(FarPropInst),
                          (void *)offsetof(FarPropInst, w));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(FarPropInst),
                          (void *)offsetof(FarPropInst, u0));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(FarPropInst),
                          (void *)offsetof(FarPropInst, rot));
    glVertexAttribDivisor(4, 1);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(FarPropInst),
                          (void *)offsetof(FarPropInst, shade));
    glVertexAttribDivisor(5, 1);
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(FarPropInst),
                          (void *)offsetof(FarPropInst, birth));
    glVertexAttribDivisor(7, 1);
    glBindVertexArray(0);
}

/* 旗帜实例布局：顶点材质槽/风摆+实例双色 */
static void setup_flag_vao(TerrainRenderer *tr, const FlagMesh *mesh) {
    glGenVertexArrays(1, &tr->flag_vao);
    glGenBuffers(1, &tr->flag_vbo);
    glGenBuffers(1, &tr->flag_ebo);
    glGenBuffers(1, &tr->flag_inst_vbo);

    glBindVertexArray(tr->flag_vao);
    glBindBuffer(GL_ARRAY_BUFFER, tr->flag_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)mesh->vcount * sizeof(FlagMeshVert)),
                 mesh->verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tr->flag_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)mesh->icount * sizeof(uint16_t)),
                 mesh->indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FlagMeshVert), (void *)offsetof(FlagMeshVert, px));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(FlagMeshVert), (void *)offsetof(FlagMeshVert, nx));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(FlagMeshVert), (void *)offsetof(FlagMeshVert, cr));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(FlagMeshVert), (void *)offsetof(FlagMeshVert, mat));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(FlagMeshVert), (void *)offsetof(FlagMeshVert, wind));

    glBindBuffer(GL_ARRAY_BUFFER, tr->flag_inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(32 * (int)sizeof(FlagInstGPU)), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(FlagInstGPU), (void *)offsetof(FlagInstGPU, x));
    glVertexAttribDivisor(5, 1);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(FlagInstGPU), (void *)offsetof(FlagInstGPU, scale));
    glVertexAttribDivisor(6, 1);
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(FlagInstGPU), (void *)offsetof(FlagInstGPU, rot));
    glVertexAttribDivisor(7, 1);
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 3, GL_FLOAT, GL_FALSE, sizeof(FlagInstGPU), (void *)offsetof(FlagInstGPU, fr));
    glVertexAttribDivisor(8, 1);
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 3, GL_FLOAT, GL_FALSE, sizeof(FlagInstGPU), (void *)offsetof(FlagInstGPU, tr));
    glVertexAttribDivisor(9, 1);
    glEnableVertexAttribArray(10);
    glVertexAttribPointer(10, 1, GL_FLOAT, GL_FALSE, sizeof(FlagInstGPU), (void *)offsetof(FlagInstGPU, level));
    glVertexAttribDivisor(10, 1);
    glBindVertexArray(0);

    tr->flag_mesh_verts   = mesh->vcount;
    tr->flag_mesh_indices = mesh->icount;
}


/* 构建或复用某物种的网格批次 */
bool qlcg_plant_batch_setup(PlantBatch *b, int species, int form, int season) {
    if (b->vao && b->species == species && b->form == form && b->season == season) return true;
    const LpGeometry *g = qlcg_plant_geometry(species, form, season);
    if (!g || g->vertex_count <= 0 || g->index_count <= 0) return false;

    int vcnt = g->vertex_count;
    PlantVertex *verts = (PlantVertex *)malloc((size_t)vcnt * sizeof(PlantVertex));
    if (!verts) return false;

    if (b->vao) {
        glDeleteVertexArrays(1, &b->vao);
        glDeleteBuffers(1, &b->vbo);
        glDeleteBuffers(1, &b->ebo);
        glDeleteBuffers(1, &b->inst_vbo);
        memset(b, 0, sizeof(*b));
    }

    float maxh = (float)g->height;
    if (maxh < 0.01f) maxh = 0.01f;
    for (int i = 0; i < vcnt; i++) {
        verts[i].px = g->positions[i * 3 + 0];
        verts[i].py = g->positions[i * 3 + 1];
        verts[i].pz = g->positions[i * 3 + 2];
        verts[i].nx = g->normals[i * 3 + 0];
        verts[i].ny = g->normals[i * 3 + 1];
        verts[i].nz = g->normals[i * 3 + 2];
        verts[i].cr = (float)g->colors[i * 3 + 0] / 255.0f;
        verts[i].cg = (float)g->colors[i * 3 + 1] / 255.0f;
        verts[i].cb = (float)g->colors[i * 3 + 2] / 255.0f;
        verts[i].part = (float)g->wind_parts[i];
        verts[i].h = qlcg_clampf(g->positions[i * 3 + 1] / maxh, 0.0f, 1.0f);
    }

    int cap = 2048;
    glGenVertexArrays(1, &b->vao);
    glGenBuffers(1, &b->vbo);
    glGenBuffers(1, &b->ebo);
    glGenBuffers(1, &b->inst_vbo);
    glBindVertexArray(b->vao);
    glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vcnt * (int)sizeof(PlantVertex)), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(g->index_count * (int)sizeof(uint32_t)), g->indices, GL_STATIC_DRAW);
    free(verts);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PlantVertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PlantVertex), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(PlantVertex), (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(PlantVertex), (void *)(9 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(PlantVertex), (void *)(10 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, b->inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(cap * (int)sizeof(PlantInstGPU)), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(PlantInstGPU), (void *)0);
    glVertexAttribDivisor(5, 1);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(PlantInstGPU), (void *)offsetof(PlantInstGPU, rot));
    glVertexAttribDivisor(6, 1);
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(PlantInstGPU), (void *)offsetof(PlantInstGPU, scale));
    glVertexAttribDivisor(7, 1);
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(PlantInstGPU), (void *)offsetof(PlantInstGPU, level));
    glVertexAttribDivisor(8, 1);
    glBindVertexArray(0);

    b->species = species;
    b->form = form;
    b->season = season;
    b->category = qlcg_plant_species_category(species);
    b->idx_count = g->index_count;
    b->inst_cap = cap;
    b->inst_count = 0;
    return true;
}

/* 实例缓冲按需扩容 */
void qlcg_plant_batch_instances(PlantBatch *b, const PlantInstGPU *inst, int count) {
    if (!b->vao) return;
    if (count <= 0) { b->inst_count = 0; return; }
    glBindBuffer(GL_ARRAY_BUFFER, b->inst_vbo);
    if (count > b->inst_cap) {
        b->inst_cap = count;
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)count*sizeof(*inst)), inst, GL_DYNAMIC_DRAW);
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)((size_t)count*sizeof(*inst)), inst);
    }
    b->inst_count = count;
}


/* 构建或复用某岩石的网格批次 */
bool qlcg_rock_batch_setup(PlantBatch *b, int index, int form, int season, int chunk) {
    if (b->vao && b->species == index && b->form == form && b->season == season) return true;
    const RockGeometry *g = qlcg_rock_geometry(index, form, season, chunk);
    if (!g || g->vertex_count <= 0 || g->index_count <= 0) return false;

    int vcnt = g->vertex_count;
    PlantVertex *verts = malloc((size_t)vcnt * sizeof(*verts));
    if (!verts) return false;
    if (b->vao) {
        glDeleteVertexArrays(1, &b->vao);
        glDeleteBuffers(1, &b->vbo);
        glDeleteBuffers(1, &b->ebo);
        glDeleteBuffers(1, &b->inst_vbo);
        memset(b, 0, sizeof(*b));
    }

    for (int i = 0; i < vcnt; i++) {
        verts[i].px = g->positions[i * 3 + 0];
        verts[i].py = g->positions[i * 3 + 1];
        verts[i].pz = g->positions[i * 3 + 2];
        verts[i].nx = g->normals[i * 3 + 0];
        verts[i].ny = g->normals[i * 3 + 1];
        verts[i].nz = g->normals[i * 3 + 2];
        verts[i].cr = (float)g->colors[i * 3 + 0] / 255.0f;
        verts[i].cg = (float)g->colors[i * 3 + 1] / 255.0f;
        verts[i].cb = (float)g->colors[i * 3 + 2] / 255.0f;
        verts[i].part = 0.0f;   /* 岩石不随风摆动 */
        verts[i].h = 0.0f;
    }

    int cap = 1024;
    glGenVertexArrays(1, &b->vao);
    glGenBuffers(1, &b->vbo);
    glGenBuffers(1, &b->ebo);
    glGenBuffers(1, &b->inst_vbo);
    glBindVertexArray(b->vao);
    glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vcnt * (int)sizeof(PlantVertex)), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(g->index_count * (int)sizeof(uint32_t)), g->indices, GL_STATIC_DRAW);
    free(verts);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PlantVertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PlantVertex), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(PlantVertex), (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(PlantVertex), (void *)(9 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(PlantVertex), (void *)(10 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, b->inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(cap * (int)sizeof(PlantInstGPU)), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(PlantInstGPU), (void *)0);
    glVertexAttribDivisor(5, 1);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(PlantInstGPU), (void *)offsetof(PlantInstGPU, rot));
    glVertexAttribDivisor(6, 1);
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(PlantInstGPU), (void *)offsetof(PlantInstGPU, scale));
    glVertexAttribDivisor(7, 1);
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(PlantInstGPU), (void *)offsetof(PlantInstGPU, level));
    glVertexAttribDivisor(8, 1);
    glBindVertexArray(0);

    b->species = index;
    b->form = form;
    b->season = season;
    b->idx_count = g->index_count;
    b->inst_cap = cap;
    b->inst_count = 0;
    return true;
}

/* 释放全部岩石批次 */
void qlcg_rock_batches_clear(TerrainRenderer *tr) {
    for (int i = 0; i < tr->rock_batch_count; i++) {
        PlantBatch *b = &tr->rocks[i];
        if (b->vao) {
            glDeleteVertexArrays(1, &b->vao);
            glDeleteBuffers(1, &b->vbo);
            glDeleteBuffers(1, &b->ebo);
            glDeleteBuffers(1, &b->inst_vbo);
        }
        memset(b, 0, sizeof(*b));
    }
    tr->rock_batch_count = 0;
    tr->rock_inst_total = 0;
}

/* 设置植被季节 */
void qlcg_terrain_set_season(TerrainRenderer *tr, int season_idx) {
    tr->plant_season = (season_idx >= 0 && season_idx < 4) ? season_idx : 1;
}

/* 释放全部批次 */
void qlcg_plant_batches_clear(TerrainRenderer *tr) {
    for (int i = 0; i < tr->plant_batch_count; i++) {
        PlantBatch *b = &tr->plants[i];
        if (b->vao) {
            glDeleteVertexArrays(1, &b->vao);
            glDeleteBuffers(1, &b->vbo);
            glDeleteBuffers(1, &b->ebo);
            glDeleteBuffers(1, &b->inst_vbo);
        }
        memset(b, 0, sizeof(*b));
    }
    tr->plant_batch_count = 0;
    tr->plant_inst_total = 0;
}

/* 初始化 3D 山川渲染器 */
/* Shared edge vertices */
static int lod_triangle(uint32_t *indices,uint32_t a,uint32_t b,uint32_t c,
                        bool split_ab,bool split_ca) {
    uint32_t ab=(a+b)/2,ca=(c+a)/2;
    int count=0;
    if(split_ab) {
        indices[count++]=ab; indices[count++]=b; indices[count++]=c;
        b=ab;
    }
    if(split_ca) {
        indices[count++]=b; indices[count++]=c; indices[count++]=ca;
        c=ca;
    }
    indices[count++]=a; indices[count++]=b; indices[count++]=c;
    return count;
}

bool qlcg_terrain_init(TerrainRenderer *tr) {
    memset(tr, 0, sizeof(*tr));
    tr->sky_prog     = qlcg_gl_compile_program(SKY_VS_SRC, SKY_FS_SRC);
    tr->terrain_prog = qlcg_gl_compile_program(TERR_VS_SRC, TERR_FS_SRC);
    tr->water_prog   = qlcg_gl_compile_program(WATER_VS_SRC, WATER_FS_SRC);
    tr->prop_prog    = qlcg_gl_compile_program(PROP_VS_SRC, PROP_FS_SRC);
    tr->flag_prog    = qlcg_gl_compile_program(FLAG_VS_SRC, PROP_FS_SRC);
    tr->plant_prog   = qlcg_gl_compile_program(PLANT_VS_SRC, PLANT_FS_SRC);
    tr->weather_prog = qlcg_gl_compile_program(WEATHER_VS_SRC, WEATHER_FS_SRC);
    tr->spring_prog  = qlcg_gl_compile_program(SPRING_VS_SRC, SPRING_FS_SRC);
    tr->far_prog     = qlcg_gl_compile_program(FAR_VS_SRC, FAR_FS_SRC);
    {
        static const float cross_v[8 * 4] = {
            -0.5f, 0.0f, 1.0f, 0.0f,   0.5f, 0.0f, 1.0f, 0.0f,
             0.5f, 1.0f, 1.0f, 0.0f,  -0.5f, 1.0f, 1.0f, 0.0f,
            -0.5f, 0.0f, 0.0f, 1.0f,   0.5f, 0.0f, 0.0f, 1.0f,
             0.5f, 1.0f, 0.0f, 1.0f,  -0.5f, 1.0f, 0.0f, 1.0f
        };
        static const uint16_t cross_i[12] = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
        static const float bill_v[4 * 4] = {
            -0.5f, 0.0f, 1.0f, 0.0f,  0.5f, 0.0f, 1.0f, 0.0f,
             0.5f, 1.0f, 1.0f, 0.0f, -0.5f, 1.0f, 1.0f, 0.0f
        };
        static const uint16_t bill_i[6] = { 0, 1, 2, 0, 2, 3 };
        tr->far_cross_idx = 12;
        tr->far_bill_idx = 6;
        setup_far_vao(&tr->far_cross_vao, &tr->far_cross_vbo, &tr->far_cross_ebo,
                      &tr->far_cross_inst, cross_v, 8, cross_i, 12);
        setup_far_vao(&tr->far_bill_vao, &tr->far_bill_vbo, &tr->far_bill_ebo,
                      &tr->far_bill_inst, bill_v, 4, bill_i, 6);
    }
    tr->ice_vis.crack_buf[0] = 0.0f;
    qlcg_ice_visual_init(tr);

    float sky_quad[12] = {-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1};
    glGenVertexArrays(1, &tr->sky_vao);
    glGenBuffers(1, &tr->sky_vbo);
    glBindVertexArray(tr->sky_vao);
    glBindBuffer(GL_ARRAY_BUFFER, tr->sky_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(sky_quad), sky_quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);

    tr->chunks = calloc(QLCG_STREAM_CAP, sizeof(TerrainChunkGPU));
    if (!tr->chunks) return false;
    uint32_t indices[CHUNK_CELLS*CHUNK_CELLS*6 + (CHUNK_CELLS/2)*(CHUNK_CELLS/2)*6 + CHUNK_CELLS*6];
    int ip = 0;
    for (int z = 0; z < CHUNK_CELLS; z++) for (int x = 0; x < CHUNK_CELLS; x++) {
        uint32_t a = z*(CHUNK_CELLS+1)+x, b = a+CHUNK_CELLS+1;
        indices[ip++] = a; indices[ip++] = b; indices[ip++] = a+1;
        indices[ip++] = a+1; indices[ip++] = b; indices[ip++] = b+1;
    }
    int lod_off = ip;   /* 半 LOD 索引段从这里开始 */
    for (int z = 0; z < CHUNK_CELLS / 2; z++) for (int x = 0; x < CHUNK_CELLS / 2; x++) {
        uint32_t a = (uint32_t)(z*2)*(CHUNK_CELLS+1) + (uint32_t)(x*2);
        uint32_t b = a + (uint32_t)(CHUNK_CELLS+1)*2;
        uint32_t c = a + 2, d = b + 2;
        ip+=lod_triangle(indices+ip,a,b,c,x==0,z==0);
        ip+=lod_triangle(indices+ip,d,c,b,x==CHUNK_CELLS/2-1,z==CHUNK_CELLS/2-1);
    }
    tr->terrain_lod_off = lod_off;
    tr->terrain_lod_count = ip - lod_off;
    glGenVertexArrays(1, &tr->terrain_vao);
    glBindVertexArray(tr->terrain_vao);
    glGenBuffers(1, &tr->terrain_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tr->terrain_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    tr->md_count = malloc(sizeof(*tr->md_count) * QLCG_STREAM_CAP);
    tr->md_index = malloc(sizeof(*tr->md_index) * QLCG_STREAM_CAP);
    tr->md_base = malloc(sizeof(*tr->md_base) * QLCG_STREAM_CAP);
    tr->terrain_idx_count = lod_off;
    tr->water_idx_count = lod_off;
    tr->terrain_origin_x = tr->terrain_origin_z = 1e30f;
    tr->plant_season = 1;
    qlcg_ground_tex_init(tr);
    qlcg_prop_sprites_init(&tr->sprites, qlcg_dev_sprite_dir("src/systems/world/model"));

    /* 双层树冠锥体网格 */
    PropMeshVert tree_v[10] = {
        { 0.0f, 1.0f,  0.0f,  0.0f, 0.8f,  0.0f},
        {-0.5f, 0.25f, -0.5f, -0.7f, 0.3f, -0.7f},
        { 0.5f, 0.25f, -0.5f,  0.7f, 0.3f, -0.7f},
        { 0.5f, 0.25f,  0.5f,  0.7f, 0.3f,  0.7f},
        {-0.5f, 0.25f,  0.5f, -0.7f, 0.3f,  0.7f},
        {-0.12f,0.0f, -0.12f, -1.0f, 0.0f,  0.0f},
        { 0.12f,0.0f, -0.12f,  1.0f, 0.0f,  0.0f},
        { 0.12f,0.0f,  0.12f,  0.0f, 0.0f,  1.0f},
        {-0.12f,0.0f,  0.12f,  0.0f, 0.0f, -1.0f},
        { 0.0f, 0.32f, 0.0f,   0.0f, 1.0f,  0.0f}
    };
    uint16_t tree_idx[24] = {
        0,1,2, 0,2,3, 0,3,4, 0,4,1,
        9,5,6, 9,6,7, 9,7,8, 9,8,5
    };
    tr->tree_mesh_indices = 24;
    setup_instanced_vao(&tr->tree_vao, &tr->tree_vbo, &tr->tree_ebo, &tr->tree_inst_vbo,
                        tree_v, 10, tree_idx, 24);

    /* 多面体山石网格 */
    PropMeshVert rock_v[6] = {
        { 0.0f, 0.85f,  0.0f,  0.0f, 1.0f,  0.0f},
        {-0.5f, 0.15f, -0.5f, -0.7f, 0.2f, -0.7f},
        { 0.5f, 0.15f, -0.5f,  0.7f, 0.2f, -0.7f},
        { 0.5f, 0.15f,  0.5f,  0.7f, 0.2f,  0.7f},
        {-0.5f, 0.15f,  0.5f, -0.7f, 0.2f,  0.7f},
        { 0.0f,-0.10f,  0.0f,  0.0f,-1.0f,  0.0f}
    };
    uint16_t rock_idx[24] = {
        0,1,2, 0,2,3, 0,3,4, 0,4,1,
        5,2,1, 5,3,2, 5,4,3, 5,1,4
    };
    tr->rock_mesh_indices = 24;
    setup_instanced_vao(&tr->rock_vao, &tr->rock_vbo, &tr->rock_ebo, &tr->rock_inst_vbo,
                        rock_v, 6, rock_idx, 24);

    /* 置旗地：竖幡、横梁与斜撑（base Flag） */
    FlagMesh flag_mesh;
    qlcg_flag_mesh_build(&flag_mesh);
    if (flag_mesh.vcount > 0 && flag_mesh.icount > 0) {
        setup_flag_vao(tr, &flag_mesh);
    }
    qlcg_flag_mesh_free(&flag_mesh);

    /* 天气粒子种子 VBO */
    float *wseeds = (float *)malloc(1800 * 3 * sizeof(float));
    for (int i = 0; i < 1800 * 3; i++) {
        wseeds[i] = (float)(qlcg_hash32((uint32_t)(i + 77)) & 0xFFFFu) / 65535.0f;
    }
    glGenVertexArrays(1, &tr->weather_vao);
    glGenBuffers(1, &tr->weather_vbo);
    glBindVertexArray(tr->weather_vao);
    glBindBuffer(GL_ARRAY_BUFFER, tr->weather_vbo);
    glBufferData(GL_ARRAY_BUFFER, 1800 * 3 * sizeof(float), wseeds, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    free(wseeds);

    qlcg_weather_init(&tr->weather_sys, 2679841377u);
    tr->cur_weather   = qlcg_get_weather_profile(0);
    tr->brush_radius  = 28.0f;
    tr->river_width_m = 16.0f;
    tr->river_depth_m = 6.5f;
    tr->river_meander = 0.35f;
    tr->wind_strength = 0.35f;
    tr->precip_intensity = 0.0f;
    tr->weather_unix_ms = 0.0;
    tr->props_weather_stamp = 0;
    return true;
}

/* 回收山川场景缓存 */
void qlcg_terrain_release_scene(TerrainRenderer *tr) {
    qlcg_astra_release(&tr->astronomy);
    qlcg_terrain_clear_chunks(tr);
    qlcg_plant_batches_clear(tr);
    qlcg_rock_batches_clear(tr);
    const GLuint buffers[] = {tr->terrain_vbo, tr->tree_inst_vbo, tr->rock_inst_vbo, tr->flag_inst_vbo};
    for (int i = 0; i < (int)(sizeof(buffers) / sizeof(buffers[0])); i++) {
        if (!buffers[i]) continue;
        glBindBuffer(GL_ARRAY_BUFFER, buffers[i]);
        glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    tr->terrain_capacity = 0;
    qlcg_ice_visual_reset(tr);
}

void qlcg_terrain_shutdown(TerrainRenderer *tr) {
    qlcg_astra_release(&tr->astronomy);
    qlcg_ice_visual_shutdown(tr);
    qlcg_terrain_clear_chunks(tr);
    free(tr->chunks); tr->chunks = NULL;
    free(tr->md_count); tr->md_count = NULL;
    free(tr->md_index); tr->md_index = NULL;
    free(tr->md_base); tr->md_base = NULL;
    qlcg_plant_batches_clear(tr);
    qlcg_rock_batches_clear(tr);
    qlcg_prop_sprites_free(&tr->sprites);
    qlcg_plant_material_shutdown();
    qlcg_rock_material_shutdown();
    qlcg_cliff_shutdown();
    glDeleteProgram(tr->plant_prog);
    glDeleteProgram(tr->far_prog);
    GLuint vaos[] = {tr->sky_vao,tr->terrain_vao,tr->tree_vao,tr->rock_vao,tr->flag_vao,tr->weather_vao,
                     tr->far_cross_vao,tr->far_bill_vao};
    GLuint bufs[] = {tr->sky_vbo,tr->terrain_vbo,tr->terrain_ebo,tr->tree_vbo,tr->tree_ebo,tr->tree_inst_vbo,
        tr->rock_vbo,tr->rock_ebo,tr->rock_inst_vbo,tr->flag_vbo,tr->flag_ebo,tr->flag_inst_vbo,tr->weather_vbo,
        tr->far_cross_vbo,tr->far_cross_ebo,tr->far_cross_inst,
        tr->far_bill_vbo,tr->far_bill_ebo,tr->far_bill_inst};
    glDeleteVertexArrays(sizeof(vaos)/sizeof(vaos[0]),vaos);
    glDeleteBuffers(sizeof(bufs)/sizeof(bufs[0]),bufs);
    glDeleteProgram(tr->sky_prog);
    glDeleteProgram(tr->terrain_prog);
    glDeleteProgram(tr->water_prog);
    glDeleteProgram(tr->prop_prog);
    glDeleteProgram(tr->flag_prog);
    glDeleteProgram(tr->weather_prog);
    glDeleteProgram(tr->spring_prog);
    for (int i = 0; i < QLCG_GROUND_BAND_N; i++)
        if (tr->ground_tex[i]) { glDeleteTextures(1, &tr->ground_tex[i]); tr->ground_tex[i] = 0; }
    free(tr->far_fade);
    tr->far_fade = NULL;
    tr->far_fade_cap = 0;
}

/* 地面材质纹理：五气候带可平铺贴图，启动生成 */
void qlcg_ground_tex_init(TerrainRenderer *tr) {
    size_t n = (size_t)QLCG_GROUND_TEX_PX * QLCG_GROUND_TEX_PX * 3;
    unsigned char *buf = (unsigned char *)malloc(n);
    if (!buf) return;
    uint32_t seed = 0x51ab3c7du;
    for (int b = 0; b < QLCG_GROUND_BAND_N; b++) {
        qlcg_ground_tex_build(buf, QLCG_GROUND_TEX_PX, QLCG_GROUND_TEX_SPAN, b, seed);
        glGenTextures(1, &tr->ground_tex[b]);
        glBindTexture(GL_TEXTURE_2D, tr->ground_tex[b]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, QLCG_GROUND_TEX_PX, QLCG_GROUND_TEX_PX,
                     0, GL_RGB, GL_UNSIGNED_BYTE, buf);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        /* 地面以掠射角观察，各向异性过滤决定远景清晰度 */
        glTexParameterf(GL_TEXTURE_2D, 0x84FE, 8.0f);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    free(buf);
}

