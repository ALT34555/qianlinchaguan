#ifndef QLCG_CONFIG_H
#define QLCG_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* 核心世界尺度常量（对齐 base） */
#define CHUNK_SIZE             32
#define TERRAIN_GRID           2
#define CHUNK_CELLS            16
#define TILE_SIZE              128
#define MIN_ATLAS_ZOOM         2
#define MAX_ATLAS_ZOOM         24
#define MIN_RENDER_DISTANCE    4
#define MAX_RENDER_DISTANCE    32
#define DEFAULT_RENDER_DIST    12
/* 植被近景全量半径（米） */
#define QLCG_PROP_NEAR_M       330.0f
/* 3D 全模型半径（区块） */
#define QLCG_PROP_FULL_CHUNKS  12
/* 交叉面片起点（区块），与 3D 同界 */
#define QLCG_PROP_CROSS_START_CHUNKS 12
/* 交叉面片半径（区块） */
#define QLCG_PROP_SPRITE_CHUNKS 18
/* 植被实例重建最小间隔（毫秒） */
#define QLCG_PROP_REBUILD_MS   120.0
/* 远景实例出现淡入时长（毫秒） */
#define QLCG_PROP_FADE_MS      250.0
#define OVERVIEW_TILE_BUDGET   720
/* 世界竖直边界，对齐base config.ts */
#define QLCG_WORLD_MIN_Y       (-4096)
#define QLCG_WORLD_MAX_Y       20480
/* HUD 昼夜判据：区块本地时刻落在 [起, 止) 为日间 */
#define QLCG_DAY_HOUR_START    6
#define QLCG_DAY_HOUR_END      18

#define MAX_PLATES             36
#define MAX_RIVERS             24576
#define MAX_FACTIONS           128
#define MAX_ACTIVE_EVENTS      16
#define MAX_MACRO_DELTAS       2048
#define MAX_CHUNK_DELTAS       2048
#define MAX_UNDO_STEPS         16
#define MAX_SAVES              64
#define MAX_SAVE_SLOTS         64
#define MAX_CHAT_MSGS          32
#define QLCG_CHUNK_SIZE_M      CHUNK_SIZE
#define QLCG_MIN_ATLAS_ZOOM    MIN_ATLAS_ZOOM
#define QLCG_MAX_ATLAS_ZOOM    MAX_ATLAS_ZOOM

/* 兼容枚举别名 */
#define SCREEN_PLAY_MODE_SELECT SCREEN_CREATE_MODE
#define SCREEN_MEMORIES_LOAD    SCREEN_SAVES
#define RAIL_TAB_HOME           TAB_HOME
#define RAIL_TAB_MEMORIES       TAB_SAVES
#define RAIL_TAB_SETTINGS       TAB_SETTINGS

/* 书签栏存 0..2 项索引，非上面的枚举值 */
#define RAIL_IDX_HOME           0
#define RAIL_IDX_SAVES          1
#define RAIL_IDX_SETTINGS       2
#define VIEW_MODE_ATLAS         VIEW_MODE_2D
#define VIEW_MODE_TERRAIN       VIEW_MODE_3D
#define CALENDAR_MODE_GREGORIAN CALENDAR_GREGORIAN
#define CALENDAR_MODE_YUAN      CALENDAR_YUAN
#define GRAPHICS_FLUENT         PRESET_SMOOTH
#define GRAPHICS_EXTREME        PRESET_EXTREME
#define CROSSHAIR_HIDDEN        CROSSHAIR_NONE
#define PLANET_VIEW_EQUIRECT    PLANET_VIEW_PROJECTION

#define LAYER_TERRITORIES       LAYER_CIVILIZATION
#define LAYER_MERIDIAN          LAYER_BASELINE
#define LAYER_CLOUDS            LAYER_RAIN
#define LAYER_CONTOURS          LAYER_CONTOUR
#define LAYER_ELEVATION         LAYER_HEIGHT

#define FILTER_FLAG             FILTER_RIVER
#define FILTER_CLAIMED          FILTER_WATER
#define FILTER_PLAIN            FILTER_MOUNTAIN
#define FILTER_HILL             FILTER_SNOW
#define FILTER_PLATEAU          FILTER_FOREST
#define FILTER_SHORE            6
#define FILTER_LANDFORM         7
#define FILTER_COUNT            8

#define NAT_EVENT_DESERTIFY     NAT_EVENT_DESERTIFICATION
#define NAT_EVENT_COLD_WAVE     NAT_EVENT_COLDWAVE

#define ENG_TOOL_DEMOLISH       ENG_TOOL_DESTROY
#define ENG_TOOL_AFFOREST       ENG_TOOL_FOREST
#define ENG_TOOL_RIVER_DIVERT   ENG_TOOL_RIVER
#define ENG_TOOL_RECLAIM_SEA    ENG_TOOL_RECLAIM
#define ENG_TOOL_RAISE_MOUNT    5
#define ENG_TOOL_DIG_LAKE       6
#define ENG_TOOL_FLATTEN        7

#define WATER_FORM_LOW_FLOW     WATER_FORM_LOW
#define WATER_FORM_CUTOFF       WATER_FORM_DRY
#define WATER_FORM_FLOOD        WATER_FORM_FLOODED
#define WATER_FREEZE_THIN_ICE   WATER_FREEZE_PARTIAL
#define WATER_FREEZE_THICK_ICE  WATER_FREEZE_FULL

#define PLANT_STATE_LUSH        PLANT_STATE_NORMAL
#define PLANT_STATE_FLOWER      PLANT_STATE_FLOWERING
#define PLANT_STATE_AUTUMN      PLANT_STATE_FRUITING
#define PLANT_STATE_DEAD        PLANT_STATE_WITHERED
#define PLANT_STATE_SNOW        PLANT_STATE_PLANTED

/* 主界面与场景枚举（对齐 base） */
typedef enum {
    SCREEN_START_HOME     = 0,
    SCREEN_CREATE_MODE    = 1,
    SCREEN_WORLD_CREATOR  = 2,
    SCREEN_SAVES          = 3,
    SCREEN_SETTINGS       = 4,
    SCREEN_IN_GAME        = 5,
    SCREEN_SPRITE_TEST    = 6,
} ScreenState;

/* 书签栏页签枚举 */
typedef enum {
    TAB_HOME     = 0,
    TAB_CREATE   = 1,
    TAB_SAVES    = 2,
    TAB_SETTINGS = 3
} RailTabKey;

/* 游戏模式：生存 / 悠游 */
typedef enum {
    PLAY_MODE_LEISURE  = 0,
    PLAY_MODE_SURVIVAL = 1
} PlayMode;

/* 视图模式：2D舆图 / 3D山川 */
typedef enum {
    VIEW_MODE_2D = 0,
    VIEW_MODE_3D = 1
} ViewMode;

/* 历法模式：公历+农历 / 元历 */
typedef enum {
    CALENDAR_DEFAULT = 0,
    CALENDAR_REAL = CALENDAR_DEFAULT,
    CALENDAR_YUAN = 1,
    CALENDAR_GREGORIAN = 2,
    CALENDAR_LUNAR = 3
} CalendarMode;

/* 度量衡系统：明制 / 公制 / 英制 */
typedef enum {
    MEAS_MING     = 0,
    MEAS_METRIC   = 1,
    MEAS_IMPERIAL = 2
} MeasurementSystem;

/* 画质预设 6 档 */
typedef enum {
    PRESET_MINIMAL  = 0,
    PRESET_LOW      = 1,
    PRESET_SMOOTH   = 2,
    PRESET_BALANCED = 3,
    PRESET_HIGH     = 4,
    PRESET_EXTREME  = 5
} GraphicsPreset;

/* 植被形态 3 档 */
typedef enum {
    VEG_FORM_SIMPLE = 0,
    VEG_FORM_NORMAL = 1,
    VEG_FORM_RICH   = 2
} VegetationForm;

/* 准心形态 5 档 */
typedef enum {
    CROSSHAIR_NONE  = 0,
    CROSSHAIR_DOT   = 1,
    CROSSHAIR_CROSS = 2,
    CROSSHAIR_AIM   = 3,
    CROSSHAIR_RING  = 4
} CrosshairStyle;

/* 日夜主题模式 */
typedef enum {
    THEME_LIGHT = 0,
    THEME_DARK  = 1,
    THEME_AUTO  = 2
} ThemeMode;

/* 星球推荐预设 5 档 */
typedef enum {
    WORLD_PRESET_HABITABLE   = 0,
    WORLD_PRESET_PRIMORDIAL  = 1,
    WORLD_PRESET_FROZEN      = 2,
    WORLD_PRESET_ARCHIPELAGO = 3,
    WORLD_PRESET_REDHOT      = 4,
    WORLD_PRESET_COUNT       = 5
} WorldPresetId;

/* 宏观地形配方 4 档 */
typedef enum {
    TERRAIN_BALANCED    = 0,
    TERRAIN_ARCHIPELAGO = 1,
    TERRAIN_MOUNTAINS   = 2,
    TERRAIN_RIFT        = 3,
    TERRAIN_RECIPE_CNT  = 4
} MacroTerrainRecipe;

/* 旧气候规则 7 档，仅用于 v8 及更早存档迁移 */
typedef enum {
    CLIMATE_WIND_FIELD    = 0,
    CLIMATE_PROGRADE_0    = 1,
    CLIMATE_PROGRADE_23   = 2,
    CLIMATE_PROGRADE_60   = 3,
    CLIMATE_RETROGRADE_0  = 4,
    CLIMATE_RETROGRADE_23 = 5,
    CLIMATE_RETROGRADE_60 = 6
} MacroClimateRule;

/* 舆图全览投影模式 */
typedef enum {
    PLANET_VIEW_GLOBE      = 0,
    PLANET_VIEW_PROJECTION = 1,
    PLANET_VIEW_MERCATOR   = 2
} PlanetView;

/* 舆图 11 层叠加图层 ID */
typedef enum {
    LAYER_CIVILIZATION = 0,
    LAYER_CHUNKS       = 1,
    LAYER_BASELINE     = 2,
    LAYER_DAYLIGHT     = 3,
    LAYER_RAIN         = 4,
    LAYER_CONTOUR      = 5,
    LAYER_CLIMATE      = 6,
    LAYER_TEMPERATURE  = 7,
    LAYER_HEIGHT       = 8,
    LAYER_ICE          = 9,
    LAYER_DEPTH        = 10,
    LAYER_SOURCE_RIVERS = 11,
    LAYER_ID_COUNT     = 12
} AtlasLayerId;

/* 舆图特定区块筛选器 */
typedef enum {
    FILTER_NONE     = 0,
    FILTER_RIVER    = 1,
    FILTER_WATER    = 2,
    FILTER_MOUNTAIN = 3,
    FILTER_SNOW     = 4,
    FILTER_FOREST   = 5
} AtlasQuickFilter;

/* 自然事件类型 */
typedef enum {
    NAT_EVENT_TYPHOON         = 0,
    NAT_EVENT_EARTHQUAKE      = 1,
    NAT_EVENT_DESERTIFICATION = 2,
    NAT_EVENT_FLOOD           = 3,
    NAT_EVENT_COLDWAVE        = 4,
    NAT_EVENT_WILDFIRE        = 5,
    NAT_EVENT_COUNT           = 6
} NaturalEventType;

/* 自由自然工程工具 */
typedef enum {
    ENG_TOOL_NONE    = 0,
    ENG_TOOL_DESTROY = 1,
    ENG_TOOL_FOREST  = 2,
    ENG_TOOL_RIVER   = 3,
    ENG_TOOL_RECLAIM = 4,
    ENG_TOOL_COUNT   = 8
} EngToolType;

/* 水体四形态（对齐 base） */
typedef enum {
    WATER_FORM_NORMAL  = 0,
    WATER_FORM_LOW     = 1,
    WATER_FORM_DRY     = 2,
    WATER_FORM_FLOODED = 3
} WaterFormState;

/* 水面冻结三状态（对齐 base） */
typedef enum {
    WATER_FREEZE_NONE    = 0,
    WATER_FREEZE_PARTIAL = 1,
    WATER_FREEZE_FULL    = 2
} WaterFreezeState;

/* 植物六类，顺序同 base */
typedef enum {
    PLANT_CAT_CAO = 0,
    PLANT_CAT_GUAN,
    PLANT_CAT_HUA,
    PLANT_CAT_NONG,
    PLANT_CAT_SHU,
    PLANT_CAT_SHUI,
    PLANT_CAT_COUNT
} PlantCategory;
_Static_assert(PLANT_CAT_SHUI == 5, "plant category order");

typedef enum {
    PLANT_STATE_NORMAL    = 0,
    PLANT_STATE_SEEDLING  = 1,
    PLANT_STATE_SUBADULT  = 2,
    PLANT_STATE_FLOWERING = 3,
    PLANT_STATE_FRUITING  = 4,
    PLANT_STATE_WITHERED  = 5,
    PLANT_STATE_PLANTED   = 6
} PlantModelState;

/* 特殊特征地貌枚举 */
typedef enum {
    LANDFORM_NORMAL   = 0,
    LANDFORM_VOLCANO  = 1,
    LANDFORM_KARST    = 2,
    LANDFORM_YARDANG  = 3,
    LANDFORM_GLACIER  = 4,
    LANDFORM_FJORD    = 5,
    LANDFORM_MANGROVE = 6
} SpecialLandform;

#endif /* QLCG_CONFIG_H */
