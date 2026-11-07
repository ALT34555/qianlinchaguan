#ifndef QLCG_SURFACE_STREAM_H
#define QLCG_SURFACE_STREAM_H

#include "planet_macro.h"

#define QLCG_STREAM_SIDE (2 * (MAX_RENDER_DISTANCE + 1) + 1)
#define QLCG_STREAM_CAP (QLCG_STREAM_SIDE * QLCG_STREAM_SIDE)
#define QLCG_CHUNK_GRID (CHUNK_CELLS + 3)

typedef struct {
    RegionData *data;
    uint64_t serial;
    int active_index;
} RegionChunkSlot;

typedef struct RegionStream {
    const MacroWorld *world;
    GameSettings settings;
    int season, radius, center_cx, center_cz;
    int offsets[QLCG_STREAM_CAP][2];
    int offset_count, cursor;
    RegionChunkSlot slots[QLCG_STREAM_CAP];
    int active[QLCG_STREAM_CAP], count;
    uint64_t next_serial, revision;
    uint32_t delta_revision, surface_revision, territory_revision;
    unsigned loaded_total, unloaded_total;
    double update_ms;
} RegionStream;

RegionData *qlcg_chunk_sample(const MacroWorld *w, int cx, int cz,
                              int season, const GameSettings *s);
void qlcg_chunk_resample(RegionData *reg, const MacroWorld *w,
                          int season, const GameSettings *s);
int qlcg_stream_slot(int cx, int cz);
RegionData *qlcg_region_chunk_at(const RegionData *reg, float lx, float lz,
                                float *chunk_lx, float *chunk_lz);
void qlcg_region_stream_update(RegionData *reg, float lx, float lz,
                               int season, const GameSettings *s, double budget_ms);
void qlcg_region_stream_refresh(RegionData *reg, float wx0, float wz0,
                                float wx1, float wz1, int season, const GameSettings *s);
void qlcg_region_stream_free(RegionStream *stream);

#endif
