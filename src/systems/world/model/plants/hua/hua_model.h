#ifndef QLCG_HUA_MODEL_H
#define QLCG_HUA_MODEL_H

#include "../../plant_arch_internal.h"

enum { HUA_TREE, HUA_SHRUB, HUA_HERB, HUA_ORCHID, HUA_LOTUS, HUA_PINE };
enum { HUA_OPEN, HUA_DOUBLE, HUA_RAY, HUA_LIP, HUA_COLUMN };
enum {
    HUA_DRUPE, HUA_HIP, HUA_CAPSULE, HUA_FOLLICLES,
    HUA_ACHENES, HUA_GRAINS, HUA_CONE, HUA_RECEPTACLE
};

typedef struct {
    int form, divisions;
    double radius, aspect;
    const char *color;
} HuaSeedProfile;

typedef struct {
    int habit, flower, bloom_season, stems, petals, rings;
    double crown_base, rise, leaf_size, leaf_width, winter_leaf, bloom_size;
    double leaf_stem_width, flower_stem_width;
    HuaSeedProfile seed;
} HuaProfile;

typedef struct {
    const char *color;
    double height, spread, bloom_size;
    int stems;
} HuaShape;

void qlcg_pa_build_ornamental(LpBuilder *b, const PdDef *def, int season, int form,
                               const HuaProfile *profile, const HuaShape *shape);
int qlcg_pa_hua_seed_count(const PdDef *def, int season);
void qlcg_pa_hua_seed(LpBuilder *b, const double at[3], const HuaSeedProfile *profile,
                      double scale, int visible);

#endif
