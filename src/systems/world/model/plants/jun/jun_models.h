#ifndef QLCG_JUN_MODELS_H
#define QLCG_JUN_MODELS_H

#include "../../plant_arch_internal.h"

typedef struct {
    const char *cap, *rim, *gills, *stem, *veil, *spots;
    double crown, fan, width, stem_width, skirt, spot_size;
    int bulb;
} MushroomShape;

typedef struct {
    double x, z, height, size, angle, crown, lean;
} MushroomBody;

void qlcg_pa_build_mushrooms(LpBuilder *b, const PdDef *def, int season, int form,
                             const MushroomShape *shape, const MushroomBody *bodies, int count);

#endif
