#ifndef QLCG_DOLL_LIFE_SAVE_H
#define QLCG_DOLL_LIFE_SAVE_H

#include "doll_life.h"
#include <stdio.h>

bool qlcg_doll_life_write(FILE *file,const MacroWorld *world);
bool qlcg_doll_life_read(FILE *file,long limit,MacroWorld *world,const char owners[MAX_FACTIONS][17]);

#endif
