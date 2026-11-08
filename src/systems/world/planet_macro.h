#ifndef QLCG_PLANET_MACRO_H
#define QLCG_PLANET_MACRO_H

#include "../../core/config.h"
#include "../../core/i18n.h"
#include "decoration_collision.h"
#include "../../core/game_settings.h"
#include "../calendar/calendar.h"
#include "chunk_types.h"
#include "ice_model.h"

typedef enum { WORLD_MAP_PLANET, WORLD_MAP_INFINITE } WorldMapMode;

struct IceWorldState;
struct IceChunkData;

/* 世界创建器完整参数（对齐 WorldCr */
typedef struct {
    char               world_name[240];
    char               ren_ming[256];
    int                height_cm;
    PlayMode           play_mode;

    uint32_t           seed;
    char               seed_str[64];
    WorldPresetId      preset;
    MacroTerrainRecipe terrain_recipe;
    MacroClimateRule   climate_rule;   /* 仅旧档迁移，生成不读 */
    int                macro_size_idx;
    int                macro_scale_idx;
    int                columns;
    int                chunks_per_cell;
    int                equator_chunks;
    float              pole_temp;
    float              equator_temp;
    int                tectonic_activity;
    float              land_ratio;
    float              precipitation;

    bool               astronomy_enabled;
    uint32_t           astronomy_version;

    /* 天文折叠组 */
    float              creation_time_speed;
    CalendarMode       calendar_mode;
    int                start_year;
    int                start_month;
    int                start_day;

    /* 人间折叠组（人造区块与阵营） */
    bool               artificial_enabled;
    bool               spawn_as_flag;
    char               faction_name[40];
    uint32_t           faction_main_color;
    uint32_t           faction_trim_color;
    int                initial_civ_limit;
    float              natural_expansion;
    float              natural_evolution;
    bool               flag_independence;
    int                monsoon_direction;   /* 季风方向 0..359，对齐 base */
    char               culture[32];
    float              axial_tilt_deg;      /* 黄赤交角 0..60 */
    bool               prograde;            /* true 顺行，false 逆行 */
    char               culture_choices[QLCG_MAX_LOCALES][32];
    WorldMapMode       map_mode;
    uint32_t           world_seed;
    int32_t            plane_x, plane_z;
} WorldCreatorOptions;

/* 构造板块结构 */
typedef struct {
    int   id;
    bool  is_minor;
    bool  oceanic;
    int   kind;            /* 0 主板块 1 交界小板块 */
    int   neighbors[2];    /* 小板块所依附的两块主板块 */
    float start[3];
    float center[3];
    float axis[3];
    float speed;
    float bias;
    float radius;
    float cap_cos;
} TectonicPlate;

/* 宏观河段矢量 */
typedef struct {
    float ax, az;
    float mx, mz;
    float bx, bz;
    float width_m;
    float flow;
    uint8_t min_level;     /* 4 主河 ~ 8 支流 */
} RiverSegment;

/* 文明阵营与领地实体（对齐 Artific */
typedef struct {
    bool     active;
    int      id;
    bool     is_player;
    char     uid[17];      /* 稳定阵营 ID，独立于数组索引 */
    char     name[40];
    uint32_t main_color;
    uint32_t trim_color;
    int      flag_cell;
    int      cx, cz;
    float    wx, wz;
    int      level;
    int      claim_radius;
    int      born_year;
    int      origin_type_id;
    char     culture[32];
    char     auto_name[96];
} TerritoryFaction;

/* 活跃自然灾害事件实体 */
typedef struct {
    bool             active;
    NaturalEventType type;
    float            wx, wz;
    float            vx, vz;
    float            radius;
    float            intensity;
    float            age;
    float            duration;
    char             title[64];
} ActiveNaturalEvent;

/* 稀疏增量记录（MacroDelta /  */
typedef struct {
    EngToolType type;
    float       wx, wz;
    float       ex, ez;
    float       radius;
    float       delta_h;
    float       width_m;
} SparseDeltaRecord;

/* 图层 ID 与列表位置独立 */
typedef struct {
    AtlasLayerId id;
    bool         on;
    float        opacity;
} AtlasLayerSetting;

/* 列表在前的图层盖在上面 */
typedef struct {
    AtlasLayerSetting layers[LAYER_ID_COUNT];
    int contour_density;
} AtlasDisplayState;

/* 湖泊记录（对齐 hydrology 的 lakes） */
typedef struct {
    int   id;
    float level;
    double area_km2;
    float max_depth;
    double inflow;
    double outflow;
    double evaporation;
    double rainfall;
    double runoff_replaced;
    double estimated_area_km2;
    int   cells;
    int   outlet;
    int   exit_cell;
    int   closed;     /* 1 咸水闭合湖 */
    int   plain;
} MacroLake;

typedef struct {
    int cell;
    double area_km2, inflow;
    int lake;
} MacroOasis;

/* 宏观取岛记录（对齐 macroIslands） */
typedef struct {
    int   cell;
    float base_height;   /* 抬升前高度，倍率前 */
    float peak_height;   /* 抬升后高度，倍率前 */
    float probability;
} MacroIsland;

typedef struct {
    double input, output, land_area;
    double lake_loss, lake_rain, lake_runoff, dry_loss, river_loss;
    int dry_basins, salt_basins, shallow_candidates, plain_lakes;
    int valid;
} HydrologyBalance;

/* 宏观星球与人工世界全量状态 */
typedef struct MacroWorld {
    WorldCreatorOptions opt;
    struct InfiniteWorld *planes;
    int                 columns;
    int                 rows;
    int                 cell_count;
    int                 equator_chunks;
    float               circumference;
    float               radius;
    float               height_scale;

    TectonicPlate       plates[MAX_PLATES];
    int                 plate_count;

    uint16_t           *plate_id;
    float              *ridge_push_e;   /* 山根推力的东向分量 */
    float              *ridge_push_n;   /* 山根推力的北向分量 */
    float              *hydro_friction; /* 河谷摩擦系数 */
    uint8_t            *biome;          /* 生物群系编号 */
    float              *basin;          /* 大陆壳盆地权重 */
    MacroLake          *lakes;
    int                 lake_count;
    int32_t            *lake_id;
    struct LakeTemplateCache *lake_templates;
    /* 单格海岛：取岛记录与惰性岛岸目录 */
    MacroIsland        *macro_islands;
    int                 macro_island_count;
    int                 macro_island_cap;
    struct IslandCatalog *island_catalog;
    bool                islands_ready;
    MacroOasis          oases[8];
    int                 oasis_count;
    HydrologyBalance    hydrology;
    int                 audit_ok;        /* 一致性校验结果 */
    uint8_t            *boundary;
    float              *points;
    float              *areas;
    int32_t            *neighbors;
    float              *lengths;

    float              *uplift;
    float              *rift;
    float              *shear;
    float              *height;
    float              *spill;
    float              *level;
    double             *flow;
    double             *catchment;
    float              *river_width;
    float              *main_river_width;
    float              *river_level;
    struct RiverSurfaceCache *river_surface;
    struct RiverArchive *river_archive;
    bool river_archive_preloaded;
    struct SourceRiverCache *source_rivers;
    struct OxbowCatalog *oxbow_catalog;
    bool decor_ready;
    struct TerrainDetailCache *terrain_detail;
    int32_t            *receiver;
    uint32_t           *rank;

    float              *temperature;
    float              *rainfall;
    float              *evaporation;
    float              *moisture;
    float              *season_amp;   /* 季节温度振幅 °C */
    float              *monsoon_amp;  /* 季风强度 0..1，运行期 */
    float              *desert_factor;
    float              *forest_bonus;

    uint8_t            *water;
    uint8_t            *saline;
    uint8_t            *waterfall_flag;
    uint8_t            *wetland_flag;
    uint8_t            *landform;
    int16_t            *chunk_type;
    uint8_t            *base_rgb;
    uint8_t            *sat_rgb;
    float              *shade;

    /* 人工世界：领地声明与探索迷雾 */
    int16_t            *claim_faction;
    uint8_t            *claim_type;
    uint64_t           *explored_chunks;
    size_t              explored_count, explored_capacity;
    TerritoryFaction    factions[MAX_FACTIONS];
    int                 faction_count;
    int                 last_civ_day;
    uint32_t            territory_revision;

    RiverSegment       *rivers;
    int                 river_count;
    int                 river_cap;

    int                 spawn_cell;
    int                 spawn_cx, spawn_cz;
    float               spawn_wx, spawn_wz;

    ActiveNaturalEvent  events[MAX_ACTIVE_EVENTS];
    bool                auto_events;
    float               sim_time;
    float               event_cooldown;
    char                last_event_msg[128];

    SparseDeltaRecord   deltas[MAX_MACRO_DELTAS];
    int                 delta_count;
    uint32_t            delta_version;
    uint32_t            surface_revision;

    const CalendarSnapshot *plant_calendar;
    bool                plant_season_forced;

    /* 冰：全世界统一气候时刻 + 稀疏交互编辑表 */
    struct IceWorldState *ice_state;
    struct AstraEventState *astra_events;
    uint32_t astronomy_broadcast;
    IceTime             ice_time;
    bool                ice_time_valid;
    int                 ice_forced_season;   /* -1 正常，0..3 调试强制 */

    /* 每日降水场 */
    struct PrecipField *precip;

    double              gen_ms;
    double              prep_ms;
    int                 generated_land_cells;
} MacroWorld;

static inline bool qlcg_world_flat(const MacroWorld *w) {
    return w->opt.map_mode == WORLD_MAP_INFINITE;
}
static inline float qlcg_world_depth(const MacroWorld *w) {
    return w->circumference * (qlcg_world_flat(w) ? 1.0f : 0.5f);
}
static inline double qlcg_world_z_offset(const MacroWorld *w) {
    return qlcg_world_flat(w) ? 0.5 : 0.25;
}
static inline bool qlcg_world_border(const MacroWorld *w,int cell) {
    int x=cell%w->columns,y=cell/w->columns;
    return qlcg_world_flat(w) && (x==0 || y==0 || x==w->columns-1 || y==w->rows-1);
}


/* 3D 植物、岩石与领地旗帜实例 */
typedef struct {
    float    x, y, z;
    float    scale_x, scale_y, scale_z;
    float    rot_y;
    float    r, g, b;
    uint8_t  category;
    uint8_t  archetype;
    uint8_t  model_state;
    uint8_t  species;      /* 植物物种表索引，0xFF 未定 */
    uint32_t trim_color;
    int16_t  faction_id;
    uint8_t  visual_season;
    uint8_t  shape_variant;
    uint8_t  wind_level;   /* 天气风速等级，运行时 */
    char     label[40];
    DecorationShape collider;
} PropInstance;

/* 3D 山川撤销快照 */
typedef struct {
    bool   valid;
    float *heights;
    float *water_levels;
    float *desert;
    float *forest;
    int    delta_count_snap;
} TerrainSnapshot;

/* 水体种类（对齐 base WaterKind） */
typedef enum {
    WATER_KIND_DRY = 0,
    WATER_KIND_SEA,
    WATER_KIND_LAKE,
    WATER_KIND_RIVER,
    WATER_KIND_FALL,
    WATER_KIND_STATIC
} SurfaceWaterKind;

typedef struct {
    uint8_t salt, spring;
} SourceWaterEffects;

typedef struct {
    float x, z;
    uint32_t seed;
} SpringEmitter;

/* 点级自然水状态，对齐 WaterSample */
typedef struct {
    float            level;        /* 水面高度；-1e9 表示无水 */
    float            bed;          /* 水底或地面高度 */
    float            depth;        /* level - bed */
    float            shore_depth;
    uint8_t          coast_material[3];
    float            vx, vz;       /* 冻态缩放后的流向分量 m/s */
    float            speed;
    float            raw_vx, raw_vz;   /* 未按冻态缩放的流动 */
    float            raw_speed;
    float            direction_x, direction_z;
    float            discharge;
    float            width_m;      /* 河宽（全宽） */
    bool             decorative;
    float            temperature;  /* 年均温 */
    float            ice_top, ice_bottom, ice_coverage, ice_thickness;
    float            ice_quality, ice_strength;   /* 无交互编辑的自然基线 */
    uint8_t          ice_stage;
    uint8_t          kind;
    uint8_t          form;
    uint8_t          freeze;
    uint8_t          river_sediment;
    uint8_t          fresh;
    SourceWaterEffects source_effects;
} SurfaceWater;

/* 山川流式世界与单块采样 */
typedef struct RegionData {

    struct RegionStream *stream;
    uint32_t        mesh_revision;
    int             width;
    int             height;
    float           step;
    float           origin_x;
    float           origin_z;
    float           center_x;
    float           center_z;
    int             center_cx, center_cz;
    int             site_cell;
    int             climate_season;   /* 采样时的本地气候季 */
    int             site_chunk_type;
    float           site_height;

    float          *heights;
    float          *water_levels;
    float          *shore_depth;
    uint8_t        *coast_material;
    float          *ice_bottom;
    float          *ice_coverage;
    float          *flow_x;        /* 逐顶点东向流速 m/s */
    float          *flow_z;        /* 逐顶点南向流速 m/s */
    uint8_t        *water_kind;    /* 逐顶点 SurfaceWaterKind */
    uint8_t        *river_sediment;
    uint8_t        *water_fresh;
    SourceWaterEffects *source_effects;
    float          *temperatures;
    float          *wetness;
    float          *desert;
    float          *forest;
    float          *aridities;
    float          *uplifts;
    uint8_t        *colors;

    PropInstance   *trees;
    int             tree_count;
    int             tree_cap;

    PropInstance   *rocks;
    int             rock_count;
    int             rock_cap;
    SpringEmitter   springs[4];
    int             spring_count;

    PropInstance    flags[32];
    int             flag_count;

    WaterFormState   water_form;
    WaterFreezeState water_freeze;

    struct IceChunkData *ice;      /* 冰缓存，无水块为 NULL */
    uint32_t         ice_revision; /* 冰专属版本，不提升 mesh_revision */

    TerrainSnapshot undo_stack[MAX_UNDO_STEPS];
    int             undo_top;
    double          sample_ms;
} RegionData;

/* 区块详情检视结构（对齐 Territor */
typedef struct {
    int                     cx, cz;
    int                     macro_col, macro_row;
    int                     cell_id;
    int                     type_id;
    const ChunkTypeDef     *type_def;
    float                   elevation;
    float                   water_level;
    float                   temperature;
    float                   moisture;
    float                   lon_deg, lat_deg;
    float                   discharge;
    float                   river_width;
    float                   river_depth;
    const char             *flow_dir_name;
    const TerritoryFaction *territory;
    SpecialLandform         landform;
} ChunkInspectInfo;

/* 本地存档元数据（对齐 LocalSave */
typedef struct {    bool                valid;
    char                id[64];
    char                file_path[260];
    char                world_name[240];
    char                ren_ming[256];
    uint32_t            main_color;
    PlayMode            play_mode;
    uint32_t            seed;
    CalendarMode        calendar_mode;
    double              unix_ms;
    int                 year, month, day;
    char                saved_at_str[32];
    float               player_x, player_y, player_z;
    float               player_yaw, player_pitch;
    bool                flying;
    WorldCreatorOptions opt;
    uint64_t            reserved_planes;
    AtlasDisplayState   atlas_display;
    /* 玄虚：世界级状态，只随存档走，不放在全局设置里 */
    float               mystery_time_speed;
    bool                mystery_menu_auto_pause;
    int                 delta_count;
    SparseDeltaRecord   deltas[64];
    uint32_t            save_version;   /* 当前槽体版本 */
    uint32_t            disk_version;   /* 磁盘上的头布局版本 */
} SaveSlotInfo;

/* 五槽容器：快速 / 手动 1-3 / 自动 */
#define QLCG_SAVE_SLOT_COUNT 5
#define QLCG_SAVE_SLOT_NAME_BYTES 240
#define QLCG_SAVE_SLOT_DISPLAY_NAME_BYTES (QLCG_SAVE_SLOT_NAME_BYTES + 5)
enum {
    SAVE_SLOT_QUICK      = 0,
    SAVE_SLOT_MANUAL_1   = 1,
    SAVE_SLOT_MANUAL_2   = 2,
    SAVE_SLOT_MANUAL_3   = 3,
    SAVE_SLOT_AUTO       = 4,
    SAVE_SLOT_MANUAL_ANY = -1
};

#define QLCG_SAVE_PACK_MAGIC   "QLCG"
#define QLCG_SAVE_PACK_VERSION 1u

/* 容器头：槽偏移表，槽块仍是单档字节 */
typedef struct {
    char     magic[4];
    uint32_t version;
    uint32_t slot_count;
    uint32_t header_bytes;
    uint64_t offset[QLCG_SAVE_SLOT_COUNT];
    uint64_t size[QLCG_SAVE_SLOT_COUNT];
    uint8_t  present[QLCG_SAVE_SLOT_COUNT];
    uint8_t  kind[QLCG_SAVE_SLOT_COUNT];
    uint8_t  reserved[6];
} SaveContainerHeader;

/* 槽位摘要，列表页用 */
typedef struct {
    bool     present;
    uint8_t  kind;
    long     offset;
    long     size;
    uint32_t disk_version;
    int      year, month, day;
    char     saved_at_str[32];
    char     name[QLCG_SAVE_SLOT_NAME_BYTES];
    char     default_name[QLCG_SAVE_SLOT_DISPLAY_NAME_BYTES];
    double   unix_ms;
    int      delta_count;
} SaveSlotMeta;

static inline const char *qlcg_save_slot_display_name(const SaveSlotMeta *slot) {
    return slot->name[0] ? slot->name : slot->default_name;
}

/* 存档文件目录项，一份文件含五槽 */
typedef struct {
    bool         valid;
    bool         container;
    char         id[64];
    char         file_path[260];
    char         world_name[240];
    uint32_t     seed;
    PlayMode     play_mode;
    CalendarMode calendar_mode;
    int          macro_size_idx;
    int          chunks_per_cell;
    char         saved_at_str[32];
    double       unix_ms;
    int          present_count;
    int          newest_slot;
    SaveSlotMeta slots[QLCG_SAVE_SLOT_COUNT];
} SaveFileInfo;

#define QLCG_SAVE_VERSION 13u
static inline bool qlcg_save_version_supported(uint32_t v) {
#ifdef _WIN32
    if(v==11u || v==12u) return true;
#endif
    return v==QLCG_SAVE_VERSION;
}

bool qlcg_saves_migrate_file(const char *path);
bool qlcg_saves_verify_file(const char *path);
bool qlcg_saves_import_file(const char *source, char *out_path, size_t capacity);

/* 宏观世界与山川区域接口 */
WorldCreatorOptions qlcg_default_creator_options(PlayMode mode, const GameSettings *settings);
void                qlcg_apply_world_preset(WorldCreatorOptions *opt, WorldPresetId preset);
void                qlcg_sync_creator_dimensions(WorldCreatorOptions *opt);
const char         *qlcg_world_preset_name(WorldPresetId p);
const char         *qlcg_terrain_recipe_name(MacroTerrainRecipe r);
const char         *qlcg_climate_rule_name(MacroClimateRule c);
const char         *qlcg_macro_size_label(int idx);
int                 qlcg_macro_scale_chunks(int idx);

MacroWorld         *qlcg_world_generate(const WorldCreatorOptions *opt);
void                qlcg_world_rebuild_atlas_fields(MacroWorld *w);
void                qlcg_world_free(MacroWorld *w);
int                 qlcg_world_cell_at_wxz(const MacroWorld *w, float wx, float wz);
void                qlcg_world_cell_center(const MacroWorld *w, int cell, float *wx, float *wz);
void                qlcg_world_inspect_chunk(const MacroWorld *w, int cx, int cz, ChunkInspectInfo *out);
int                 qlcg_world_natural_chunk_material(const MacroWorld *w, int cx, int cz);
enum { QLCG_SPAWN_REVEAL_RADIUS = 4 };
bool                qlcg_world_reveal_chunk(MacroWorld *w, int cx, int cz);
bool                qlcg_world_chunk_explored(const MacroWorld *w, int cx, int cz);
void                qlcg_world_update_artificial(MacroWorld *w, double unix_ms, int player_cx, int player_cz);

/* 连续表面采样与 3D 山川区域生成 */
void                qlcg_sample_surface_point(const MacroWorld *w, float wx, float wz,
                                              float *out_h, float *out_wl, float *out_temp,
                                              float *out_wet, float *out_desert, float *out_forest,
                                              float *out_aridity, float *out_uplift);
/* 宏观基底高度，不含地貌细节 */
float               qlcg_sample_macro_height(const MacroWorld *w, float wx, float wz);
/* 连续地表采样，保留 LOD 接口 */
void                qlcg_sample_surface_lod(const MacroWorld *w, float wx, float wz, float lod_m,
                                            float *out_h, float *out_wl, float *out_temp,
                                            float *out_wet, float *out_desert, float *out_forest,
                                            float *out_aridity, float *out_uplift);
/* 贴地水位、流动、温度与冰壳 */
void                qlcg_sample_water(const MacroWorld *w, float wx, float wz, int season_idx,
                                      SurfaceWater *out, IceSite *out_ice);
/* 给定地表与水位的水体属性，区块采样复用 */
void                qlcg_sample_water_at(const MacroWorld *w, float wx, float wz, int season_idx,
                                         float bed, float wl, SurfaceWater *out, IceSite *out_ice);
/* 标量与水属性一次算齐（区块采样用） */
void                qlcg_sample_surface_water(const MacroWorld *w, float wx, float wz, int season_idx,
                                              float *out_h, float *out_wl, float *out_temp,
                                              float *out_wet, float *out_desert, float *out_forest,
                                              float *out_aridity, float *out_uplift,
                                              SurfaceWater *out_water, IceSite *out_ice);
RegionData         *qlcg_region_sample(const MacroWorld *w, int center_cx, int center_cz,
                                       int season_idx, const GameSettings *settings);
void                qlcg_region_recolor(RegionData *reg, const MacroWorld *w, int season_idx);
void                qlcg_region_scatter_props(RegionData *reg, const MacroWorld *w,
                                              int season_idx, const GameSettings *settings);
float               qlcg_region_height_at(const RegionData *reg, float lx, float lz);
float               qlcg_region_ice_ground(const RegionData *reg, float x, float z, float half, float feet);
float               qlcg_region_ice_ceiling(const RegionData *reg, float x, float z, float half, float head);
float               qlcg_region_ice_resolve(const RegionData *reg, float x, float y, float z, float height, float half);
bool                qlcg_region_ice_blocked(const RegionData *reg, float x, float y, float z, float height, float half);
float               qlcg_region_slope_at(const RegionData *reg, float lx, float lz);
float               qlcg_region_water_at(const RegionData *reg, float lx, float lz);
void                qlcg_region_free(RegionData *reg);

/* 自然事件与自由自然工程接口 */
const char         *qlcg_nat_event_name(NaturalEventType t);
const char         *qlcg_eng_tool_name(EngToolType t);
const char         *qlcg_eng_tool_desc(EngToolType t);
void                qlcg_trigger_macro_event(MacroWorld *w, NaturalEventType type, float wx, float wz);
void                qlcg_apply_region_event(RegionData *reg, MacroWorld *w, NaturalEventType type,
                                            float lx, float lz, int season_idx, const GameSettings *s);
bool                qlcg_tick_natural_events(MacroWorld *w, RegionData *reg, float dt,
                                             float *out_shake, float *out_wind, float *out_rain);

void                qlcg_eng_push_undo(RegionData *reg, const MacroWorld *w);
bool                qlcg_eng_undo(RegionData *reg, MacroWorld *w, int season_idx, const GameSettings *s);
void                qlcg_eng_macro_brush(MacroWorld *w, EngToolType tool, float wx, float wz, float radius);
void                qlcg_eng_macro_river(MacroWorld *w, float x0, float z0, float x1, float z1, float width_m);
void                qlcg_eng_region_brush(RegionData *reg, MacroWorld *w, EngToolType tool,
                                          float lx, float lz, float radius, int season_idx, const GameSettings *s);
void                qlcg_eng_region_river(RegionData *reg, MacroWorld *w, const float *pts_x, const float *pts_z,
                                          int pt_count, float width_m, float depth_m, float meander,
                                          int season_idx, const GameSettings *s);

/* 存档管理接口（对齐 SaveStore） */
void                qlcg_init_atlas_display(AtlasDisplayState *st);
void                qlcg_normalize_atlas_display(AtlasDisplayState *st);
bool                qlcg_atlas_layer_move(AtlasDisplayState *st, int from, int to);
float               qlcg_atlas_layer_opacity(const AtlasDisplayState *st, AtlasLayerId id);
bool                qlcg_atlas_layer_has_opacity(AtlasLayerId id);
int                 qlcg_saves_scan(SaveFileInfo *out_files, int max_files);
bool                qlcg_saves_load_slot(const SaveFileInfo *file, int slot, SaveSlotInfo *out,
                                         long *out_offset, long *out_size);
bool                qlcg_saves_restore_deltas(MacroWorld *w, const SaveSlotInfo *slot,
                                              long offset, long size);
bool                qlcg_saves_write_slot(const MacroWorld *w, const CalendarClock *clk,
                                           const AtlasDisplayState *atlas_st,
                                           float px, float py, float pz, float yaw, float pitch,
                                           bool flying, bool menu_auto_pause,
                                           const char *file_id, const char *save_name,
                                           int slot_kind, char *out_id, size_t id_cap,
                                           int *out_slot, char *out_msg, size_t msg_cap);
bool                qlcg_saves_rename(const char *file_path, const char *new_name,
                                      char *out_msg, size_t msg_cap);
bool                qlcg_saves_write_slot_named(const MacroWorld *w, const CalendarClock *clk,
                                               const AtlasDisplayState *atlas_st,
                                               float px, float py, float pz, float yaw, float pitch,
                                               bool flying, bool menu_auto_pause,
                                               const char *file_id, const char *save_name,
                                               int slot_kind, const char *slot_name,
                                               char *out_id, size_t id_cap, int *out_slot,
                                               char *out_msg, size_t msg_cap);
bool                qlcg_saves_rename_slot(const char *file_path, int slot, const char *name,
                                          char *out_msg, size_t msg_cap);
bool                qlcg_saves_copy_to_leisure(const SaveFileInfo *file, int slot,
                                               char *out_msg, size_t msg_cap);

#endif /* QLCG_PLANET_MACRO_H */
