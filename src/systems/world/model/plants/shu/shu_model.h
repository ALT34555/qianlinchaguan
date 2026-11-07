#ifndef QLCG_SHU_MODEL_H
#define QLCG_SHU_MODEL_H

#include "../../plant_arch_internal.h"

typedef struct {
    double density, size, droop;
} ShuSeason;

enum { SHU_LEAF_SIMPLE, SHU_LEAF_COMPOUND, SHU_LEAF_FAN, SHU_LEAF_LOBED };

typedef struct {
    int limbs, leaf_style, bloom_season, cold, heat;
    double crown_base, reach, rise, hang, lean;
    double cluster, leaf_width, winter_leaf;
    double summer_fullness;
} ShuProfile;

typedef struct {
    double height, crown, trunk;
    int branches;
} ShuShape;

ShuSeason qlcg_pa_shu_season(const PdDef *def, int season, double winter_leaf, int cold, int heat);
void qlcg_pa_shu_broadleaf(LpBuilder *b, const PdDef *def, int season, int form,
                           const ShuProfile *profile, const ShuShape *shape, PaFruitBuilder draw_fruit);

void qlcg_pa_shu_fruit_cluster(LpBuilder *b, const PdDef *def, const double center[3],
                               double reach, int count, LpRng *rng, double size,
                               PaFruitBuilder draw);

#endif
