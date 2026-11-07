/* v4 设置布局冻结，仅旧档迁移用 */
#ifndef QLCG_SETTINGS_LEGACY_V4_H
#define QLCG_SETTINGS_LEGACY_V4_H

#include "game_settings.h"

/* ACT_QUICKSAVE 之前的动作数 */
#define QLCG_LEGACY_ACT_COUNT 10

typedef struct {
    PlayerProfile     profile;
    char              ren_ming[256];
    MeasurementSystem measurement_system;

    ThemeMode         theme;
    bool              show_top_bar;
    CrosshairStyle    crosshair;
    bool              profile_palette_to_ui;
    float             ui_scale;

    GraphicsPreset    graphics_preset;
    int               fov;
    int               render_distance;
    int               vegetation_distance;
    VegetationForm    vegetation_form;
    int               dense_vegetation;
    int               frame_rate;
    bool              antialias;
    bool              water_reflections;
    bool              water_waves;
    bool              particles;
    bool              weather_atmosphere;
    bool              decorative_clouds;
    bool              natural_physics;
    bool              status_feedback;

    ViewMode          view_mode;

    float             safe_x;
    float             safe_y;
    float             text_scale;
    bool              reduce_motion;

    uint32_t          binds_magic;
    QlBind            binds[QLCG_LEGACY_ACT_COUNT];

    uint32_t          interface_main_color;
    uint32_t          interface_trim_color;

    char              font_id[96];

    bool              mirror_reflections;
    char              resolution[16];
    char              locale[32];

    bool              touch_controls;
    int               joystick_style;
    float             touch_opacity;

    float             hidpi_scale;

    float             touch_layout[4][3];

    bool              invert_look_x;
    bool              invert_look_y;
} GameSettingsV4;

_Static_assert(sizeof(GameSettingsV4) == 888, "v4 settings layout");
_Static_assert(offsetof(GameSettingsV4, binds) == offsetof(GameSettings, binds),
               "v4 prefix layout");

#endif /* QLCG_SETTINGS_LEGACY_V4_H */
