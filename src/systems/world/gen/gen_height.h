/* 高度包络，对齐 terrainHeightCap */
#ifndef QLCG_GEN_HEIGHT_H
#define QLCG_GEN_HEIGHT_H

#include "../planet_macro.h"

/* 地面高度上限（米） */
float qlcg_terrain_height_cap(const WorldCreatorOptions *opt);

/* 高度放大系数 = cap / 3800 */
float qlcg_height_scale_for(const WorldCreatorOptions *opt);

#endif
