#ifndef QLCG_WORLD_FACTION_SAVE_H
#define QLCG_WORLD_FACTION_SAVE_H

#include "planet_macro.h"
#include <stdio.h>

bool qlcg_factions_write(FILE *file, const MacroWorld *world);
/* limit<=0 表示以文件尾为界 */
bool qlcg_factions_read_lim(FILE *file, MacroWorld *world, long limit);
bool qlcg_factions_tail_valid_lim(FILE *file, long limit);
/* 须在尾开头调用；返回结束偏移，-1 无效 */
long qlcg_factions_tail_end_lim(FILE *file, long limit);

static inline bool qlcg_factions_read(FILE *file, MacroWorld *world) {
    return qlcg_factions_read_lim(file, world, 0);
}
static inline bool qlcg_factions_tail_valid(FILE *file) {
    return qlcg_factions_tail_valid_lim(file, 0);
}
static inline long qlcg_factions_tail_end(FILE *file) {
    return qlcg_factions_tail_end_lim(file, 0);
}

#endif
