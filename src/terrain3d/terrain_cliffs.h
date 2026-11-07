#ifndef QLCG_TERRAIN_CLIFFS_H
#define QLCG_TERRAIN_CLIFFS_H

#include "terrain_types.h"
#include "../systems/world/planet_macro.h"

typedef struct {
    TerrainVert *vertices;
    int count, capacity, patches;
} TerrainCliffMesh;

typedef struct { int x,z; float dx,dz,score; } TerrainCliffSite;

void qlcg_terrain_cliff_site(TerrainCliffSite sites[4], int x, int z, float dx, float dz);

void qlcg_terrain_cliffs_reset(void);
void qlcg_terrain_cliffs_build(TerrainCliffMesh *mesh,const RegionData *reg,const MacroWorld *world,
    const TerrainCliffSite sites[4],double ox,double oz);

#endif
