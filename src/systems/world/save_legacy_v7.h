/* 十一图层存档布局冻结 */
#ifndef QLCG_SAVE_LEGACY_V7_H
#define QLCG_SAVE_LEGACY_V7_H
#include "save_legacy_v8.h"
typedef struct {
    AtlasLayerSetting layers[11];
    int contour_density;
} AtlasDisplayStateV7;
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
    WorldCreatorOptionsV8 opt;
    AtlasDisplayStateV7 atlas_display;
    /* 玄虚世界级设置 */
    float               mystery_time_speed;
    bool                mystery_menu_auto_pause;
    int                 delta_count;
    SparseDeltaRecord   deltas[64];
    uint32_t            save_version;   /* 结构版本，兼容 v5/v6 */
    uint32_t            disk_version;   /* 磁盘上的头布局版本 */
} SaveSlotInfoV7;
#endif

