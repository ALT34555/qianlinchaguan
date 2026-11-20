/* 星球生成内部契约 */
#ifndef QLCG_WORLD_INTERNAL_H
#define QLCG_WORLD_INTERNAL_H

#include "planet_macro.h"
#include <math.h>

extern const int        MACRO_SIZES[3];
extern const int        MACRO_SCALES[7];
extern const char      *FLOW_DIR_NAMES[8];

static inline int wrap_col(int x, int cols) {
    int r = x % cols;
    return r < 0 ? r + cols : r;
}

static inline float qlcg_macro_delta_x(const MacroWorld *w, float x, float origin) {
    float dx = x - origin;
    float period = w->circumference;
    if (period > 0 && !qlcg_world_flat(w) && fabsf(dx) > period * .5f)
        dx = remainderf(dx, period);
    return dx;
}

int qlcg_macro_rects(const MacroWorld *w, double x0, double z0, double x1, double z1,
                     MacroCellRect rects[2]);
typedef enum { MACRO_RIVERS_UNCHANGED, MACRO_RIVERS_WATER, MACRO_RIVERS_FLOW } MacroRiverUpdate;
void qlcg_world_rebuild_atlas_region(MacroWorld *w, const MacroCellRect *rects, int count,
                                    MacroRiverUpdate rivers);
void qlcg_world_atlas_rivers_free(MacroWorld *w);
void qlcg_eng_atlas_rivers(MacroWorld *w);
void qlcg_world_macro_surface_dirty(MacroWorld *w, uint32_t before_delta, uint32_t before_surface,
                                     double x0, double z0, double x1, double z1);

void build_sphere_grid(MacroWorld *w);
void build_tectonics(MacroWorld *w);
void calibrate_sea_level(MacroWorld *w);
void solve_climate_and_hydrology(MacroWorld *w);
void build_initial_civilizations(MacroWorld *w);
void rotate_vec3(const float v[3], const float k[3], float a, float out[3]);

#endif
