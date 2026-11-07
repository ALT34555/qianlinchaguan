/* 植物原型内部共用声明 */
#ifndef QLCG_PLANT_ARCH_INTERNAL_H
#define QLCG_PLANT_ARCH_INTERNAL_H

#include "plant_arch.h"

#define PI  3.14159265358979323846
#define TAU 6.283185307179586476925286766559
#define GOLDEN 2.39996322972865332223

static inline unsigned qlcg_pa_shape_variant(const PdDef *def) {
    double shape = pd_num(&def->params, "shapeVariant", 0);
    return shape == 1.0 ? 1u : shape == 2.0 ? 2u : 0u;
}

double qlcg_pa_pnum(const PdDef *def, const char *key, double dflt);
int    qlcg_pa_js_round(double v);
double qlcg_pa_clampd(double v, double lo, double hi);
LpRgb  qlcg_pa_tone(const PdDef *def, const char *key, double scale);
LpRgb  qlcg_pa_deepen(LpRgb c, double depth);

LpRgb qlcg_pa_snow_color(const PdDef *def);
int   qlcg_pa_has_snow_target(const PdDef *def, const char *target);
int   qlcg_pa_wants_snow_on(const PdDef *def, const char *target);
void  qlcg_pa_snow_cap(LpBuilder *b, const PdDef *def, const double center[3],
                       double r, double y_scale);

int  qlcg_pa_bloom_count_for(const PdDef *def, int requested, int fallback);
int  qlcg_pa_fruit_count_for(const PdDef *def, int requested, int fallback);
void qlcg_pa_flower_at(LpBuilder *b, const PdDef *def, const double center[3],
                       double radius, LpRng *rand);
void qlcg_pa_bloom_cluster(LpBuilder *b, const PdDef *def, const double center[3],
                           double r, int count, LpRng *rand, double size, double y_span);
void qlcg_pa_leaf_spray(LpBuilder *b, const PdDef *def, const double center[3],
                        double r, int count, LpRng *rand, double width,
                        double pitch_in, double spread_y, int dry, double len_scale);
void qlcg_pa_bent_leaf(LpBuilder *b, const double start[3], double bearing,
                       double len, double wid, double rise, double drop,
                       LpRgb col, LpRng *rand, int mat);
typedef void (*PaFruitBuilder)(LpBuilder *, const PdDef *, const double[3], double, LpRng *);
void qlcg_pa_fruit_layout(LpBuilder *b, const PdDef *def, const double center[3],
                          double r, int count, LpRng *rand, double size,
                          int sockets, PaFruitBuilder draw);
void qlcg_pa_fruit_cluster(LpBuilder *b, const PdDef *def, const double center[3],
                           double r, int count, LpRng *rand, double size);
void qlcg_pa_limb_to(LpBuilder *b, const double start[3], const double dir[3],
                     double length, double radius, LpRgb col, double out_end[3]);
void qlcg_pa_pollen_grains(LpBuilder *b, const PdDef *def, int count, double base_radius,
                           double bottom, double top, LpRng *rand);
int  qlcg_pa_tag_has(const PdDef *def, const char *tag);

#endif
