/* 应用状态与跨模块声明 */
#ifndef QLCG_APP_STATE_H
#define QLCG_APP_STATE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core/config.h"
#include "core/math_util.h"
#include "core/measurements.h"
#include "core/game_settings.h"
#include "core/i18n.h"
#include "core/screenshot.h"
#include "systems/calendar/calendar.h"
#include "systems/world/chunk_types.h"
#include "systems/world/planet_macro.h"
#include "terrain3d/gl_loader.h"
#include "ui/ui_core.h"
#include "ui/ui_widgets.h"
#include "ui/ui_status_feedback.h"
#include "ui/ui_toast.h"
#include "ui/ui_touch.h"
#include "ui/ui_font_mod.h"
#include "atlas/atlas_view.h"

/* 聊天输入上限：1024 字，含四字节余量 */
#define QLCG_CHAT_MAX_CHARS   1024u
#define QLCG_CHAT_INPUT_BYTES (QLCG_CHAT_MAX_CHARS * 4u + 2048u)
#define QLCG_CHAT_LINE_BYTES  (QLCG_CHAT_MAX_CHARS * 4u + 2048u)
#define UIS_CHAT_LIST         0x43484154u   /* CHAT 历史滚动区 */
#define QLCG_AUTOSAVE_SECONDS 900.0f
#define QLCG_AUTOSAVE_RETRY   60.0f
#define QLCG_SAVE_FLASH_SECONDS 2.6f
#include "terrain3d/terrain_view.h"
#include "app_river_craft.h"

typedef enum {
    SET_TAB_PROFILE = 0,
    SET_TAB_UI,
    SET_TAB_GRAPHICS,
    SET_TAB_CONTROLS,
    SET_TAB_METAPHYSICS
} SettingsSubTab;

/* 屏幕矩形（左、上、宽、高） */
typedef struct { float x, y, w, h; } UiRect;

typedef struct {
    HWND               hwnd;
    HDC                hdc;
    HGLRC              hglrc;
    int                win_w, win_h;
    bool               running;

    ScreenState        screen;
    ScreenState        prev_screen;
    RailTabKey         rail_tab;
    bool               rail_collapsed;
    SettingsSubTab     set_tab;

    GameSettings       settings;
    UiModFontList      mod_fonts;
    WorldCreatorOptions creator_opt;
    bool               fold_geo;
    bool               fold_astro;
    bool               fold_human;
    bool               set_fold_visual;  /* 设置页「高级视觉」折叠态 */
    int                creator_tab;
    int                edit_field_id;
    UiTabPending       rail_pending;

    SaveFileInfo       saves[MAX_SAVE_SLOTS];
    int                save_count;
    int                save_filter_mode;
    int                save_page;          /* 存档页当前页码，筛选或刷新后归零 */
    /* 改名编辑态：id 为空表示未进入 */
    char               save_edit_id[64];
    char               save_edit_buf[240];
    /* 当前存档文件与手动槽选择 */
    char               current_save_id[64];
    char               save_expanded_id[64];
    int                manual_slot;        /* 手存目标槽 1..3 */
    bool               manual_slot_user;   /* 是否玩家手选 */
    bool               manual_slots_open;
    char               manual_slot_names[SAVE_SLOT_MANUAL_3][QLCG_SAVE_SLOT_NAME_BYTES];
    float              save_idle_s;        /* 距上次读档/存档的秒数 */
    float              auto_save_flash;    /* 自动存档图标剩余秒 */
    UiToastQueue       toasts;

    /* 游戏内核心系统状态 */
    MacroWorld        *world;
    RegionData        *region;
    CalendarClock      clock;
    CalendarSnapshot   snap;
    ViewMode           view_mode;
    int                player_cx, player_cz;

    UiContext          ui;
    UiTouch            touch;
    StatusFeedback     status_feedback;   /* 七种状态特效与 501 模糊 */
    bool               touch_mouse;   /* 调试：鼠标驱动虚拟按键 */
    AtlasRenderer      atlas;
    TerrainRenderer    terrain;
    RiverCraft         river_craft;

    /* F12 调试面板、ESC 暂停与聊天指令面板 */
    bool               show_debug_f12;
    bool               esc_paused;
    bool               world_menu_auto_pause;  /* 唤出菜单是否暂停世界，随存档保存 */
    bool               cheats_unlocked;        /* 生存模式解锁玄虚，运行时状态不持久化 */
    char               save_name[256];         /* 总存档名称 */
    char               loaded_slot_name[QLCG_SAVE_SLOT_DISPLAY_NAME_BYTES];
    bool               settings_back_to_game;  /* 设置页从游戏抽屉进入，可返回游戏 */
    bool               settings_in_drawer;     /* 游戏内设置以抽屉叠在场景上，对齐 base */
    bool               chat_open;
    bool               touch_layout_edit; /* 虚拟按键布局编辑中 */
    char               chat_input[QLCG_CHAT_INPUT_BYTES];
    char               chat_logs[MAX_CHAT_MSGS][QLCG_CHAT_LINE_BYTES];
    int                chat_log_count;
    double             chat_last_message_ms;

    float              fps_smooth;
    double             airdrop_ms;
} AppState;

extern AppState g_app;

/* 动作 */
void app_push_toast(const char *msg);
/* 带类型通知：kind 取 UI_TOAST_* */
void app_push_toast_k(int kind, const char *title, const char *msg);
void app_push_chat(const char *line);
/* 是否有文本框正在接收键盘 */
bool app_text_input_active(void);
/* 键盘状态表：未被文本编辑消费的键才入表 */
void app_key_track(unsigned int vk, bool down);
void app_key_track_clear(void);
bool app_key_down(unsigned int vk);
/* 释放文本键盘占用：Esc、切页、控件消失 */
void app_text_focus_clear(void);
/* 聊天发送：Enter 与发送按钮共用 */
bool app_chat_send(void);
void app_airdrop_to_chunk(int cx, int cz, bool switch_to_3d);
bool app_atlas_airdrop(double wx,double wz);
/* 冰时间：全世界统一时刻，0~3 强制季节 */
void app_ice_sync(int forced_season);
void app_release_world(void);
void app_infinite_step(void);
bool app_rebuild_creator_world(void);
void app_refresh_creator_clock(void);
void app_enter_world_from_creator(void);
/* 统一保存入口：槽号见 SAVE_SLOT_*，成功会清零自动存档计时 */
bool app_save_current(int slot_kind, bool notify);
bool app_save_manual_slot(int slot, const char *name);
bool app_rename_manual_slot(int slot, const char *name);
/* 快速存档与自动存档计时 */
void app_quick_save(void);
void app_save_tick(float dt);
void app_refresh_saves(void);
/* 当前文件的手动槽摘要，无档返回 NULL */
const SaveSlotMeta *app_current_save_slot(int slot);
int  app_manual_slot_default(void);
void app_manual_slot_reset(void);
/* 槽位显示名：快速/手动 N/自动 */
void app_slot_kind_label(int slot, char *buf, size_t cap);
/* 读档：文件+槽，或已展开的槽头 */
bool app_load_from_save_file(const SaveFileInfo *file, int slot);
bool app_load_from_save_slot(const SaveSlotInfo *slot, long offset, long size);
/* 存档文件级操作 */
bool app_export_save_file(const SaveFileInfo *file);
bool app_export_current_save(void);
bool app_save_rename(const SaveFileInfo *file, const char *new_name);
bool app_save_copy_leisure(const SaveFileInfo *file, int slot);
void app_exec_chat_command(const char *cmd);

/* 页面共用 */
UiRect draw_page_shell(UiContext *ui);
void render_rail_language_popup(UiContext *ui);

/* 页面 */
void render_screen_home(UiContext *ui, float dt);
void render_screen_play_mode_select(UiContext *ui);
void render_screen_world_creator(UiContext *ui);
void render_screen_memories(UiContext *ui);
void render_screen_settings(UiContext *ui);
bool settings_confirm_active(void);
void settings_confirm_cancel(void);
void settings_confirm_begin(float safe_x, float safe_y, float text_scale, float ui_scale);
void settings_layout_cancel(void);
void render_screen_in_game(UiContext *ui);
void render_debug_overlay_f12(UiContext *ui);

/* 游戏内分层视图 */
bool game_view_atlas(UiContext *ui);
void game_view_terrain(UiContext *ui);
void game_top_bar(UiContext *ui);
void game_save_indicator(UiContext *ui);
void game_chat_panel(UiContext *ui);
bool game_chat_over_ui(UiContext *ui);
bool game_debug_over_ui(UiContext *ui);
bool game_scene_over_ui(UiContext *ui);
/* 占领/景气 HUD 已归档 003backup */
/* 常驻小地图已归档，改由 F12 的 5x5 图承担 */
void game_esc_menu(UiContext *ui);

/* 应用骨架 */
bool init_win32_gl(HINSTANCE hinst, int w, int h);
void app_toggle_world_view(void);
void app_apply_font(void);
/* 文本输入通路：追加一个 UTF-16 码元到当前聚焦框 */
void app_input_push_utf16(UiContext *ui, uint32_t unit);

/* 设置页上下文：三套界面差异，对齐 base */
bool     app_world_active(void);      /* 从游戏抽屉进入设置 */

/* 3D 走离屏再拉伸，UI 仍按窗口分辨率 */
void app_resolution_size(int win_w, int win_h, int *out_w, int *out_h);
bool app_lowres_active(int win_w, int win_h);
void app_lowres_release(void);
bool app_lowres_begin(int win_w, int win_h);
void app_lowres_end(int win_w, int win_h, int pix_w, int pix_h, bool pixelated);
bool app_lowres_pixelated(void);
bool app_lowres_msaa(void);
bool     app_cheats_allowed(void);    /* 悠游 或 已解锁 */
PlayMode app_current_play_mode(void);
/* 玄虚解锁指令，同 base 的 unlock */
#define QLCG_UNLOCK_CMD "/world.qianlin 天地無窮極陰陽轉相因"
/* 身高范围：解锁 8~1024，否则 64~256 */
void     app_profile_height_range(int *min_cm, int *max_cm);
/* 设置 → UI 三轴（尺寸/文字/安全区）同步 */
void app_sync_layout_axes(void);
/* mod 字体目录，扫描与重新扫描共用 */
#define QLCG_FONT_DIR L"userdata\\mods\\fonts"
void app_rescan_fonts(void);

/* 按「将偏好色应用到界面」选档案色或独立界面色 */
void app_apply_ui_palette(void);

/* 跟随系统时，山川按区块本地时刻切昼夜 */
void app_sync_hud_daynight(void);

/* 输入绑定：WM_KEYDOWN 匹配 / 持续按下 */
bool app_action_match(int act, unsigned int vk);
/* 视角增量：套用反转设置并钳制俯仰，正值 limit */
void app_look_apply(float d_yaw, float d_pitch, float limit);
/* 指针锁定开关，由 want_mouse_lock 定 */
void app_set_mouse_lock(bool on);
/* 该锁鼠标的状态：山川、未暂停、无抽屉、非触屏 */
bool app_want_mouse_lock(void);
void app_apply_hidpi_scale(void);
/* 触屏虚拟动作：与键盘并列，任一来源按下即生效 */
void app_action_set_virtual(int act, bool on);
bool app_action_virtual(int act);
bool app_action_down(int act);
void app_astra_advance(double before);
void app_astra_report(void);
void app_render_frame(float dt);
void app_build_player_input(PlayerInput *in);

#endif /* QLCG_APP_STATE_H */
