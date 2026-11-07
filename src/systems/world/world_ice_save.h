/* ICE1 尾段：可选、小端、含编辑/过渡记录 */
#ifndef QLCG_WORLD_ICE_SAVE_H
#define QLCG_WORLD_ICE_SAVE_H

#include "planet_macro.h"
#include <stdio.h>

/* 尾段版本独立于 QLCG_SAVE_VERSION */
#define ICE_SAVE_VERSION        1u
#define ICE_SAVE_MODEL_VERSION  1u

/* 固定字节长度：头 40，编辑记录 40，过渡记录 40 */
#define ICE_SAVE_HEADER_BYTES   40u
#define ICE_SAVE_EDIT_BYTES     40u
#define ICE_SAVE_TRANS_BYTES    40u

/* 编辑上限=cap/2，保证装载率 ≤ 50% */
#define ICE_SAVE_EDIT_LIMIT     32768u

/* 读写：须在阵营尾后；无 ICE1 且到界视为旧档 */
bool qlcg_ice_write(FILE *file, const MacroWorld *world);
/* limit<=0 表示以文件尾为界 */
bool qlcg_ice_read_optional_lim(FILE *file, MacroWorld *world, long limit);
/* 只校验不装入：旧档真，损坏/截断假 */
bool qlcg_ice_tail_valid_lim(FILE *file, long limit);
/* ICE1 起点返回尾段结束 */
long qlcg_ice_tail_end_lim(FILE *file, long limit);

static inline bool qlcg_ice_read_optional(FILE *file, MacroWorld *world) {
    return qlcg_ice_read_optional_lim(file, world, 0);
}
static inline bool qlcg_ice_tail_valid(FILE *file) {
    return qlcg_ice_tail_valid_lim(file, 0);
}
static inline long qlcg_ice_tail_end(FILE *file) {
    return qlcg_ice_tail_end_lim(file, 0);
}

#endif /* QLCG_WORLD_ICE_SAVE_H */
