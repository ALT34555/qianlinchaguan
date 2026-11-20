#ifndef QLCG_SAVE_LEGACY_V12_H
#define QLCG_SAVE_LEGACY_V12_H
#include "save_legacy_v8.h"
typedef struct {
    bool                valid;
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
    AtlasDisplayStateV8 atlas_display;
    /* 世界级玄虚状态 */
    float               mystery_time_speed;
    bool                mystery_menu_auto_pause;
    int                 delta_count;
    SparseDeltaRecord   deltas[QLCG_LEGACY_INLINE_DELTAS];
    uint32_t            save_version;   /* 当前槽体版本 */
    uint32_t            disk_version;   /* 磁盘上的头布局版本 */
} SaveSlotInfoV12;
#endif
