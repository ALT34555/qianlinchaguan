/* 块冰缓存、单元接触、稀疏编辑与再冻 */
#ifndef QLCG_ICE_RUNTIME_H
#define QLCG_ICE_RUNTIME_H

#include "planet_macro.h"
#include "surface_stream.h"

#define ICE_CELL_M         2.0f
/* 浅于此不破：踩的是底冻冰，碎了也只是踩湖床 */
#define ICE_BREAK_DEPTH    0.30f
#define ICE_GRID_CELLS     CHUNK_CELLS                        /* 16 单元/边 */
#define ICE_CELLS_PER_CHUNK (CHUNK_CELLS * CHUNK_CELLS)       /* 256 */
#define ICE_VERTS          (QLCG_CHUNK_GRID * QLCG_CHUNK_GRID)
#define ICE_EDIT_CAP_MAX   65536u
#define ICE_EDIT_SOFT      16384u
#define ICE_TRANS_CAP      64

/* 世界级稀疏编辑：只有被破坏或带疲劳的单元 */
typedef struct {
    int64_t  gx, gz;
    double   broken_at;
    float    damage;
    uint32_t flags;      /* 见 ICE_EDIT_* */
    uint32_t stamp;
} IceEdit;

enum { ICE_EDIT_HOLE = 1u, ICE_EDIT_DAMAGE = 2u };

/* 温度事件缓变：稀疏校正，不补算过去天气 */
typedef struct {
    bool     used;
    int32_t  cx, cz;
    float    wx, wz;
    double   start_day;
    float    delta_h, delta_q, tau;
} IceThermalTransition;

typedef struct IceChunkData {
    int16_t  annual_c[ICE_VERTS];
    uint16_t depth_cm[ICE_VERTS];
    uint8_t  flow_u[ICE_VERTS];   /* 0..255 → 0..1 流动抑制 */
    uint8_t  shore[ICE_VERTS];    /* 0..255 → 0..1 近岸权重 */
    uint8_t  noise[ICE_VERTS];    /* 0..255 → 0..1 空间噪声 */
    uint8_t  kind[ICE_VERTS];
    uint8_t  wet[ICE_VERTS];
    float    h[ICE_VERTS];
    float    q[ICE_VERTS];
    uint32_t open[ICE_CELLS_PER_CHUNK / 32];
    float    damage[ICE_CELLS_PER_CHUNK];
    uint32_t revision;
    uint32_t merged_edits;   /* 已合并的世界编辑版本 */
    double   refreshed_day;
    int      wet_count;
} IceChunkData;

typedef struct IceWorldState {
    IceEdit *edits;
    uint32_t cap, count, soft_limit, overflow;
    IceThermalTransition trans[ICE_TRANS_CAP];
    uint32_t epoch;
    double   last_climate_day;
    uint32_t edit_revision;
    /* 玩家身体占洞：该格不允许封口夹人 */
    float    occ_x, occ_z, occ_half;
    bool     occ_valid;
} IceWorldState;

/* 单元接触结果：单足迹通常 ≤4 格 */
typedef struct {
    int   cx, cz, ix, iz;
    float wx, wz;
    float area, strength, top, thickness, quality;
    float depth;        /* 接触面内最小水深 m */
} IceContact;

IceWorldState *qlcg_ice_world_create(void);
void           qlcg_ice_world_free(IceWorldState *st);
IceChunkData  *qlcg_ice_chunk_create(void);
void           qlcg_ice_chunk_free(IceChunkData *c);

/* 时刻：正常游戏每帧设置；回拨/切历法递增 epoch */
void qlcg_ice_set_time(MacroWorld *w, const IceTime *t);
uint32_t qlcg_ice_epoch(const MacroWorld *w);

/* 采样期写入静态量，随后由 refresh 求 H/Q */
void qlcg_ice_cache_site(IceChunkData *c, int vertex, const IceSite *site);
/* 重采样后重建块缓存并合并世界稀疏编辑 */
void qlcg_ice_chunk_attach(const MacroWorld *w, RegionData *reg);
/* 地形/水体改动后让冰缓存立即过期 */
void qlcg_ice_chunk_invalidate(RegionData *reg);
/* 按当前时刻重算本块冰与覆盖；返回是否有变 */
bool qlcg_ice_chunk_refresh(MacroWorld *w, RegionData *reg);
bool qlcg_ice_chunk_stale(const MacroWorld *w, const RegionData *reg);

bool qlcg_ice_cell_open(const IceChunkData *c, int ix, int iz);
float qlcg_ice_cell_damage(const IceChunkData *c, int ix, int iz);

/* 破裂与疲劳：写世界稀疏编辑 + 更新已加载块掩码 */
void qlcg_ice_break_cells(const MacroWorld *w, RegionData *reg, const IceContact *cells, int count);
void qlcg_ice_add_damage(const MacroWorld *w, RegionData *reg, const IceContact *cells,
                         int count, float amount);
void qlcg_ice_set_occupant(const MacroWorld *w, float wx, float wz, float half);
/* 单元疲劳 0..1 */
float qlcg_ice_cell_damage_at(const MacroWorld *w, const RegionData *reg, float wx, float wz);
/* 每帧推进块冰刷新：3×3 优先，余按预算 */
void qlcg_ice_tick(RegionData *reg, float player_lx, float player_lz, int budget);
void qlcg_ice_refresh_all(RegionData *reg);
/* 未封口单元的再冻厚度：渲染补片与接触共用 */
float qlcg_ice_cell_regrow(const MacroWorld *w, const IceChunkData *c,
                           const RegionData *reg, int ix, int iz, float *out_quality);
/* 按年均温求该点自然基线厚度 */
float qlcg_ice_baseline_h(const MacroWorld *w, const RegionData *reg, float wx, float wz,
                          float annual);
/* 全局 2m 单元键（经度接缝规范化） */
void qlcg_ice_cell_key(const MacroWorld *w, float wx, float wz, int64_t *gx, int64_t *gz);

const IceEdit *qlcg_ice_edit_find(const MacroWorld *w, int64_t gx, int64_t gz);
int  qlcg_ice_edit_count(const MacroWorld *w);

/* 足迹接触集合（只读）与载荷施加 */
int  qlcg_region_ice_contacts(const RegionData *reg, float x, float z, float half, float feet,
                              IceContact *out, int cap);
/* 载荷：0 承受 / 1 疲劳 / 2 破裂 */
int  qlcg_region_ice_apply_load(const MacroWorld *w, RegionData *reg, float x, float z,
                                float half, float feet, float load, float dt, bool impact);

/* 温度事件缓变 */
void qlcg_ice_thermal_begin(const MacroWorld *w, float wx, float wz, double start_day,
                            float delta_h, float delta_q, float tau);
void qlcg_ice_thermal_sample(const MacroWorld *w, float wx, float wz, double now_day,
                             float *delta_h, float *delta_q);

#endif /* QLCG_ICE_RUNTIME_H */
