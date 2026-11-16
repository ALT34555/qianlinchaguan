/* RAIN1 尾段：可选、小端、量化降水场 */
#ifndef QLCG_WORLD_PRECIP_SAVE_H
#define QLCG_WORLD_PRECIP_SAVE_H

#include "planet_macro.h"
#include <stdio.h>

#define PRECIP_SAVE_VERSION      1u
#define PRECIP_SAVE_VERSION2     2u
#define PRECIP_SAVE_HEADER_BYTES 40u
#define PRECIP_SAVE_SCALE        64.0f

bool qlcg_precip_write(FILE *file, const MacroWorld *world);
bool qlcg_precip_encode_compact(const MacroWorld *world, uint8_t **out_buf, size_t *out_size);
/* limit<=0 表示以文件尾为界 */
bool qlcg_precip_read_optional_lim(FILE *file, MacroWorld *world, long limit);
bool qlcg_precip_tail_valid_lim(FILE *file, long limit);
bool qlcg_precip_read_strict(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, long limit);
bool qlcg_precip_read_strict_mem(const uint8_t *data, size_t size, MacroWorld *world, const WorldCreatorOptions *opt);

static inline bool qlcg_precip_read_optional(FILE *file, MacroWorld *world) {
    return qlcg_precip_read_optional_lim(file, world, 0);
}
static inline bool qlcg_precip_tail_valid(FILE *file) {
    return qlcg_precip_tail_valid_lim(file, 0);
}

#endif
