#ifndef QLCG_AMBIENT_GRASS_H
#define QLCG_AMBIENT_GRASS_H

#include "terrain_visual.h"
#include "../systems/world/climate_season.h"

#define QLCG_GRASS_RANK_SPAN 0.86f

typedef struct {
    float x, y, z, rot, scale;
    uint8_t root[4], tip[4], norm[4];
    float season;
    float water[4]; /* 水草标记、流速、疏密 */
} QlcgGrassInstance;

typedef struct {
    float density, height, width, lean, straw;
} QlcgGrassShape;

typedef struct {
    GLuint vao, vbo, ibo, instance_vbo, program;
    int index_count;
} QlcgGrassMesh;

extern const QlcgGrassShape qlcg_grass_seasons[4];

bool qlcg_grass_mesh_init(QlcgGrassMesh *mesh, int capacity, bool local_season);
void qlcg_grass_mesh_free(QlcgGrassMesh *mesh);
uint8_t qlcg_grass_unorm(float value);
void qlcg_grass_tip_color(float temp, float arid, float ampl, const QlcgClimateSeason *cs,
                         float mean, float salt, float root_min, float variation,
                         uint32_t seed, float tip[3], float *frost);
void qlcg_terrain_grass_draw(TerrainRenderer *tr, const RegionData *reg, const MacroWorld *w,
                            const TerrainVisualFrame *frame, const float relative_mvp[16],
                            float ground_band, const float fog_color[3]);
void qlcg_terrain_grass_free(TerrainRenderer *tr);

#endif
