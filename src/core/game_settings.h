#ifndef QLCG_GAME_SETTINGS_H
#define QLCG_GAME_SETTINGS_H

#include "config.h"
#include "input_bind.h"

/* 玩家档案配置（对齐 PlayerProf */
typedef struct {
    char     name[256];
    int      height_cm;
    uint32_t main_color;
    uint32_t trim_color;
} PlayerProfile;

/* 全局游戏设置（对齐 GameSettin */
typedef struct {
    PlayerProfile     profile;
    char              ren_ming[256];
    MeasurementSystem measurement_system;

    /* 界面设置 */
    ThemeMode         theme;
    bool              show_top_bar;
    CrosshairStyle    crosshair;
    bool              profile_palette_to_ui;
    float             ui_scale;

    /* 图像与自然物理设置 */
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

    /* 视图偏好，世界态见SaveSlotInfo */
    ViewMode          view_mode;

    /* 须置于末尾，旧settings.bin前缀才对 */
    float             safe_x;      /* 左右安全区，占宽度百分比 */
    float             safe_y;      /* 上下安全区，占高度百分比 */
    float             text_scale;  /* 文字缩放倍数 */
    bool              reduce_motion; /* 关闭界面过渡与点击水波 */

    /* 输入绑定；magic 标识旧存档未初始化区 */
    uint32_t          binds_magic;
    QlBind            binds[ACT_COUNT];

    /* 界面独立配色：仅当「将偏好色应用到界面」关闭时生效 */
    uint32_t          interface_main_color;
    uint32_t          interface_trim_color;

    /* 字体 id，尾部扩容不挪前序偏移 */
    char              font_id[96];

    /* 追加项；旧存档读不到则取默认 */
    bool              mirror_reflections;  /* base 中为「即将支持」的禁用项 */
    char              resolution[16];      /* "auto" 或 "1280x720" */
    char              locale[32];          /* 界面语言代码，如 "zh-CN" / "en" / "ja" */

    /* 触屏与虚拟按键 */
    bool              touch_controls;      /* 游戏内显示摇杆与动作键 */
    int               joystick_style;      /* 摇杆形态 0 = jxqy（暂只实现这一种） */
    float             touch_opacity;       /* 虚拟按键透明度 0.1 ~ 1.0 */

    /* 高分辨率屏适配：把窗口整体放大的倍率，1 / 2 / 4 */
    float             hidpi_scale;   /* 新基准倍率，实际缩放为两倍 */

    /* 虚拟按键落位：4项×(x%,y%,尺寸) */
    float             touch_layout[4][3];

    /* 视角反转：鼠标与手柄共用，追加在最末以兼容旧档案 */
    bool              invert_look_x;
    bool              invert_look_y;
} GameSettings;

#define QLCG_BINDS_MAGIC 0xB1AD0001u
#define QLCG_SETTINGS_MAGIC    "QLCGSTG7"
#define QLCG_SETTINGS_MAGIC_V6 "QLCGSTG6"
#define QLCG_SETTINGS_MAGIC_V5 "QLCGSTG5"
#define QLCG_SETTINGS_MAGIC_V4 "QLCGSTG4"
#define QLCG_MAX_VEGETATION_DENSITY 9

typedef struct {
    float understory;
    float trees;
} VegetationDensity;

/* 初始化、校验与持久化设置接口 */
GameSettings qlcg_default_settings(void);
VegetationDensity qlcg_vegetation_density(int level);
/* 恢复默认但保留界面语言：语言属于使用习惯 */
void         qlcg_reset_settings_keep_locale(GameSettings *s);
void         qlcg_apply_graphics_preset(GameSettings *s, GraphicsPreset preset);
void         qlcg_load_settings_disk(GameSettings *s);
bool         qlcg_settings_write(const char *path, const GameSettings *s);
void         qlcg_save_settings_disk(const GameSettings *s);
/* 对齐 base defaultTouchLayout */
void         qlcg_touch_layout_default(float out[4][3]);
const char  *qlcg_graphics_preset_name(GraphicsPreset p);
const char  *qlcg_veg_form_name(VegetationForm f);
const char  *qlcg_crosshair_name(CrosshairStyle c);
const char  *qlcg_theme_name(ThemeMode t);

#endif /* QLCG_GAME_SETTINGS_H */
