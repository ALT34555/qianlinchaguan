#ifndef QLCG_YAO_MODEL_H
#define QLCG_YAO_MODEL_H

#include "../../plant_arch_internal.h"

typedef struct {
    double height, spread, phase, leaf_scale;
    LpRgb leaf, shade, stem;
    int simple, bloom, fruit, dry, seedling;
} YaoPlant;

YaoPlant qlcg_pa_yao_plant(const PdDef *def, int season, int form, int bloom_season);
void qlcg_pa_yao_stalk(LpBuilder *b, const double from[3], const double to[3],
                       double radius, double taper, LpRgb color);
void qlcg_pa_yao_twig(LpBuilder *b, const double from[3], const double to[3],
                      double radius, LpRgb color);
void qlcg_pa_yao_berry(LpBuilder *b, const double at[3], double radius,
                       double aspect, LpRgb color);
void qlcg_pa_yao_flower(LpBuilder *b, const double at[3], double a, int count,
                        double length, double width, double pitch, LpRgb color);

#endif
