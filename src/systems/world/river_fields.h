#ifndef QLCG_RIVER_FIELDS_H
#define QLCG_RIVER_FIELDS_H
#include "river_archive.h"
#include "source_rivers.h"
void qlcg_river_RiverJunction(RiverBuffer *b, RiverJunction *v);
void qlcg_river_SourceMaterial(RiverBuffer *b, SourceMaterial *v);
void qlcg_river_RiverSource(RiverBuffer *b, RiverSource *v);
void qlcg_river_RiverMouthPoint(RiverBuffer *b, RiverMouthPoint *v);
void qlcg_river_SurfaceFrame(RiverBuffer *b, SurfaceFrame *v);
void qlcg_river_OxbowPlan(RiverBuffer *b, OxbowPlan *v);
void qlcg_river_values(RiverBuffer *b, void *data, size_t count, unsigned width);
void qlcg_river_padding(RiverBuffer *b, size_t count);
#endif
