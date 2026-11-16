#ifndef QLCG_PAINT_SURFACE_H
#define QLCG_PAINT_SURFACE_H
#include "terrain_view.h"
#include "terrain_types.h"
typedef struct PaintSurfaceCache PaintSurfaceCache;
bool qlcg_paint_surface_sync(PaintSurfaceCache **cache,const MacroWorld *world);
int qlcg_paint_surface_material(PaintSurfaceCache *cache,const MacroWorld *world,int cx,int cz);
void qlcg_paint_surface_blend(TerrainVert *v,const RegionData *reg,int sample,int season,const uint8_t materials[9]);
void qlcg_paint_surface_weights(TerrainVert *v,const RegionData *reg,int sample,int season);
void qlcg_terrain_vertex_layout(size_t base);
#endif
