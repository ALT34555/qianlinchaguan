#ifndef QLCG_HYDROLOGY_FIELDS_H
#define QLCG_HYDROLOGY_FIELDS_H
#include "hydrology_archive.h"
#include "hydrology_types.h"
#include "hydrology_junction.h"
void qlcg_river_RiverJunction(RiverBuffer *b, RiverJunction *v);
void qlcg_river_RiverMouthPoint(RiverBuffer *b, RiverMouthPoint *v);
void qlcg_river_SurfaceFrame(RiverBuffer *b, SurfaceFrame *v);
void qlcg_river_OxbowPlan(RiverBuffer *b, OxbowPlan *v);
void qlcg_river_values(RiverBuffer *b, void *data, size_t count, unsigned width);
void qlcg_river_padding(RiverBuffer *b, size_t count);
#endif
