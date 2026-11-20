#ifndef QLCG_TENG_MODEL_H
#define QLCG_TENG_MODEL_H

#include "../../plant_arch_internal.h"

typedef struct {
    LpRgb stem, leaf, shade, bloom, fruit;
    double span;
    int flowering, fruiting, bare, hanging;
} TengShoot;

typedef void (*TengNode)(LpBuilder *, const TengShoot *, const double[3],
                         double bearing, int node);

typedef struct {
    double reach, arch, tip_height, drop;
    double bearing_step, bend, hanging_spread, hanging_lift;
} TengForm;

typedef struct {
    int canes, nodes, bloom_season, twined_canes;
    double thickness;
    TengNode draw_node;
    TengForm forms[3];
} TengHabit;

void qlcg_pa_vine(LpBuilder *b, const PdDef *def, int season, const TengHabit *habit);
void qlcg_pa_vine_twig(LpBuilder *b, const double from[3], const double to[3],
                       double radius, double taper, LpRgb color);
void qlcg_pa_vine_ribbon(LpBuilder *b, const double from[3], const double to[3],
                         double bearing, double radius, LpRgb color);
void qlcg_pa_vine_fruit(LpBuilder *b, const double at[3], const double radii[3], LpRgb color);

#endif
