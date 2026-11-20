#ifndef QLCG_WORLD_CIV_GROWTH_H
#define QLCG_WORLD_CIV_GROWTH_H
#include "planet_macro.h"
bool qlcg_civ_growth_step(MacroWorld *w, int previous_day, int current_day);
void qlcg_civ_growth_free(MacroWorld *w);
#endif
