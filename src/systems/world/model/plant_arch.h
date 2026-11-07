#ifndef QLCG_PLANT_ARCH_H
#define QLCG_PLANT_ARCH_H

#include "lp_builder.h"
#include "plant_def.h"

int qlcg_plant_species_build(const PdDef *def, int season, int form, LpBuilder *b);

#endif
