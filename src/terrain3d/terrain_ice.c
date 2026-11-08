/* 冰视觉：浮冰实例池、碎冰与裂纹反馈 */
#include "terrain_view.h"
#include "terrain_types.h"
#include "terrain_shaders.h"
#include "gl_loader.h"
#include "../systems/world/ice_runtime.h"
#include "../core/math_util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ICE_VIS_FLOATS 8

typedef struct {
    float px, py, pz;
    float nx, ny, nz;
} IceTplVert;

/* 每次构建模板的临时缓冲 */
static IceTplVert s_tpl_pos[96];
static uint16_t   s_tpl_idx[192];
static int        s_tpl_v, s_tpl_i;

static void tpl_push(float x, float y, float z, float nx, float ny, float nz) {
    if (s_tpl_v >= 96) return;
    s_tpl_pos[s_tpl_v].px = x;
    s_tpl_pos[s_tpl_v].py = y;
    s_tpl_pos[s_tpl_v].pz = z;
    s_tpl_pos[s_tpl_v].nx = nx;
    s_tpl_pos[s_tpl_v].ny = ny;
    s_tpl_pos[s_tpl_v].nz = nz;
    s_tpl_v++;
}

static void tpl_tri(int a, int b, int c) {
    if (s_tpl_i + 3 > 192) return;
    s_tpl_idx[s_tpl_i++] = (uint16_t)a;
    s_tpl_idx[s_tpl_i++] = (uint16_t)b;
    s_tpl_idx[s_tpl_i++] = (uint16_t)c;
}

/* 9 边不规则薄冰板：顶面/底面/侧面各自独立顶点 */
#define PRISM_SIDES 9
static void build_prism(int variant) {
    static const float jitter[4][PRISM_SIDES] = {
        { 1.00f, 0.62f, 0.96f, 0.74f, 1.14f, 0.70f, 0.92f, 1.18f, 0.66f },
        { 0.72f, 1.12f, 0.68f, 1.02f, 0.86f, 1.16f, 0.64f, 0.94f, 1.08f },
        { 1.16f, 0.78f, 0.88f, 1.20f, 0.66f, 0.98f, 1.10f, 0.70f, 0.90f },
        { 0.90f, 1.06f, 1.18f, 0.68f, 0.94f, 1.14f, 0.72f, 1.02f, 0.64f }
    };
    float rx[PRISM_SIDES], rz[PRISM_SIDES];
    for (int k = 0; k < PRISM_SIDES; k++) {
        float ang = (float)k * (6.2831853f / (float)PRISM_SIDES) + (float)variant * 0.19f;
        float r = jitter[variant & 3][k];
        rx[k] = cosf(ang) * r;
        rz[k] = sinf(ang) * r;
    }
    s_tpl_v = s_tpl_i = 0;
    /* 顶面 */
    int top0 = s_tpl_v;
    for (int k = 0; k < PRISM_SIDES; k++) tpl_push(rx[k], 1.0f, rz[k], 0.0f, 1.0f, 0.0f);
    tpl_push(0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    for (int k = 0; k < PRISM_SIDES; k++)
        tpl_tri(top0 + PRISM_SIDES, top0 + k, top0 + (k + 1) % PRISM_SIDES);
    /* 底面（y=-1，上下对称，实例厚度即全厚） */
    int bot0 = s_tpl_v;
    for (int k = 0; k < PRISM_SIDES; k++) tpl_push(rx[k], -1.0f, rz[k], 0.0f, -1.0f, 0.0f);
    tpl_push(0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f);
    for (int k = 0; k < PRISM_SIDES; k++)
        tpl_tri(bot0 + PRISM_SIDES, bot0 + (k + 1) % PRISM_SIDES, bot0 + k);
    /* 侧面 */
    for (int k = 0; k < PRISM_SIDES; k++) {
        int n = (k + 1) % PRISM_SIDES;
        float ex = rx[n] - rx[k], ez = rz[n] - rz[k];
        float nx = ez, nz = -ex;
        float len = sqrtf(nx * nx + nz * nz);
        if (len > 1e-6f) { nx /= len; nz /= len; }
        int base = s_tpl_v;
        tpl_push(rx[k], 1.0f, rz[k], nx, 0.0f, nz);
        tpl_push(rx[k], -1.0f, rz[k], nx, 0.0f, nz);
        tpl_push(rx[n], -1.0f, rz[n], nx, 0.0f, nz);
        tpl_push(rx[n], 1.0f, rz[n], nx, 0.0f, nz);
        tpl_tri(base + 0, base + 1, base + 2);
        tpl_tri(base + 0, base + 2, base + 3);
    }
}

bool qlcg_ice_visual_init(TerrainRenderer *tr) {
    IceVisual *v = &tr->ice_vis;
    v->ice_prog   = qlcg_gl_compile_program(ICEP_VS_SRC, ICEP_FS_SRC);
    v->ice_top_prog = qlcg_gl_compile_program(ICE_VS_SRC, ICE_FS_SRC);
    v->crack_prog = qlcg_gl_compile_program(CRACK_VS_SRC, CRACK_FS_SRC);
    for (int t = 0; t < QLCG_ICE_FLOW_TEMPLATES; t++) {
        build_prism(t);
        glGenVertexArrays(1, &v->ice_tpl_vao[t]);
        glGenBuffers(1, &v->ice_tpl_vbo[t]);
        glGenBuffers(1, &v->ice_tpl_ebo[t]);
        glBindVertexArray(v->ice_tpl_vao[t]);
        glBindBuffer(GL_ARRAY_BUFFER, v->ice_tpl_vbo[t]);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(s_tpl_v * (int)sizeof(IceTplVert)),
                     s_tpl_pos, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(IceTplVert), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(IceTplVert), (void *)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(IceTplVert), (void *)(3 * sizeof(float)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, v->ice_tpl_ebo[t]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(s_tpl_i * (int)sizeof(uint16_t)),
                     s_tpl_idx, GL_STATIC_DRAW);
        v->ice_tpl_idx[t]  = s_tpl_i;
        v->ice_tpl_vert[t] = s_tpl_v;
    }
    glGenBuffers(1, &v->ice_inst_vbo);
    for (int t = 0; t < QLCG_ICE_FLOW_TEMPLATES; t++) {
        glBindVertexArray(v->ice_tpl_vao[t]);
        glBindBuffer(GL_ARRAY_BUFFER, v->ice_inst_vbo);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, ICE_VIS_FLOATS * sizeof(float), (void *)0);
        glVertexAttribDivisor(3, 1);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, ICE_VIS_FLOATS * sizeof(float), (void *)(3 * sizeof(float)));
        glVertexAttribDivisor(4, 1);
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, ICE_VIS_FLOATS * sizeof(float), (void *)(4 * sizeof(float)));
        glVertexAttribDivisor(5, 1);
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, ICE_VIS_FLOATS * sizeof(float), (void *)(5 * sizeof(float)));
        glVertexAttribDivisor(6, 1);
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, ICE_VIS_FLOATS * sizeof(float), (void *)(6 * sizeof(float)));
        glVertexAttribDivisor(7, 1);
        glEnableVertexAttribArray(8);
        glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, ICE_VIS_FLOATS * sizeof(float), (void *)(7 * sizeof(float)));
        glVertexAttribDivisor(8, 1);
    }
    glBindVertexArray(0);
    glGenVertexArrays(1, &v->crack_vao);
    glGenBuffers(1, &v->crack_vbo);
    glBindVertexArray(v->crack_vao);
    glBindBuffer(GL_ARRAY_BUFFER, v->crack_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(3 * sizeof(float)));
    glBindVertexArray(0);
    return v->ice_prog != 0 && v->ice_top_prog != 0 && v->crack_prog != 0;
}

void qlcg_ice_visual_shutdown(TerrainRenderer *tr) {
    IceVisual *v = &tr->ice_vis;
    for (int t = 0; t < QLCG_ICE_FLOW_TEMPLATES; t++) {
        if (v->ice_tpl_vao[t]) qlcg_gl_delete_vertex_arrays(1, &v->ice_tpl_vao[t]);
        if (v->ice_tpl_vbo[t]) qlcg_gl_delete_buffers(1, &v->ice_tpl_vbo[t]);
        if (v->ice_tpl_ebo[t]) qlcg_gl_delete_buffers(1, &v->ice_tpl_ebo[t]);
    }
    if (v->ice_inst_vbo) qlcg_gl_delete_buffers(1, &v->ice_inst_vbo);
    if (v->crack_vao) qlcg_gl_delete_vertex_arrays(1, &v->crack_vao);
    if (v->crack_vbo) qlcg_gl_delete_buffers(1, &v->crack_vbo);
    if (v->ice_prog) qlcg_gl_delete_program(v->ice_prog);
    if (v->ice_top_prog) qlcg_gl_delete_program(v->ice_top_prog);
    if (v->crack_prog) qlcg_gl_delete_program(v->crack_prog);
    memset(v, 0, sizeof(*v));
}

void qlcg_ice_visual_reset(TerrainRenderer *tr) {
    memset(tr->ice_vis.ice_pool, 0, sizeof(tr->ice_vis.ice_pool));
    tr->ice_vis.crack_count = 0;
    tr->ice_vis.ice_seed_cursor = 0;
    tr->ice_vis.ice_seed_timer = 0.0f;
}

static IceFlowInst *pool_alloc(IceVisual *v, uint8_t kind) {
    int lo = kind == 0 ? 0 : QLCG_ICE_FLOW_NATURAL;
    int hi = kind == 0 ? QLCG_ICE_FLOW_NATURAL : QLCG_ICE_FLOW_NATURAL + QLCG_ICE_FLOW_FRAGMENT;
    for (int i = lo; i < hi; i++) if (!v->ice_pool[i].used) return &v->ice_pool[i];
    return NULL;
}

void qlcg_ice_visual_spawn_fragments(TerrainRenderer *tr, float wx, float wy, float wz, int count) {
    IceVisual *v = &tr->ice_vis;
    for (int i = 0; i < count; i++) {
        IceFlowInst *f = pool_alloc(v, 1);
        if (!f) return;
        float a = (float)i * 2.39996f;
        memset(f, 0, sizeof(*f));
        f->used = true;
        f->kind = 1;
        f->tpl = (uint8_t)((i * 3 + 1) & (QLCG_ICE_FLOW_TEMPLATES - 1));
        f->x = wx + cosf(a) * 0.35f;
        f->z = wz + sinf(a) * 0.35f;
        f->y = wy;
        f->rot = a;
        f->scale = 0.12f + 0.22f * ((float)((i * 37) % 7) / 7.0f);
        f->thickness = 0.018f + 0.020f * ((float)((i * 53) % 5) / 5.0f);
        f->alpha = 0.95f;
        f->yaw_spin = (a > 3.14159f ? 1.0f : -1.0f) * 1.6f;
        f->phase = 0.0f;           /* 用作年龄 */
    }
}

/* 洞口一圈碎冰：让方形洞口看起来是砸开的 */
void qlcg_ice_visual_spawn_rim(TerrainRenderer *tr, float wx, float wy, float wz,
                               float half, int count) {
    IceVisual *v = &tr->ice_vis;
    for (int i = 0; i < count; i++) {
        IceFlowInst *f = pool_alloc(v, 1);
        if (!f) return;
        float a = (float)i * (6.2831853f / (float)count) + (float)(i % 3) * 0.37f;
        float r = half + 0.35f + 0.30f * ((float)((i * 7) % 5) / 5.0f);
        memset(f, 0, sizeof(*f));
        f->used = true;
        f->kind = 1;
        f->tpl = (uint8_t)((i * 5 + 1) & (QLCG_ICE_FLOW_TEMPLATES - 1));
        f->x = wx + cosf(a) * r;
        f->z = wz + sinf(a) * r;
        f->y = wy - 0.015f;
        f->rot = a + 0.6f;
        f->scale = 0.16f + 0.16f * ((float)((i * 37) % 7) / 7.0f);
        f->thickness = 0.020f + 0.018f * ((float)((i * 53) % 5) / 5.0f);
        f->alpha = 0.98f;
        f->yaw_spin = ((float)(i % 2) * 2.0f - 1.0f) * 0.25f;
        f->phase = -6.0f;
    }
}
void qlcg_ice_visual_cracks_clear(TerrainRenderer *tr) {
    tr->ice_vis.crack_count = 0;
}

void qlcg_ice_visual_crack_add(TerrainRenderer *tr, float wx, float wy, float wz,
                               float size, float damage) {
    IceVisual *v = &tr->ice_vis;
    int slot = -1;
    for (int i = 0; i < v->crack_count; i++) {
        float dx = v->cracks[i].x - wx, dz = v->cracks[i].z - wz;
        if (dx * dx + dz * dz < 1.0f) { slot = i; break; }
    }
    if (slot < 0) {
        if (v->crack_count >= QLCG_ICE_CRACK_MAX) {
            /* 丢最旧的 */
            slot = 0;
            for (int i = 1; i < v->crack_count; i++)
                if (v->cracks[i].damage < v->cracks[slot].damage) slot = i;
        } else {
            slot = v->crack_count++;
        }
        v->cracks[slot].x = wx;
        v->cracks[slot].y = wy;
        v->cracks[slot].z = wz;
        v->cracks[slot].rot = (float)((int)(wx * 13.7f + wz * 7.3f) % 100) * 0.0628f;
        v->cracks[slot].size = size;
        v->cracks[slot].decay = 0.0f;
    }
    v->cracks[slot].damage = qlcg_maxf(v->cracks[slot].damage, damage);
}

/* 附近格是否有天然冰：只为装饰生成服务 */
static bool cell_has_ice_near(const RegionData *reg, float lx, float lz) {
    const float half = CHUNK_SIZE * 0.5f;
    int gx = (int)floorf((lx + half) / TERRAIN_GRID);
    int gz = (int)floorf((lz + half) / TERRAIN_GRID);
    for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
        int x = gx + dx + 1, z = gz + dz + 1;
        if (x < 0 || z < 0 || x >= reg->width || z >= reg->height) continue;
        if (reg->ice_coverage && reg->ice_coverage[z * reg->width + x] >= .5f) return true;
    }
    return false;
}

static void seed_natural(TerrainRenderer *tr, RegionData *reg, const MacroWorld *w) {
    IceVisual *v = &tr->ice_vis;
    int live = 0;
    for (int i = 0; i < QLCG_ICE_FLOW_NATURAL; i++) if (v->ice_pool[i].used) live++;
    if (live >= QLCG_ICE_FLOW_NATURAL) return;
    /* 每轮只看一块 3×3 中的一块，cursor 轮转 */
    int idx = v->ice_seed_cursor++ % 9;
    int dxc = idx % 3 - 1, dzc = idx / 3 - 1;
    const float span = CHUNK_SIZE;
    float lx0 = (float)dxc * span + tr->x - reg->center_x;
    float lz0 = (float)dzc * span + tr->z - reg->center_z;
    for (int cz = 0; cz < 4; cz++) {
        for (int cx = 0; cx < 4; cx++) {
            float lx = lx0 + (float)cx * 8.0f + 4.0f;
            float lz = lz0 + (float)cz * 8.0f + 4.0f;
            uint32_t hsh = qlcg_hash_u32((uint32_t)(int)floorf(lx * 0.125f) * 73856093u ^
                                         (uint32_t)(int)floorf(lz * 0.125f) * 19349663u ^
                                         (w ? w->opt.seed : 1u));
            float p = (float)(hsh & 0xFFFFu) / 65535.0f;
            if (p > 0.10f) continue;
            float wx = reg->center_x + lx, wz = reg->center_z + lz;
            float wl = qlcg_region_water_at(reg, lx, lz);
            float gh = qlcg_region_height_at(reg, lx, lz);
            if (!(wl > gh + 0.25f)) continue;
            /* 天然浮冰：附近有冰、本格开水才生成 */
            if (!cell_has_ice_near(reg, lx, lz)) continue;
            if (qlcg_region_ice_ground(reg, lx, lz, 0.6f, wl + 0.2f) > wl - 0.05f) continue;
            IceFlowInst *f = pool_alloc(v, 0);
            if (!f) return;
            memset(f, 0, sizeof(*f));
            f->used = true;
            f->kind = 0;
            f->tpl = (uint8_t)((hsh >> 5) & (QLCG_ICE_FLOW_TEMPLATES - 1));
            f->x = wx + ((float)((hsh >> 8) & 0xFF) / 255.0f - .5f) * 6.0f;
            f->z = wz + ((float)((hsh >> 16) & 0xFF) / 255.0f - .5f) * 6.0f;
            f->y = wl - 0.012f;
            f->rot = (float)(hsh & 0xFF) / 255.0f * 6.2831853f;
            f->scale = 0.55f + 1.25f * ((float)((hsh >> 3) & 0x3F) / 63.0f);
            f->thickness = 0.022f + 0.038f * ((float)((hsh >> 9) & 0x3F) / 63.0f);
            f->alpha = 1.0f;
            f->yaw_spin = ((float)((hsh >> 15) & 0x1F) / 31.0f - .5f) * 0.12f;
            f->phase = -(8.0f + 12.0f * ((float)((hsh >> 21) & 0x3F) / 63.0f));
        }
    }
}

void qlcg_ice_visual_update(TerrainRenderer *tr, RegionData *reg, const MacroWorld *w, float dt) {
    IceVisual *v = &tr->ice_vis;
    if (dt <= 0.0f) dt = 0.016f;
    if (dt > 0.1f) dt = 0.1f;
    int total = QLCG_ICE_FLOW_NATURAL + QLCG_ICE_FLOW_FRAGMENT;
    for (int i = 0; i < total; i++) {
        IceFlowInst *f = &v->ice_pool[i];
        if (!f->used) continue;
        if (f->kind == 1) {
            f->phase += dt;
            f->y -= 0.12f * dt * qlcg_minf(1.0f, f->phase * 3.0f);
            f->alpha = qlcg_maxf(0.0f, 0.95f - f->phase * 0.55f);
            f->rot += f->yaw_spin * dt;
            if (f->phase > 0.5f + f->scale * 3.0f || f->alpha <= 0.02f) f->used = false;
            continue;
        }
        f->phase += dt;
        float age = f->phase;
        /* 漂移：湖面平移很小，河流按未缩放流速 */
        float vx = 0.0f, vz = 0.0f;
        float lx = f->x - reg->center_x, lz = f->z - reg->center_z;
        const RegionData *c = qlcg_region_chunk_at(reg, lx, lz, NULL, NULL);
        if (c) {
            int vx_i = (int)floorf((lx - c->center_x + CHUNK_SIZE * .5f) / TERRAIN_GRID) + 1;
            int vz_i = (int)floorf((lz - c->center_z + CHUNK_SIZE * .5f) / TERRAIN_GRID) + 1;
            vx_i = qlcg_clampi(vx_i, 0, c->width - 1);
            vz_i = qlcg_clampi(vz_i, 0, c->height - 1);
            int idx = vz_i * c->width + vx_i;
            float sp = hypotf(c->flow_x ? c->flow_x[idx] : 0.0f, c->flow_z ? c->flow_z[idx] : 0.0f);
            if (sp > 0.05f) {
                float k = qlcg_minf(0.8f / sp, 0.35f);
                vx = c->flow_x[idx] * k;
                vz = c->flow_z[idx] * k;
            } else {
                vx = 0.04f * sinf(age * 0.6f + f->rot);
                vz = 0.04f * cosf(age * 0.5f + f->rot);
            }
        }
        f->x += vx * dt;
        f->z += vz * dt;
        f->rot += f->yaw_spin * dt;
        /* 垂直摆幅 ≤1cm */
        f->y += sinf(age * 1.1f + f->rot) * 0.004f * dt * 6.0f;
        float life = 8.0f + 12.0f * (0.5f + 0.5f * sinf(f->rot * 3.1f));
        float left = life - age;
        f->alpha = qlcg_clampf(qlcg_minf(age * 1.5f, left * 0.8f), 0.0f, 1.0f);
        if (left <= 0.0f) { f->used = false; continue; }
        /* 漂到干岸或未加载区淡出 */
        int tick = (int)(age * 4.0f);
        if ((tick & 3) == 0) {
            float wl = qlcg_region_water_at(reg, f->x - reg->center_x, f->z - reg->center_z);
            if (!(wl > -1e8f)) f->alpha *= 0.5f;
        }
    }
    v->ice_seed_timer += dt;
    if (v->ice_seed_timer >= 0.5f) {
        v->ice_seed_timer = 0.0f;
        seed_natural(tr, reg, w);
    }
}

void qlcg_ice_visual_draw_flow(TerrainRenderer *tr, const float *mvp, const float *cam,
                               const float *fog_col, float fog_near, float fog_far,
                               float light_scale,
                               const float *sun, const float *tint, float time) {
    IceVisual *v = &tr->ice_vis;
    (void)time;
    int counts[QLCG_ICE_FLOW_TEMPLATES] = {0, 0, 0, 0};
    int total = QLCG_ICE_FLOW_NATURAL + QLCG_ICE_FLOW_FRAGMENT;
    for (int i = 0; i < total; i++) if (v->ice_pool[i].used) counts[v->ice_pool[i].tpl & 3]++;
    if (!counts[0] && !counts[1] && !counts[2] && !counts[3]) return;
    float *buf = (float *)malloc((size_t)total * ICE_VIS_FLOATS * sizeof(float));
    if (!buf) return;
    int off[QLCG_ICE_FLOW_TEMPLATES];
    int base = 0;
    for (int t = 0; t < QLCG_ICE_FLOW_TEMPLATES; t++) { off[t] = base; base += counts[t]; }
    for (int i = 0; i < total; i++) {
        IceFlowInst *f = &v->ice_pool[i];
        if (!f->used) continue;
        int t = f->tpl & 3;
        float *p = buf + (size_t)(off[t]++) * ICE_VIS_FLOATS;
        p[0] = f->x;
        p[1] = f->y;
        p[2] = f->z;
        p[3] = f->rot;
        p[4] = f->scale;
        p[5] = f->thickness;
        p[6] = f->alpha;
        p[7] = f->rot * 0.6180339f + f->x * 0.0137f + f->z * 0.0071f;   /* 实例 seed */
    }
    glUseProgram(v->ice_prog);
    glUniformMatrix4fv(qlcg_gl_uniform(v->ice_prog, "uMVP"), 1, GL_FALSE, mvp);
    glUniform3f(qlcg_gl_uniform(v->ice_prog, "uCamPos"), cam[0], cam[1], cam[2]);
    glUniform3f(qlcg_gl_uniform(v->ice_prog, "uSunDir"), sun[0], sun[1], sun[2]);
    glUniform3f(qlcg_gl_uniform(v->ice_prog, "uFogCol"), fog_col[0], fog_col[1], fog_col[2]);
    glUniform2f(qlcg_gl_uniform(v->ice_prog, "uFogRange"), fog_near, fog_far);
    glUniform1f(qlcg_gl_uniform(v->ice_prog, "uLightScale"), light_scale);
    glUniform3f(qlcg_gl_uniform(v->ice_prog, "uTint"), tint[0], tint[1], tint[2]);
    glBindBuffer(GL_ARRAY_BUFFER, v->ice_inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(base * ICE_VIS_FLOATS * (int)sizeof(float)),
                 buf, GL_DYNAMIC_DRAW);
    free(buf);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    int cursor = 0;
    for (int t = 0; t < QLCG_ICE_FLOW_TEMPLATES; t++) {
        if (counts[t] <= 0) continue;
        glBindVertexArray(v->ice_tpl_vao[t]);
        glDrawElementsInstanced(GL_TRIANGLES, v->ice_tpl_idx[t], GL_UNSIGNED_SHORT, 0, counts[t]);
        cursor += counts[t];
    }
    (void)cursor;
}

void qlcg_ice_visual_draw_cracks(TerrainRenderer *tr, const float *mvp, const float *cam,
                                 const float *fog_col, float fog_near, float fog_far,
                                 float light_scale) {
    IceVisual *v = &tr->ice_vis;
    if (v->crack_count <= 0) return;
    float *buf = v->crack_buf;
    int n = 0;
    for (int i = 0; i < v->crack_count; i++) {
        const IceCrackInst *k = &v->cracks[i];
        float dmg = qlcg_clampf(k->damage, 0.0f, 1.0f);
        if (dmg <= 0.02f) continue;
        float size = k->size;
        float c = cosf(k->rot), s = sinf(k->rot);
        /* 三条折线：主裂 + 两条分支 */
        for (int seg = 0; seg < 3; seg++) {
            float ax, az, bx, bz;
            if (seg == 0) { ax = -size; az = 0.0f; bx = size; bz = 0.0f; }
            else if (seg == 1) { ax = -size * .2f; az = 0.0f; bx = size * .55f; bz = size * .5f; }
            else { ax = size * .2f; az = 0.0f; bx = -size * .45f; bz = -size * .55f; }
            float rax = ax * c - az * s, raz = ax * s + az * c;
            float rbx = bx * c - bz * s, rbz = bx * s + bz * c;
            float wx0 = k->x + rax - (float)tr->anchor_x;
            float wz0 = k->z + raz - (float)tr->anchor_z;
            float wx1 = k->x + rbx - (float)tr->anchor_x;
            float wz1 = k->z + rbz - (float)tr->anchor_z;
            float wy = k->y + 0.004f;
            if (n + 2 > QLCG_ICE_CRACK_MAX * 8) break;
            buf[n * 4 + 0] = wx0; buf[n * 4 + 1] = wy; buf[n * 4 + 2] = wz0; buf[n * 4 + 3] = 0.35f + dmg * 0.6f;
            n++;
            buf[n * 4 + 0] = wx1; buf[n * 4 + 1] = wy; buf[n * 4 + 2] = wz1; buf[n * 4 + 3] = 0.15f + dmg * 0.5f;
            n++;
        }
    }
    if (n <= 0) return;
    glUseProgram(v->crack_prog);
    glUniformMatrix4fv(qlcg_gl_uniform(v->crack_prog, "uMVP"), 1, GL_FALSE, mvp);
    glUniform3f(qlcg_gl_uniform(v->crack_prog, "uCamPos"), cam[0], cam[1], cam[2]);
    glUniform3f(qlcg_gl_uniform(v->crack_prog, "uFogCol"), fog_col[0], fog_col[1], fog_col[2]);
    glUniform2f(qlcg_gl_uniform(v->crack_prog, "uFogRange"), fog_near, fog_far);
    glUniform1f(qlcg_gl_uniform(v->crack_prog, "uLightScale"), light_scale);
    glBindVertexArray(v->crack_vao);
    glBindBuffer(GL_ARRAY_BUFFER, v->crack_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 4 * (int)sizeof(float)), buf, GL_DYNAMIC_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.5f);
    glDrawArrays(GL_LINES, 0, n);
    glLineWidth(1.0f);
}

int qlcg_ice_visual_flow_count(const TerrainRenderer *tr) {
    int n = 0;
    for (int i = 0; i < QLCG_ICE_FLOW_NATURAL + QLCG_ICE_FLOW_FRAGMENT; i++)
        if (tr->ice_vis.ice_pool[i].used) n++;
    return n;
}
