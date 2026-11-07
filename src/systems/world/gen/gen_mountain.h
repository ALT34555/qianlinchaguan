/* 山脉，对齐 mountain-ranges.mjs */
#ifndef QLCG_GEN_MOUNTAIN_H
#define QLCG_GEN_MOUNTAIN_H

#include "../planet_macro.h"

/* 在山脊抬升之前调用：修改 height，返回抬升峰值 */
float qlcg_build_mountain_ridges(MacroWorld *w);

#endif
