#ifndef QLCG_GUAN_MODEL_H
#define QLCG_GUAN_MODEL_H

#include "../../plant_arch_internal.h"

typedef enum {
    SHRUB_WOODY, SHRUB_THORNY, SHRUB_FEATHER, SHRUB_PANICLE, SHRUB_CATKIN,
    SHRUB_CAUDEX, SHRUB_PENCIL, SHRUB_JOINTED, SHRUB_ROSETTE, SHRUB_COLUMN,
    SHRUB_ARCHING, SHRUB_BRAMBLE
} ShrubHabit;

typedef struct {
    ShrubHabit habit;
    int stems, forks;
    double reach, bend, leaf, width, winter_leaf;
    int cold, heat, bloom_season;
    double bloom_size;
    double crown_height;
    int leaf_sites;
    double height_scale, spread_scale;
    int rosette_leaves;
    void (*build_fruit)(LpBuilder *, const PdDef *, const double[3], double, LpRng *);
} ShrubProfile;

void qlcg_pa_build_shrub(LpBuilder *b, const PdDef *def, int season, int form,
                         const ShrubProfile *p);

#endif
