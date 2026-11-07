/* v5 存档结构冻结 */
#ifndef QLCG_SAVE_LEGACY_V5_H
#define QLCG_SAVE_LEGACY_V5_H
#include "save_legacy_v7.h"

typedef struct {
    char               world_name[80];
    char               ren_ming[256];
    int                height_cm;
    PlayMode           play_mode;

    uint32_t           seed;
    char               seed_str[64];
    WorldPresetId      preset;
    MacroTerrainRecipe terrain_recipe;
    MacroClimateRule   climate_rule;
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

    
    float              creation_time_speed;
    CalendarMode       calendar_mode;
    int                start_year;
    int                start_month;
    int                start_day;

    
    bool               artificial_enabled;
    bool               spawn_as_flag;
    char               faction_name[40];
    uint32_t           faction_main_color;
    uint32_t           faction_trim_color;
    int                initial_civ_limit;
    float              natural_expansion;
    float              natural_evolution;
    bool               flag_independence;
    int                monsoon_direction;   
} WorldCreatorOptionsV5;

typedef struct {    bool                valid;
    char                id[64];
    char                file_path[260];
    char                world_name[80];
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
    WorldCreatorOptionsV5 opt;
    AtlasDisplayStateV7 atlas_display;
    
    float               mystery_time_speed;
    bool                mystery_menu_auto_pause;
    int                 delta_count;
    SparseDeltaRecord   deltas[64];
    uint32_t            save_version;   
} SaveSlotInfoV5;

#endif
