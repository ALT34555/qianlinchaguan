#ifndef QLCG_TERRAIN_TOOLS_H
#define QLCG_TERRAIN_TOOLS_H

#include "terrain_view.h"

typedef struct {
    InventoryItem item;
    float cx, cy, size, radians;
} TerrainHandSprite;

unsigned qlcg_terrain_hand_sprites(const TerrainRenderer *tr,const MacroWorld *w,
                                   float fov,float width,float height,TerrainHandSprite out[2]);
float qlcg_terrain_hand_swing(const TerrainRenderer *tr,int hand,double now,InventoryItem *active);
void qlcg_terrain_tools_world_draw(TerrainRenderer *tr,const MacroWorld *w,const float vp[16],
                                   const float wrists[2][3],float body_scale,float daylight);

const char *qlcg_terrain_tool_use(TerrainRenderer *tr, RegionData *reg, MacroWorld *w,
                                  const GameSettings *settings, bool off_hand, bool *worked);
void qlcg_terrain_tools_draw(TerrainRenderer *tr, const MacroWorld *w, const float proj[16],
                             float daylight);
void qlcg_terrain_tools_free(TerrainRenderer *tr);

#endif
