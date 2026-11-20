#ifndef QLCG_TERRAIN_SETTLEMENT_H
#define QLCG_TERRAIN_SETTLEMENT_H
#include "terrain_view.h"
void qlcg_terrain_settlement_draw(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w,GLuint program);
void qlcg_terrain_settlement_prepare_editor(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *world);
void qlcg_terrain_settlement_lights(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *world,float night);
void qlcg_terrain_settlement_begin(TerrainRenderer *tr);
void qlcg_terrain_settlement_add(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w,int slot);
bool qlcg_terrain_settlement_needs_refresh(const TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w);
void qlcg_terrain_settlement_free(TerrainRenderer *tr);
int qlcg_terrain_settlement_visible(const TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w,int *slots,const TerritoryFaction **factions,int capacity);
#endif
