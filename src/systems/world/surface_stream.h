#ifndef QLCG_SURFACE_STREAM_H
#define QLCG_SURFACE_STREAM_H

#include "planet_macro.h"
#include "core/frame_workers.h"

#define QLCG_STREAM_SIDE (2 * (MAX_RENDER_DISTANCE + 1) + 1)
#define QLCG_STREAM_CAP (QLCG_STREAM_SIDE * QLCG_STREAM_SIDE)
#define QLCG_CHUNK_GRID (CHUNK_CELLS + 3)

typedef struct {
    RegionData *data;
    uint64_t serial;
    uint64_t appearance;
    int active_index;
    bool refresh_surface, refresh_appearance;
} RegionChunkSlot;

typedef struct RegionBuildJob RegionBuildJob;
struct QlcgSurfaceWorker;
typedef struct { int x, z, distance2; } RegionStreamOffset;
typedef struct {
    RegionBuildJob *job;
    struct QlcgSurfaceWorker *sampler;
    int x,z;
    bool serial;
} RegionStreamTask;

typedef struct RegionStream {
    QlcgFrameWorkers *workers;
    RegionStreamTask tasks[QLCG_FRAME_LANES];
    double overrun_ms;
    float view_yaw,view_cos;
    bool view_ready,priority_dirty;
    const MacroWorld *world;
    GameSettings settings;
    bool artificial_enabled;
    int season, radius, center_cx, center_cz;
    RegionStreamOffset offsets[QLCG_STREAM_CAP];
    int order[QLCG_STREAM_CAP];
    int offset_count, cursor;
    RegionChunkSlot slots[QLCG_STREAM_CAP];
    int active[QLCG_STREAM_CAP], count;
    uint64_t next_serial, revision;
    uint32_t delta_revision, surface_revision, territory_revision;
    uint32_t farm_revision;
    unsigned loaded_total, unloaded_total;
    double update_ms;
} RegionStream;

RegionBuildJob *qlcg_chunk_build_begin(const MacroWorld *w,int cx,int cz,int season,const GameSettings *settings);
/* 无几何与碰撞的基准散生乔木任务 */
RegionBuildJob *qlcg_chunk_plants_begin(const MacroWorld *w,int cx,int cz);
bool qlcg_chunk_build_step(RegionBuildJob *job);
bool qlcg_chunk_build_needs_surface(const RegionBuildJob *job);
void qlcg_chunk_build_surface(RegionBuildJob *job,const MacroWorld *snapshot,double deadline);
bool qlcg_chunk_build_valid(const RegionBuildJob *j,const MacroWorld *w,int season,const GameSettings *s);
RegionData *qlcg_chunk_build_take(RegionBuildJob *job);
void qlcg_chunk_build_cancel(RegionBuildJob *job);
void qlcg_region_stream_view(RegionStream *st,float yaw,float half_fov);
void qlcg_region_stream_require(RegionData *reg,float x,float z);
RegionData *qlcg_chunk_sample(const MacroWorld *w, int cx, int cz,
                              int season, const GameSettings *s);
void qlcg_chunk_resample(RegionData *reg, const MacroWorld *w,
                          int season, const GameSettings *s);
uint64_t qlcg_chunk_territory_appearance(RegionData *reg, const MacroWorld *w);
int qlcg_stream_slot(int cx, int cz);
RegionData *qlcg_region_chunk_at(const RegionData *reg, float lx, float lz,
                                float *chunk_lx, float *chunk_lz);
void qlcg_region_stream_update(RegionData *reg, float lx, float lz,
                               int season, const GameSettings *s, double budget_ms);
void qlcg_region_stream_refresh(RegionData *reg, float wx0, float wz0,
                                float wx1, float wz1, int season, const GameSettings *s);
void qlcg_region_stream_sync_changed(RegionData *reg,int season,const GameSettings *s);
void qlcg_region_stream_free(RegionStream *stream);

#endif
