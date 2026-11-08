/* 应用状态实例 */
#include "app/app_state.h"
#include "core/dev_tools.h"
#include "ui/ui_input.h"
#include "platform/android/bridge.h"

AppState g_app;

/* 按 font_id 应用字体，system 回默认 */
void app_apply_font(void) {
    const char *id = g_app.settings.font_id;
    if (!id[0] || strcmp(id, "system") == 0) {
        ui_set_font_family(&g_app.ui, L"Microsoft YaHei UI", L"Microsoft YaHei UI", L"Consolas");
        return;
    }
    const UiModFont *f = ui_font_mod_find(&g_app.mod_fonts, id);
    if (!f) {
        printf("[Font] 字体 id 未找到：%s（可用 %d 个）\n", id, g_app.mod_fonts.count);
        return;
    }
    ui_set_font_family(&g_app.ui, f->family, f->family, L"Consolas");
}

/* 重扫 mod 字体目录：卸载旧私有字体后重扫 */
void app_rescan_fonts(void) {
    ui_font_mod_unload();
    ui_font_mod_scan(QLCG_FONT_DIR, &g_app.mod_fonts);
    if (strcmp(g_app.settings.font_id, "system") != 0 &&
        !ui_font_mod_find(&g_app.mod_fonts, g_app.settings.font_id)) {
        strncpy(g_app.settings.font_id, "system", sizeof(g_app.settings.font_id) - 1);
    }
    app_apply_font();
}

/* 三轴参数同步进 UI 上下文，启动与改设置都走 */
void app_sync_layout_axes(void) {
    float hmul = g_app.settings.hidpi_scale;
    if (!(hmul >= 0.5f)) hmul = 1.0f;   /* 顺带拦住 NaN 与 0 */
    if (hmul > 4.0f) hmul = 4.0f;
    hmul*=2.0f;
    g_app.ui.hidpi_mul  = hmul;
    g_app.ui.user_size  = (g_app.settings.ui_scale   > 0.0f ? g_app.settings.ui_scale   : 1.0f) * hmul;
    g_app.ui.user_scale = (g_app.settings.text_scale > 0.0f ? g_app.settings.text_scale : 1.0f) * hmul;
    g_app.ui.safe_x_pct = g_app.settings.safe_x;
    g_app.ui.safe_y_pct = g_app.settings.safe_y;
    g_app.ui.reduce_motion = g_app.settings.reduce_motion;
}

/* 该按键事件是否命中此动作（含修饰键要求） */
bool app_action_match(int act, unsigned int vk) {
    if (act < 0 || act >= ACT_COUNT) return false;
    const QlBind *b = &g_app.settings.binds[act];
    if (b->key == 0) return false;
    if (qlcg_norm_key((uint16_t)vk) != qlcg_norm_key(b->key)) return false;
    if ((b->mods & QL_MOD_CTRL)  && !qlcg_platform_key_down(QL_KEY_CONTROL)) return false;
    if ((b->mods & QL_MOD_SHIFT) && !qlcg_platform_key_down(QL_KEY_SHIFT)) return false;
    if ((b->mods & QL_MOD_ALT)   && !qlcg_platform_key_down(QL_KEY_MENU)) return false;
    return true;
}

/* 视角增量：套用反转设置并钳制俯仰 */
void app_look_apply(float d_yaw, float d_pitch, float limit) {
    float sx = g_app.settings.invert_look_x ? -1.0f : 1.0f;
    float sy = g_app.settings.invert_look_y ? -1.0f : 1.0f;
    if (limit <= 0.0f) limit = 1.48f;
    g_app.terrain.yaw   += d_yaw * sx;
    g_app.terrain.pitch  = qlcg_clampf(g_app.terrain.pitch + d_pitch * sy, -limit, limit);
}

/* 触屏虚拟动作：与键盘并列，任一来源按下即生效 */
static bool s_virtual[ACT_COUNT];

void app_action_set_virtual(int act, bool on) {
    if (act < 0 || act >= ACT_COUNT) return;
    s_virtual[act] = on;
}

bool app_action_virtual(int act) {
    if (act < 0 || act >= ACT_COUNT) return false;
    return s_virtual[act];
}

/* 是否有文本框正在接收键盘：字段优先，其次聊天条 */
bool app_text_input_active(void) {
    return g_app.ui.input_id != 0 || g_app.chat_open;
}

/* 键盘状态表：文本输入消费的按键不入表 */
static bool s_key_down[256];

void app_key_track(unsigned int vk, bool down) {
    if (vk < 256u) s_key_down[vk] = down;
    if (down && vk >= 0x20u) {
        QLCG_DEV_INPUT_LOG("[in] track %u\n", vk);
    }
}

void app_key_track_clear(void) {
    memset(s_key_down, 0, sizeof(s_key_down));
}

bool app_key_down(unsigned int vk) {
    return vk < 256u && s_key_down[vk];
}

/* 放弃文本键盘占用：字段与聊天一起放掉 */
void app_text_focus_clear(void) {
    UiContext *ui = &g_app.ui;
    ui->input_id       = 0;
    ui->input_dirty    = false;
    ui->input_commit   = false;
    ui->input_utf16_hi = 0;
    ui->edit_field.scroll   = 0.0f;
    ui->edit_chat.scroll    = 0.0f;
    ui->edit_field.dragging = false;
    ui->edit_chat.dragging  = false;
    g_app.chat_open = false;
    QLCG_DEV_INPUT_LOG("[in] focus clear\n");
}

/* 持续按住判定，供移动/跳跃等逐帧查询 */
bool app_action_down(int act) {
    if (act < 0 || act >= ACT_COUNT) return false;
    if (s_virtual[act]) return true;
    const QlBind *b = &g_app.settings.binds[act];
    if (b->key == 0) return false;
    if ((b->mods & QL_MOD_CTRL)  && !qlcg_platform_key_down(QL_KEY_CONTROL)) return false;
    if ((b->mods & QL_MOD_SHIFT) && !qlcg_platform_key_down(QL_KEY_SHIFT)) return false;
    if ((b->mods & QL_MOD_ALT)   && !qlcg_platform_key_down(QL_KEY_MENU)) return false;
    return app_key_down(b->key);
}

/* 设置页上下文：三套差异由这三个函数决定 */
bool app_world_active(void) {
    return g_app.settings_back_to_game;
}

PlayMode app_current_play_mode(void) {
    if (!app_world_active() || !g_app.world) return PLAY_MODE_LEISURE;
    return g_app.world->opt.play_mode;
}

bool app_cheats_allowed(void) {
    return app_current_play_mode() == PLAY_MODE_LEISURE || g_app.cheats_unlocked;
}

void app_profile_height_range(int *min_cm, int *max_cm) {
    if (app_cheats_allowed()) {
        if (min_cm) *min_cm = 8;
        if (max_cm) *max_cm = 1024;
    } else {
        if (min_cm) *min_cm = 64;
        if (max_cm) *max_cm = 256;
    }
}

/* 界面主色来源：勾选时跟档案，否则用界面独立色 */
void app_apply_ui_palette(void) {
    const GameSettings *s = &g_app.settings;
    uint32_t mc = s->profile_palette_to_ui ? s->profile.main_color : s->interface_main_color;
    uint32_t tc = s->profile_palette_to_ui ? s->profile.trim_color : s->interface_trim_color;
    ui_set_palette(&g_app.ui, mc, tc);
}

/* 山川按区块本地时刻切昼夜，其余交回系统 */
void app_sync_hud_daynight(void) {
    int ov = -1;
    if (g_app.screen == SCREEN_IN_GAME && g_app.settings.theme == THEME_AUTO &&
        g_app.view_mode == VIEW_MODE_TERRAIN) {
        int hour = g_app.snap.gregorian.hour;
        ov = (hour >= QLCG_DAY_HOUR_START && hour < QLCG_DAY_HOUR_END) ? 0 : 1;
    }
    ui_set_night_override(&g_app.ui, ov);
}

/* 存档列表重扫 */
void app_refresh_saves(void) {
    g_app.save_count = qlcg_saves_scan(g_app.saves, MAX_SAVE_SLOTS);
}

/* 当前会话文件，无则 NULL */
static const SaveFileInfo *current_save_file(void) {
    for (int i = 0; i < g_app.save_count; i++)
        if (g_app.saves[i].valid && strcmp(g_app.saves[i].id, g_app.current_save_id) == 0)
            return &g_app.saves[i];
    return NULL;
}

/* 手动槽默认：空槽优先，满则最早 */
static int manual_default_slot(const SaveFileInfo *f) {
    if (!f) return SAVE_SLOT_MANUAL_1;
    for (int k = SAVE_SLOT_MANUAL_1; k <= SAVE_SLOT_MANUAL_3; k++)
        if (!f->slots[k].present) return k;
    int best = SAVE_SLOT_MANUAL_1;
    for (int k = SAVE_SLOT_MANUAL_2; k <= SAVE_SLOT_MANUAL_3; k++)
        if (strcmp(f->slots[k].saved_at_str, f->slots[best].saved_at_str) < 0) best = k;
    return best;
}

int app_manual_slot_default(void) {
    return manual_default_slot(current_save_file());
}

/* 载入/新建后回到默认槽 */
void app_manual_slot_reset(void) {
    g_app.manual_slot_user = false;
    g_app.manual_slot = app_manual_slot_default();
    g_app.manual_slots_open = false;
}

/* 存盘位置：同区块用局部坐标，否则取区块中心地面 */
static void app_save_position(float *px, float *py, float *pz) {
    bool same_chunk = g_app.region &&
        g_app.region->center_cx == g_app.player_cx &&
        g_app.region->center_cz == g_app.player_cz;
    if (same_chunk) {
        *px = g_app.region->center_x + g_app.terrain.x;
        *pz = g_app.region->center_z + g_app.terrain.z;
        *py = g_app.terrain.y;
        return;
    }
    float wx = ((float)g_app.player_cx + 0.5f) * (float)CHUNK_SIZE;
    float wz = ((float)g_app.player_cz + 0.5f) * (float)CHUNK_SIZE;
    float h = 0.0f, wl = -1e9f;
    if (g_app.world)
        qlcg_sample_surface_point(g_app.world, wx, wz, &h, &wl,
                                  NULL, NULL, NULL, NULL, NULL, NULL);
    *px = wx;
    *pz = wz;
    *py = (wl > h ? wl : h) + 1.7f;
}

/* 统一保存入口：成功清零自动存档计时 */
static bool save_current(int slot_kind, const char *slot_name, bool notify) {
    if (!g_app.world) return false;
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    app_save_position(&px, &py, &pz);
    char msg[256];
    char id[64] = { 0 };
    int used = -1;
    bool ok = qlcg_saves_write_slot_named(g_app.world, &g_app.clock, &g_app.atlas.display,
                                          px, py, pz, g_app.terrain.yaw, g_app.terrain.pitch,
                                          g_app.terrain.flying, g_app.world_menu_auto_pause,
                                          g_app.current_save_id, g_app.save_name, slot_kind, slot_name,
                                          id, sizeof(id), &used, msg, sizeof(msg));
    if (ok) {
        ui_u8_copy(g_app.current_save_id, sizeof(g_app.current_save_id), id);
        g_app.save_idle_s = 0.0f;
        app_refresh_saves();
        if (notify) app_push_toast(msg);
    } else if (notify) {
        app_push_toast_k(UI_TOAST_ERROR, NULL, msg);
    }
    (void)used;
    return ok;
}

bool app_save_current(int slot_kind, bool notify) {
    return save_current(slot_kind, NULL, notify);
}

bool app_save_manual_slot(int slot, const char *name) {
    if (slot < SAVE_SLOT_MANUAL_1 || slot > SAVE_SLOT_MANUAL_3) return false;
    if (!save_current(slot, name, true)) return false;
    g_app.manual_slot = slot;
    g_app.manual_slot_user = true;
    return true;
}

bool app_rename_manual_slot(int slot, const char *name) {
    const SaveFileInfo *file = current_save_file();
    if (!file) return false;
    char msg[320];
    if (!qlcg_saves_rename_slot(file->file_path, slot, name, msg, sizeof(msg))) {
        app_push_toast_k(UI_TOAST_ERROR, NULL, msg[0] ? msg : tr("saves.renameFailed"));
        return false;
    }
    app_refresh_saves();
    app_push_toast_k(UI_TOAST_SUCCESS, NULL, msg);
    return true;
}

/* 快速存档：Ctrl+P 与绑定表共用 */
void app_quick_save(void) {
    if (!g_app.world || g_app.screen != SCREEN_IN_GAME) return;
    if (g_app.settings_in_drawer || settings_confirm_active()) return;
    if (app_text_input_active()) return;
    app_save_current(SAVE_SLOT_QUICK, true);
}

/* 自动存档：距上次读档/存档满 15 分钟 */
void app_save_tick(float dt) {
    if (!g_app.world || g_app.screen != SCREEN_IN_GAME) return;
    float every = QLCG_AUTOSAVE_SECONDS;
    QLCG_DEV_ONLY(every = qlcg_dev_autosave_every(every);)
    g_app.save_idle_s += dt;
    if (g_app.save_idle_s < every) return;
    if (app_text_input_active() || g_app.settings_in_drawer || settings_confirm_active()) return;
    if (app_save_current(SAVE_SLOT_AUTO, false)) {
        g_app.auto_save_flash = QLCG_SAVE_FLASH_SECONDS;
    } else {
        g_app.save_idle_s = every - QLCG_AUTOSAVE_RETRY;
        app_push_toast_k(UI_TOAST_WARNING, NULL, tr("saves.err.auto"));
    }
}

const SaveSlotMeta *app_current_save_slot(int slot) {
    if (slot < 0 || slot >= QLCG_SAVE_SLOT_COUNT) return NULL;
    const SaveFileInfo *f = current_save_file();
    if (!f || !f->slots[slot].present) return NULL;
    return &f->slots[slot];
}

/* 槽位显示名 */
void app_slot_kind_label(int slot, char *buf, size_t cap) {
    if (!buf || cap == 0) return;
    switch (slot) {
        case SAVE_SLOT_QUICK: snprintf(buf, cap, "%s", tr("saves.slotQuick")); break;
        case SAVE_SLOT_AUTO:  snprintf(buf, cap, "%s", tr("saves.slotAuto")); break;
        case SAVE_SLOT_MANUAL_1:
        case SAVE_SLOT_MANUAL_2:
        case SAVE_SLOT_MANUAL_3:
            snprintf(buf, cap, tr("saves.slotManualFmt"), slot);
            break;
        default: buf[0] = '\0'; break;
    }
}

/* 导出存档文件到 userdata/exports/ */
bool app_export_save_file(const SaveFileInfo *file) {
    if (!file || !file->file_path[0]) return false;
#ifdef __ANDROID__
    return qlcg_android_export_request(file->file_path);
#else
    qlcg_platform_mkdir("userdata");
    qlcg_platform_mkdir("userdata/exports");
    const char *name = file->file_path;
    for (const char *p = name; *p; ++p) if (*p == '/' || *p == '\\') name = p + 1;
    char dst[1024];
    bool ok = false;
    for (int n = 0; n < 100; ++n) {
        if (!n) snprintf(dst, sizeof(dst), "userdata/exports/%s", name);
        else snprintf(dst, sizeof(dst), "userdata/exports/%d_%s", n, name);
        if (qlcg_platform_exists(dst)) continue;
        ok = qlcg_platform_copy(file->file_path, dst);
        break;
    }
    app_push_toast_k(ok ? UI_TOAST_SUCCESS : UI_TOAST_ERROR, NULL, tr(ok ? "export.done" : "export.failed"));
    return ok;
#endif
}

bool app_export_current_save(void) {
    const SaveFileInfo *f = current_save_file();
    return f ? app_export_save_file(f) : false;
}

/* 改名只动档内名，文件名不变 */
bool app_save_rename(const SaveFileInfo *file, const char *new_name) {
    if (!file || !new_name || !*new_name) {
        app_push_toast_k(UI_TOAST_WARNING, NULL, tr("saves.nameRequired"));
        return false;
    }
    char path[260];
    char id[64];
    ui_u8_copy(path, sizeof(path), file->file_path);
    ui_u8_copy(id, sizeof(id), file->id);
    char msg[320];
    if (!qlcg_saves_rename(path, new_name, msg, sizeof(msg))) {
        app_push_toast_k(UI_TOAST_ERROR, NULL, msg[0] ? msg : tr("saves.renameFailed"));
        return false;
    }
    if (strcmp(id, g_app.current_save_id) == 0)
        ui_u8_copy(g_app.save_name, sizeof(g_app.save_name), new_name);
    app_refresh_saves();
    app_push_toast_k(UI_TOAST_SUCCESS, NULL, msg);
    return true;
}

/* 所选槽复制为悠游副本 */
bool app_save_copy_leisure(const SaveFileInfo *file, int slot) {
    if (!file) return false;
    char msg[320];
    if (!qlcg_saves_copy_to_leisure(file, slot, msg, sizeof(msg))) {
        app_push_toast_k(UI_TOAST_ERROR, NULL, msg[0] ? msg : tr("saves.err.convert"));
        return false;
    }
    g_app.save_filter_mode = PLAY_MODE_LEISURE;
    g_app.save_page        = 0;
    app_refresh_saves();
    app_push_toast_k(UI_TOAST_SUCCESS, NULL, msg);
    return true;
}

/* 开发工具 */
void app_build_player_input(PlayerInput *in) {
    static bool prev_jump = false;
    in->forward = app_action_down(ACT_FORWARD);
    in->back    = app_action_down(ACT_BACK);
    in->left    = app_action_down(ACT_LEFT);
    in->right   = app_action_down(ACT_RIGHT);
    in->jump    = app_action_down(ACT_JUMP);
    in->sprint  = app_action_down(ACT_SPRINT);
    in->crouch  = app_action_down(ACT_CROUCH);
    in->inspect_plants = g_app.show_debug_f12;
    in->jump_pressed = in->jump && !prev_jump;
    prev_jump = in->jump;
}
