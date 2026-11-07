/* 星球生成内部契约 */
#ifndef QLCG_WORLD_INTERNAL_H
#define QLCG_WORLD_INTERNAL_H

#include "planet_macro.h"

extern const int        MACRO_SIZES[3];
extern const int        MACRO_SCALES[7];
extern const char      *FLOW_DIR_NAMES[8];

static inline int wrap_col(int x, int cols) {
    int r = x % cols;
    return r < 0 ? r + cols : r;
}

void build_sphere_grid(MacroWorld *w);
void build_tectonics(MacroWorld *w);
void calibrate_sea_level(MacroWorld *w);
void solve_climate_and_hydrology(MacroWorld *w);
void build_initial_civilizations(MacroWorld *w);
void rotate_vec3(const float v[3], const float k[3], float a, float out[3]);

#endif
