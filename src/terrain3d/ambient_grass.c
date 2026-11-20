#include "ambient_grass.h"
#include "ambient_grass_shaders.h"
#include "../core/math_util.h"
#include "../systems/world/gen/gen_cover.h"
#include "../systems/world/model/plant_phenology.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

const QlcgGrassShape qlcg_grass_seasons[4] = {
    {0.66f, 0.69f, 0.83f, 0.62f, 0.00f},
    {1.00f, 1.00f, 1.12f, 1.00f, 0.00f},
    {0.43f, 0.83f, 0.76f, 1.35f, 0.00f},
    {0.00f, 0.36f, 0.73f, 0.87f, 0.64f}
};

bool qlcg_grass_mesh_init(QlcgGrassMesh *mesh, int capacity, bool local_season) {
    // 4片交叉草叶网格
    static const float ANGLE[4] = {0.0f, 1.30f, 3.04f, 4.62f};
    static const float HEIGHT[4] = {0.86f, 0.69f, 0.78f, 0.61f};
    float verts[4 * 4 * 6];
    int nv = 0;
    for (int b = 0; b < 4; b++) {
        float dx = cosf(ANGLE[b]), dz = sinf(ANGLE[b]);
        float bnx = -dz, bnz = dx;
        float px = bnx * 0.08f, pz = bnz * 0.08f;
        float quad[4][3] = {
            { -px * 0.55f, 0.0f, -pz * 0.55f },
            {  px * 0.55f, 0.0f,  pz * 0.55f },
            {  px * 0.08f, HEIGHT[b],  pz * 0.08f },
            { -px * 0.08f, HEIGHT[b], -pz * 0.08f }
        };
        for (int v = 0; v < 4; v++) {
            float top = quad[v][1] > 0.3f ? 1.0f : 0.0f;
            verts[nv * 6 + 0] = quad[v][0] + dx * quad[v][1] * 0.16f;
            verts[nv * 6 + 1] = quad[v][1];
            verts[nv * 6 + 2] = quad[v][2] + dz * quad[v][1] * 0.16f;
            verts[nv * 6 + 3] = bnx;
            verts[nv * 6 + 4] = bnz;
            verts[nv * 6 + 5] = top;
            nv++;
        }
    }
    unsigned short idx[4 * 6];
    int ni = 0;
    for (int b = 0; b < 4; b++) {
        unsigned short o = (unsigned short)(b * 4);
        idx[ni++] = o + 0; idx[ni++] = o + 1; idx[ni++] = o + 2;
        idx[ni++] = o + 0; idx[ni++] = o + 2; idx[ni++] = o + 3;
    }

    glGenVertexArrays(1, &mesh->vao);
    glBindVertexArray(mesh->vao);
    glGenBuffers(1, &mesh->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));

    glGenBuffers(1, &mesh->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    glGenBuffers(1, &mesh->instance_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->instance_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(capacity * sizeof(QlcgGrassInstance)), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(QlcgGrassInstance), (void *)offsetof(QlcgGrassInstance, x));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(QlcgGrassInstance), (void *)offsetof(QlcgGrassInstance, rot));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(QlcgGrassInstance), (void *)offsetof(QlcgGrassInstance, root));
    glVertexAttribDivisor(4, 1);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(QlcgGrassInstance), (void *)offsetof(QlcgGrassInstance, tip));
    glVertexAttribDivisor(5, 1);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(QlcgGrassInstance), (void *)offsetof(QlcgGrassInstance, norm));
    glVertexAttribDivisor(6, 1);
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(QlcgGrassInstance), (void *)offsetof(QlcgGrassInstance, season));
    glVertexAttribDivisor(7, 1);
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, sizeof(QlcgGrassInstance), (void *)offsetof(QlcgGrassInstance, water));
    glVertexAttribDivisor(8, 1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    mesh->program = qlcg_gl_compile_program(GRASS_VS, GRASS_FS);
    if (!mesh->program) {
        qlcg_grass_mesh_free(mesh);
        return false;
    }
    qlcg_visual_program_init(mesh->program);
    glUniform1f(qlcg_gl_uniform(mesh->program, "uRankSpan"), QLCG_GRASS_RANK_SPAN);
    glUniform1i(qlcg_gl_uniform(mesh->program, "uLocalSeason"), local_season);
    static const float world_density[4] = {0.43f, 1.00f, 0.20f, 0.00f};
    for (int s = 0; s < 4; s++) {
        char uniform[18];
        snprintf(uniform, sizeof(uniform), "uSeasonShape[%d]", s);
        const QlcgGrassShape *shape = &qlcg_grass_seasons[s];
        glUniform4f(qlcg_gl_uniform(mesh->program, uniform),
                    world_density[s], shape->height, shape->width, shape->lean);
    }
    glUseProgram(0);
    mesh->index_count = ni;
    return true;
}

void qlcg_grass_mesh_free(QlcgGrassMesh *mesh) {
    if (mesh->vao) qlcg_gl_delete_vertex_arrays(1, &mesh->vao);
    const GLuint buffers[] = {mesh->vbo, mesh->ibo, mesh->instance_vbo};
    qlcg_gl_delete_buffers(3, buffers);
    if (mesh->program) qlcg_gl_delete_program(mesh->program);
    *mesh = (QlcgGrassMesh){0};
}

uint8_t qlcg_grass_unorm(float value) {
    return (uint8_t)(qlcg_clampf(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// 地表草层季色
static void cover_grass_color(float pos, float arid, int season, float ampl,
                              bool seasonless, float out[3]) {
    static const float TONE[5][3] = {
        {138.0f / 255.0f, 146.0f / 255.0f, 126.0f / 255.0f},
        {130.0f / 255.0f, 144.0f / 255.0f, 118.0f / 255.0f},
        {122.0f / 255.0f, 138.0f / 255.0f,  92.0f / 255.0f},
        {122.0f / 255.0f, 140.0f / 255.0f,  86.0f / 255.0f},
        {140.0f / 255.0f, 152.0f / 255.0f,  88.0f / 255.0f}
    };
    static const float SEA_RGB[4][3] = {
        {120.0f / 255.0f, 146.0f / 255.0f,  82.0f / 255.0f},
        { 92.0f / 255.0f, 120.0f / 255.0f,  68.0f / 255.0f},
        {172.0f / 255.0f, 142.0f / 255.0f,  84.0f / 255.0f},
        {146.0f / 255.0f, 142.0f / 255.0f, 124.0f / 255.0f}
    };
    static const float SEA_AMT[4] = { 0.36f, 0.30f, 0.62f, 0.72f };
    static const float DRY_RGB[3] = { 179.0f / 255.0f, 154.0f / 255.0f, 108.0f / 255.0f };
    float p = qlcg_clampf(pos, 0.0f, 4.0f);
    int i = qlcg_clampi((int)p, 0, 3);
    float f = qlcg_clampf(p - (float)i, 0.0f, 1.0f);
    for (int k = 0; k < 3; k++) out[k] = qlcg_lerpf(TONE[i][k], TONE[i + 1][k], f);
    float dryk = (1.0f - qlcg_smoothf(0.22f, 0.55f, arid)) * 0.55f;
    for (int k = 0; k < 3; k++) out[k] = qlcg_lerpf(out[k], DRY_RGB[k], dryk);
    int s = qlcg_clampi(season, 0, 3);
    float amp_k = seasonless ? 0.15f : qlcg_clampf(0.45f + 0.55f * ampl, 0.0f, 1.0f);
    float sq = amp_k * SEA_AMT[s];
    for (int k = 0; k < 3; k++) out[k] = qlcg_lerpf(out[k], SEA_RGB[s][k], sq);
}

// 五带植物四季色板
static void band_plant_color(float pos, int season, float out[3]) {
    static const float PAL[5][4][3] = {
        {{0.46f, 0.54f, 0.40f}, {0.40f, 0.49f, 0.36f}, {0.58f, 0.52f, 0.34f}, {0.54f, 0.53f, 0.46f}},
        {{0.44f, 0.64f, 0.28f}, {0.31f, 0.49f, 0.22f}, {0.76f, 0.59f, 0.22f}, {0.47f, 0.45f, 0.31f}},
        {{0.46f, 0.69f, 0.28f}, {0.35f, 0.53f, 0.22f}, {0.75f, 0.55f, 0.19f}, {0.49f, 0.47f, 0.29f}},
        {{0.43f, 0.65f, 0.26f}, {0.30f, 0.50f, 0.21f}, {0.56f, 0.56f, 0.22f}, {0.39f, 0.45f, 0.26f}},
        {{0.32f, 0.57f, 0.24f}, {0.25f, 0.48f, 0.20f}, {0.45f, 0.54f, 0.20f}, {0.35f, 0.46f, 0.24f}}
    };
    int s = qlcg_clampi(season, 0, 3);
    float p = qlcg_clampf(pos, 0.0f, 4.0f);
    int i = qlcg_clampi((int)p, 0, 3);
    float f = qlcg_clampf(p - (float)i, 0.0f, 1.0f);
    for (int k = 0; k < 3; k++) out[k] = qlcg_lerpf(PAL[i][s][k], PAL[i + 1][s][k], f);
}

// 计算草尖区域季色与霜度
void qlcg_grass_tip_color(float temp, float arid, float ampl,
                               const QlcgClimateSeason *cs, float c_mean,
                               float salt_w, float vc_min, float t_var,
                               uint32_t seed, float out[3], float *out_frost) {
    static const float DRY_FOL[4][3] = {
        {0.68f, 0.64f, 0.38f}, {0.71f, 0.64f, 0.37f},
        {0.79f, 0.67f, 0.34f}, {0.62f, 0.58f, 0.39f}
    };
    static const float SALT_FOL[4][3] = {
        {0.44f, 0.63f, 0.55f}, {0.37f, 0.53f, 0.47f},
        {0.56f, 0.63f, 0.41f}, {0.31f, 0.45f, 0.40f}
    };
    float pos = qlcg_cover_band_of_temp(temp);
    int vsea = qlcg_plant_visual_season(cs, temp, seed);
    float colA[3], colB[3];
    cover_grass_color(pos, arid, vsea, ampl, cs->seasonless != 0, colA);
    band_plant_color(pos, vsea, colB);
    float dry_w = 1.0f - qlcg_smoothf(0.18f, 0.62f, arid);
    for (int k = 0; k < 3; k++) colB[k] = qlcg_lerpf(colB[k], DRY_FOL[vsea][k], dry_w);
    if (salt_w > 0.01f) {
        for (int k = 0; k < 3; k++) colB[k] = qlcg_lerpf(colB[k], SALT_FOL[vsea][k], salt_w);
    }
    float shade = 0.91f + 0.18f * t_var;
    for (int k = 0; k < 3; k++) out[k] = (colA[k] * 0.42f + colB[k] * 0.58f) * shade;

    float t_loc = temp + (cs->temperature - c_mean);
    float snow_g = qlcg_smoothf(0.70f, 0.88f, vc_min);
    float frost_t = 1.0f - qlcg_smoothf(-6.0f, 1.5f, t_loc);
    float frost = fmaxf(snow_g * 0.85f, frost_t * 0.72f);
    if (frost > 0.01f) {
        static const float FROST[3] = { 0.91f, 0.94f, 0.98f };
        for (int k = 0; k < 3; k++) out[k] = qlcg_lerpf(out[k], FROST[k], frost);
    }
    *out_frost = frost;
}

#define GRASS_DRAW_CAP 32768
#define GRASS_CACHE_STEP 8.0f

typedef struct TerrainGrass {
    QlcgGrassMesh mesh;
    QlcgGrassInstance *instances;
    uint64_t revision, mesh_version;
    const RegionStream *stream;
    double anchor_x, anchor_z;
    float x, z, distance;
    int day, forced, count;
    bool shader_failed;
} TerrainGrass;

static float season_phase(QlcgClimateSeason *cs, bool has_calendar, int forced) {
    if (forced >= 0) {
        cs->season = forced;
        cs->seasonless = 1;
        return (float)forced;
    }
    float phase = (float)cs->season;
    if (cs->seasonless) {
        if (!has_calendar) return phase;
        cs->season = (int)floorf(cs->phase + 0.5f) % 4;
        return cs->phase;
    }
    float window = fminf(10.0f, (cs->since_days + cs->until_days) * 0.5f);
    if (window <= 0.0f) return phase;
    if (cs->since_days < window) phase -= 0.5f * (1.0f - cs->since_days / window);
    if (cs->until_days < window) phase += 0.5f * (1.0f - cs->until_days / window);
    return phase;
}

static void grass_instance(QlcgGrassInstance *out, const GrassTuft *p, const RegionData *c,
                           const QlcgClimateSeason *cs, float phase, float mean, float moisture,
                           float ampl, bool saline, float thermal_shift, float ox, float oz) {
    float half = (c->width - 1) * c->step * 0.5f;
    float gx = (p->x + half) / c->step, gz = (p->z + half) / c->step;
    int ix = (int)gx, iz = (int)gz;
    float x = gx - ix, z = gz - iz;
    int k = iz * c->width + ix;
    int ids[4] = {k, k + 1, k + c->width, k + c->width + 1};
    float weights[4] = {(1.0f-x)*(1.0f-z), x*(1.0f-z), (1.0f-x)*z, x*z};
    float normal[3] = {0, 1, 0};
    if (x + z <= 1.0f) {
        normal[0] = (c->heights[k] - c->heights[ids[1]]) / c->step;
        normal[2] = (c->heights[k] - c->heights[ids[2]]) / c->step;
    } else {
        normal[0] = (c->heights[ids[2]] - c->heights[ids[3]]) / c->step;
        normal[2] = (c->heights[ids[1]] - c->heights[ids[3]]) / c->step;
    }
    qlcg_vec3_norm(&normal[0], &normal[1], &normal[2]);

    float root[3] = {0};
    for (int j = 0; j < 4; j++)
        for (int channel = 0; channel < 3; channel++)
            root[channel] += c->colors[ids[j] * 3 + channel] * weights[j] / 255.0f;
    float luminance = 0.25f * root[0] + 0.55f * root[1] + 0.20f * root[2];
    float exposure = 1.0f - qlcg_smoothf(0.25f, 0.71f, normal[1]);
    float rock_tint = 0.40f + (luminance - 0.40f) * 0.40f;
    root[0] = qlcg_lerpf(root[0], rock_tint * 1.06f, exposure);
    root[1] = qlcg_lerpf(root[1], rock_tint * 1.02f, exposure);
    root[2] = qlcg_lerpf(root[2], rock_tint * 0.97f, exposure);

    *out = (QlcgGrassInstance){
        .x = ox + p->x, .y = p->y, .z = oz + p->z, .scale = p->scale, .season = phase,
        .rot = ((p->seed >> 8) & 255u) * (2.0f * QLCG_PI / 255.0f)
    };
    float tip[3], frost = 0.0f;
    float variation = ((p->seed >> 12) & 63u) / 63.0f;
    if (p->water_kind != WATER_KIND_DRY) {
        static const float tips[2][3] = {{0.37f, 0.65f, 0.29f}, {0.27f, 0.62f, 0.43f}};
        const float *color = tips[p->water_kind == WATER_KIND_SEA || saline];
        float tone = 0.84f + 0.27f * variation;
        for (int j = 0; j < 3; j++) tip[j] = color[j] * tone;
        out->water[0] = 1.0f;
        float annual = 0.0f;
        for (int j = 0; j < 4; j++) {
            out->water[1] += c->flow_x[ids[j]] * weights[j];
            out->water[2] += c->flow_z[ids[j]] * weights[j];
            annual += c->temperatures[ids[j]] * weights[j];
        }
        float response = p->water_kind == WATER_KIND_SEA ? 0.43f : 0.72f;
        float water_temp = annual + thermal_shift * response;
        out->water[3] = 0.26f + 0.74f * qlcg_smoothf(1.0f, 22.0f, water_temp)
            - 0.29f * qlcg_smoothf(32.0f, 41.0f, water_temp);
        float flow = 1.0f + hypotf(out->water[1], out->water[2]);
        out->water[1] /= flow;
        out->water[2] /= flow;
    } else {
        float temp = c->temperatures[k];
        float arid = c->aridities[k] * (1.0f + 0.20f*c->forest[k] + 0.15f*c->wetness[k] - 0.35f*c->desert[k]);
        static const float dry_by_season[4] = {0.15f, -0.30f, -0.05f, 0.30f};
        arid = fmaxf(0.05f, arid * (1.0f + (cs->moisture - moisture)*0.65f
            - dry_by_season[cs->season]*(1.0f - ampl)*0.55f));
        float salt = saline ? 1.0f : 0.0f;
        if (!saline && c->source_effects && c->source_effects[k].salt >= 32)
            salt = fminf(1.0f, c->source_effects[k].salt / 128.0f);
        qlcg_grass_tip_color(temp, arid, ampl, cs, mean, salt,
            fminf(root[0], fminf(root[1], root[2])), variation, p->seed, tip, &frost);
    }
    for (int j = 0; j < 3; j++) {
        out->root[j] = qlcg_grass_unorm(root[j]);
        out->tip[j] = qlcg_grass_unorm(tip[j]);
        out->norm[j] = qlcg_grass_unorm(normal[j] * 0.5f + 0.5f);
    }
    out->tip[3] = qlcg_grass_unorm(frost);
    out->norm[3] = p->rank;
}

static void sync_instances(TerrainGrass *grass, TerrainRenderer *tr, const RegionStream *st,
                           const MacroWorld *w, const float cam[3], float distance) {
    const CalendarSnapshot *date = w->plant_calendar;
    int day = date ? date->jdn : 0;
    int forced = w->plant_season_forced ? qlcg_clampi(tr->plant_season, 0, 3) : -1;
    float dx = cam[0] - grass->x, dz = cam[2] - grass->z;
    if (grass->stream == st && grass->revision == st->revision && grass->mesh_version == tr->mesh_version &&
        grass->anchor_x == tr->anchor_x && grass->anchor_z == tr->anchor_z &&
        grass->day == day && grass->forced == forced && grass->distance == distance &&
        dx*dx + dz*dz < GRASS_CACHE_STEP*GRASS_CACHE_STEP) return;

    const float range = distance + GRASS_CACHE_STEP;
    grass->count = 0;
    for (int i = 0; i < st->count && grass->count < GRASS_DRAW_CAP; i++) {
        int slot = st->active[i];
        const RegionData *c = st->slots[slot].data;
        if (!c->grass_count || !tr->chunks[slot].terrain_ready ||
            tr->chunks[slot].revision != c->mesh_revision) continue;
        float ox = (float)((double)c->center_x - tr->anchor_x);
        float oz = (float)((double)c->center_z - tr->anchor_z);
        float bx = fmaxf(0, fabsf(ox - cam[0]) - CHUNK_SIZE * 0.5f);
        float bz = fmaxf(0, fabsf(oz - cam[2]) - CHUNK_SIZE * 0.5f);
        if (bx*bx + bz*bz > range*range) continue;

        int cell = c->site_cell;
        bool valid = cell >= 0 && cell < w->cell_count;
        float mean = valid && w->temperature ? w->temperature[cell] : 15.0f;
        float moisture = valid && w->moisture ? w->moisture[cell] : 0.5f;
        float ampl = qlcg_world_flat(w) ? 0.65f : fabsf(sinf(-c->center_z / qlcg_world_depth(w) * QLCG_PI));
        bool saline = (valid && w->saline && w->saline[cell]) || qlcg_chunk_material(c->site_chunk_type) == 3;
        QlcgClimateSeason cs;
        qlcg_climate_season_at(w, cell, date, &cs);
        static const float forced_temperature[4] = {0.0f, 1.0f, 0.0f, -1.0f};
        float thermal_shift = forced >= 0 ? cs.amplitude * forced_temperature[forced]
            : valid ? cs.temperature - mean : 0.0f;
        float phase = season_phase(&cs, date != NULL && valid, forced);
        for (int j = 0; j < c->grass_count && grass->count < GRASS_DRAW_CAP; j++) {
            const GrassTuft *p = &c->grass[j];
            float x = ox + p->x - cam[0], z = oz + p->z - cam[2];
            if (x*x + z*z > range*range) continue;
            if (!grass->instances) {
                grass->instances = malloc(GRASS_DRAW_CAP * sizeof(*grass->instances));
                if (!grass->instances) return;
            }
            grass_instance(&grass->instances[grass->count++], p, c, &cs, phase, mean, moisture,
                           ampl, saline, thermal_shift, ox, oz);
        }
    }
    if (grass->count) {
        if (!grass->mesh.program && !qlcg_grass_mesh_init(&grass->mesh, GRASS_DRAW_CAP, true)) {
            grass->shader_failed = true;
            return;
        }
        glBindBuffer(GL_ARRAY_BUFFER, grass->mesh.instance_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, grass->count * sizeof(*grass->instances), grass->instances);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    grass->stream = st;
    grass->revision = st->revision;
    grass->mesh_version = tr->mesh_version;
    grass->anchor_x = tr->anchor_x;
    grass->anchor_z = tr->anchor_z;
    grass->x = cam[0];
    grass->z = cam[2];
    grass->distance = distance;
    grass->day = day;
    grass->forced = forced;
}

void qlcg_terrain_grass_draw(TerrainRenderer *tr, const RegionData *reg, const MacroWorld *w,
                            const TerrainVisualFrame *frame, const float relative_mvp[16],
                            float ground_band, const float fog_color[3]) {
    if (tr->hide_guild_grass || tr->reflection_pass || frame->settings->scroll_style) return;
    if (!tr->grass) tr->grass = calloc(1, sizeof(*tr->grass));
    TerrainGrass *grass = tr->grass;
    if (!grass || grass->shader_failed) return;
    int vd = qlcg_clampi(frame->settings->vegetation_distance, 0, reg->stream->radius);
    float distance = fminf(150.0f, ((float)vd + 0.5f) * CHUNK_SIZE);
    const float *cam = frame->camera;
    sync_instances(grass, tr, reg->stream, w, cam, distance);
    GLuint program = grass->mesh.program;
    if (!grass->count || !program) return;

    qlcg_visual_bind(tr, program);
    const SkyClimate *sky = &frame->sky;
    glUniformMatrix4fv(qlcg_gl_uniform(program, "uMVP"), 1, GL_FALSE, relative_mvp);
    glUniform3f(qlcg_gl_uniform(program, "uCamPos"), cam[0], cam[1], cam[2]);
    glUniform2f(qlcg_gl_uniform(program, "uFadeRange"), distance * (115.0f / 150.0f), distance);
    glUniform3f(qlcg_gl_uniform(program, "uSunDir"), sky->sun_dir[0], sky->sun_dir[1], sky->sun_dir[2]);
    glUniform1f(qlcg_gl_uniform(program, "uLightScale"), sky->light_scale);
    glUniform3f(qlcg_gl_uniform(program, "uTint"), sky->tint[0], sky->tint[1], sky->tint[2]);
    glUniform3f(qlcg_gl_uniform(program, "uFogCol"), fog_color[0], fog_color[1], fog_color[2]);
    glUniform2f(qlcg_gl_uniform(program, "uFogRange"), frame->fog[0], frame->fog[1]);
    glUniform1i(qlcg_gl_uniform(program, "uUnderwater"), frame->underwater);
    glUniform1f(qlcg_gl_uniform(program, "uStraw"), qlcg_grass_seasons[3].straw);
    glUniform1f(qlcg_gl_uniform(program, "uTipSnow"), frame->weather.snow * 0.55f);

    static const float wind[6][2] = {
        {0.000f, 0.000f}, {0.014f, 0.004f}, {0.036f, 0.010f},
        {0.062f, 0.018f}, {0.108f, 0.028f}, {0.155f, 0.042f}
    };
    int level = qlcg_natural_physics_level(frame->weather.id);
    if (tr->wind_strength >= 2.0f) level = 5;
    else if (tr->wind_strength >= 1.0f && level < 4) level = 4;
    level = frame->settings->natural_physics ? qlcg_clampi(level, 0, 5) : 0;
    glUniform1f(qlcg_gl_uniform(program, "uTime"), frame->time);
    glUniform2f(qlcg_gl_uniform(program, "uWind"), wind[level][0], wind[level][1]);
    glUniform1f(qlcg_gl_uniform(program, "uWaterMotion"), frame->settings->natural_physics ? 1.0f : 0.0f);
    glUniform1f(qlcg_gl_uniform(program, "uGroundMix"), ground_band - (int)ground_band);
    glUniform1f(qlcg_gl_uniform(program, "uGroundDetail"), 1.0f);
    glUniform2f(qlcg_gl_uniform(program, "uGroundAnchor"),
        (float)fmod(tr->anchor_x, QLCG_GROUND_TEX_SPAN), (float)fmod(tr->anchor_z, QLCG_GROUND_TEX_SPAN));
    glUniform1f(qlcg_gl_uniform(program, "uGroundSpan"), QLCG_GROUND_TEX_SPAN);

    bool cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(grass->mesh.vao);
    glDrawElementsInstanced(GL_TRIANGLES, grass->mesh.index_count, GL_UNSIGNED_SHORT, 0, grass->count);
    glBindVertexArray(0);
    if (cull) glEnable(GL_CULL_FACE);
}

void qlcg_terrain_grass_free(TerrainRenderer *tr) {
    if (!tr->grass) return;
    qlcg_grass_mesh_free(&tr->grass->mesh);
    free(tr->grass->instances);
    free(tr->grass);
    tr->grass = NULL;
}
