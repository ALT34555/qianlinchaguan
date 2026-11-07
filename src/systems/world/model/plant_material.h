/* 植物材料：物种索引与几何缓存 */
#ifndef QLCG_PLANT_MATERIAL_H
#define QLCG_PLANT_MATERIAL_H

#include "plant_forms.h"
#include "../decoration_collision.h"

/* 加载 plants，返回物种数；幂等 */
int  qlcg_plant_material_init(void);
void qlcg_plant_material_shutdown(void);

int  qlcg_plant_species_count(void);
/* 未知名返回 -1 */
int  qlcg_plant_species_index(const char *id);
const char *qlcg_plant_species_id(int index);
/* 六类编码，未定返回 -1 */
int  qlcg_plant_species_category(int index);

/* 取该物种该形态该季节的几何；调用方不得释放 */
const LpGeometry *qlcg_plant_geometry(int species, int form, int season);

const DecorationShape *qlcg_plant_collision_shape(int index, int form, int season);

#endif
