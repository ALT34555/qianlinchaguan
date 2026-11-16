#ifndef QLCG_WATER_REFLECTION_H
#define QLCG_WATER_REFLECTION_H
#include "terrain_view.h"
void qlcg_reflection_free(TerrainRenderer *tr);
void qlcg_reflection_collect(WaterReflection choices[2],const WaterReflection old[2],const RegionData *region,
    const TerrainChunkGPU *chunk,float ox,float oz,const float camera[3],const float forward[3],const float mvp[16]);
void qlcg_reflection_render(TerrainRenderer *tr,const WaterReflection choices[2],const RegionData *reg,
    const MacroWorld *world,const CalendarClock *clock,int utc,const GameSettings *settings,int width,int height);
void qlcg_reflection_uniforms(TerrainRenderer *tr);
void qlcg_reflection_clip(float projection[16],const float view[16],float camera_y,float plane);
#endif
