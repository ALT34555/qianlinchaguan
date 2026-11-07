#ifndef QLCG_RIVER_ARCHIVE_H
#define QLCG_RIVER_ARCHIVE_H
#include "planet_macro.h"
#include <stdio.h>

enum { RIVER_RECORD_MAIN=1, RIVER_RECORD_SOURCE=2, RIVER_RECORD_OXBOW=3 };
enum { QLCG_TERRAIN_VERSION=4 };
typedef struct {
    unsigned char *data;
    size_t size,capacity,at;
    int reading,failed;
} RiverBuffer;
typedef struct { int kind,cell; RiverBuffer buffer; } RiverArchiveUpdate;
struct RiverArchive *qlcg_river_archive_create(uint32_t terrain_version);
uint32_t qlcg_river_archive_terrain(const MacroWorld *w);
void qlcg_river_io(RiverBuffer *b,void *data,size_t size);
void qlcg_river_buffer_free(RiverBuffer *b);
int qlcg_river_archive_get(const MacroWorld *w,int kind,int cell,RiverBuffer *b);
int qlcg_river_archive_put(MacroWorld *w,int kind,int cell,const RiverBuffer *b);
int qlcg_river_archive_commit(MacroWorld *w,const RiverArchiveUpdate *updates,int count);
uint64_t qlcg_river_archive_next_id(MacroWorld *w);
void qlcg_river_archive_free(MacroWorld *w);
bool qlcg_river_archive_write(FILE *file,const MacroWorld *w);
bool qlcg_river_archive_read(FILE *file,MacroWorld *w,long limit);
bool qlcg_saves_read_rivers(MacroWorld *w,const SaveSlotInfo *slot,long offset,long size);
MacroWorld *qlcg_world_generate_with_rivers(const WorldCreatorOptions *opt,struct RiverArchive *archive);
#endif
