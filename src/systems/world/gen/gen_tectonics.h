/* 板块漂移碰撞与高度合成（tectonics） */
#ifndef QLCG_GEN_TECTONICS_H
#define QLCG_GEN_TECTONICS_H

#include "../planet_macro.h"

/* 生成板块骨架，写入 height/uplift 等场 */
void qlcg_gen_tectonics(MacroWorld *w);
void qlcg_gen_planar(MacroWorld *w);
void qlcg_spread_tectonic(const MacroWorld *w,const float *src,float width,float *out);

#endif
