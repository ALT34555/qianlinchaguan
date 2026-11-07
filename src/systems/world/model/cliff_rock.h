#ifndef QLCG_CLIFF_ROCK_H
#define QLCG_CLIFF_ROCK_H

#include "rock_builder.h"

struct MacroWorld;

typedef enum {
    CLIFF_BLUE, CLIFF_COOL_GRAY, CLIFF_WARM_GRAY, CLIFF_COOL_WHITE,
    CLIFF_WARM_WHITE, CLIFF_BLACK, CLIFF_COLOR_COUNT
} CliffRockColor;

int qlcg_cliff_count(void);
const char *qlcg_cliff_id(int index);
CliffRockColor qlcg_cliff_color_at(const struct MacroWorld *w, double wx, double wz);
const RockGeometry *qlcg_cliff_geometry(int index, CliffRockColor color);
const RockGeometry *qlcg_cliff_geometry_at(int index, const struct MacroWorld *w, double wx, double wz);
void qlcg_cliff_shutdown(void);

int qlcg_stone_forest_count(void);
const char *qlcg_stone_forest_id(int index);
const RockGeometry *qlcg_stone_forest_geometry(int index, CliffRockColor color);
const RockGeometry *qlcg_stone_forest_geometry_at(int index, const struct MacroWorld *w,
                                                double wx, double wz);

#endif
