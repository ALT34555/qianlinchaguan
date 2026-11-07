/* 水文主体，对齐 hydrology.mjs */
#ifndef QLCG_GEN_HYDROLOGY_H
#define QLCG_GEN_HYDROLOGY_H

#include "../planet_macro.h"

/* 填洼→汇流，写 height/water/flow */
void qlcg_build_hydrology(MacroWorld *w);

#endif
