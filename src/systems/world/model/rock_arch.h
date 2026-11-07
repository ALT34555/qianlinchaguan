/* 岩石原型解释器：对齐 archetypes.ts */
#ifndef QLCG_ROCK_ARCH_H
#define QLCG_ROCK_ARCH_H

#include "rock_builder.h"
#include "rock_def.h"

typedef void (*RockArchFn)(RockBuilder *b, const RockBuildDef *def);

int qlcg_rock_arch_build(const RockBuildDef *def, RockGeometry *out);
RockArchFn qlcg_rock_arch_fn(const char *name);

#endif
