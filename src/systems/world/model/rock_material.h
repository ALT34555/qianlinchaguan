/* 岩石材料：编号索引与几何缓存 */
#ifndef QLCG_ROCK_MATERIAL_H
#define QLCG_ROCK_MATERIAL_H

#include "rock_builder.h"
#include "../decoration_collision.h"

int  qlcg_rock_material_init(void);
void qlcg_rock_material_shutdown(void);
int  qlcg_rock_material_count(void);

/* 取该岩石/形态/季节/区块几何；调用方不得释放 */
const RockGeometry *qlcg_rock_geometry(int index, int form, int season, int chunk);
float qlcg_rock_sprite_span(const RockGeometry *g);

const DecorationShape *qlcg_rock_collision_shape(int index, int form, int season, int chunk);

#endif
