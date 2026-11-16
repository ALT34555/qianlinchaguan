#ifndef QLCG_SAVE_PORTABLE_H
#define QLCG_SAVE_PORTABLE_H
#include "save_fields.h"
long qlcg_portable_header_size(void);
long qlcg_portable_header_size_ver(uint32_t version);
bool qlcg_portable_header_write(FILE *file, const SaveSlotInfo *slot);
bool qlcg_portable_header_read(FILE *file, SaveSlotInfo *slot, long limit);
bool qlcg_portable_body_write(FILE *file, const MacroWorld *world);
bool qlcg_portable_body_read(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, int deltas, long limit);
bool qlcg_portable_body_read_ver(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, int deltas, long limit, uint32_t save_version);
bool qlcg_portable_rivers(FILE *file, MacroWorld *world, long limit);
#endif
