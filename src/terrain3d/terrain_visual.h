#ifndef QLCG_TERRAIN_VISUAL_H
#define QLCG_TERRAIN_VISUAL_H
#include "terrain_view.h"
#include "sky_climate.h"
#include "paint_uniforms.h"

typedef enum {
    QL_VIS_TERRAIN, QL_VIS_PLANT, QL_VIS_WATER, QL_VIS_ICE,
    QL_VIS_PROP, QL_VIS_FLAG, QL_VIS_FAR, QL_VIS_ICE_FLOW, QL_VIS_CRACK,
    QL_VIS_SPRING, QL_VIS_WEATHER,
    QL_VIS_COUNT
} TerrainVisualPass;

typedef struct {
    const GameSettings *settings;
    SkyClimate sky;
    WeatherProfile weather;
    float camera[3], vp[16], fog[2], clip_far, time;
    int width, height;
    GLint output,viewport[4];
    bool paint, enhanced, underwater;
    TerrainPaintUniforms colors;
} TerrainVisualFrame;

bool qlcg_visual_paint_target_ready(TerrainRenderer *tr,int width,int height,int samples);
bool qlcg_visual_paint_ready(TerrainRenderer *tr);
void qlcg_visual_begin(TerrainRenderer *tr, const RegionData *reg,
    const GameSettings *s, const SkyClimate *sky, const WeatherProfile *weather,
    const float *camera, const float *vp, float fog_near, float fog_far,
    float clip_far, float time, bool underwater, TerrainVisualFrame *frame);
void qlcg_visual_resume(TerrainRenderer *tr, const TerrainVisualFrame *frame);
GLuint qlcg_visual_program(const TerrainRenderer *tr, GLuint native, TerrainVisualPass pass);
void qlcg_visual_program_init(GLuint program);
void qlcg_visual_bind(TerrainRenderer *tr, GLuint program);
void qlcg_visual_clouds_prepare(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *world);
void qlcg_visual_clouds_bind(TerrainRenderer *tr,GLuint program);
bool qlcg_visual_sky(TerrainRenderer *tr, const float *forward, const float *right,
    const float *up, float aspect, float tan_half, const AstraFrame *astronomy);
void qlcg_visual_strokes(TerrainRenderer *tr, const RegionData *reg);
void qlcg_visual_end(TerrainRenderer *tr);
void qlcg_visual_reset(TerrainRenderer *tr);
void qlcg_visual_free(TerrainRenderer *tr);
#endif
