/* 地形配方（对齐 backend.mjs） */
#ifndef QLCG_GEN_RECIPES_H
#define QLCG_GEN_RECIPES_H

#include "../planet_macro.h"

/* 在山脊之后、海平面校准之前施加地形配方 */
void qlcg_apply_recipes(MacroWorld *w);

#endif
