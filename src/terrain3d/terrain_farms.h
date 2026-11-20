#ifndef QLCG_TERRAIN_FARMS_H
#define QLCG_TERRAIN_FARMS_H
#include "terrain_view.h"
void qlcg_terrain_farms_prepare(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *world,int vegetation_form);
void qlcg_terrain_farms_draw(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *world);
void qlcg_terrain_farms_free(TerrainRenderer *tr);
#endif
