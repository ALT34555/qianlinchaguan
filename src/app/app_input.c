#include "app/app_state.h"
#include "core/dev_tools.h"
#include "systems/world/climate_season.h"

UiEditField *app_text_target(void) {
    static UiEditField f;
    UiContext *ui = &g_app.ui;
    if (ui->input_id != 0) {
        size_t cap = (ui->input_max_bytes > 0) ? ui->input_max_bytes + 1
                                               : sizeof(ui->input_buf);
        if (cap > sizeof(ui->input_buf)) cap = sizeof(ui->input_buf);
        f.buf       = ui->input_buf;
        f.cap       = cap;
        f.max_chars = ui->input_max_chars;
        f.ed        = &ui->edit_field;
        return &f;
    }
    if (g_app.chat_open) {
        f.buf       = g_app.chat_input;
        f.cap       = sizeof(g_app.chat_input);
        f.max_chars = QLCG_CHAT_MAX_CHARS;
        f.ed        = &ui->edit_chat;
        return &f;
    }
    return NULL;
}

void app_input_push_utf16(UiContext *ui, uint32_t unit) {
    uint32_t cp = unit;
    if (unit >= 0xD800u && unit <= 0xDBFFu) {   /* 高位代理：等低位 */
        ui->input_utf16_hi = unit;
        return;
    }
    if (unit >= 0xDC00u && unit <= 0xDFFFu) {   /* 低位代理：与高位拼合 */
        if (ui->input_utf16_hi == 0) return;
        cp = 0x10000u + ((ui->input_utf16_hi - 0xD800u) << 10) + (unit - 0xDC00u);
    }
    ui->input_utf16_hi = 0;
    if (cp < 0x20u || cp == 0x7Fu) return;      /* 控制字符不收 */

    UiEditField *f = app_text_target();
    if (f && ui_edit_insert_cp(f, cp)) ui->input_dirty = true;
}

static bool app_text_key_edit(unsigned int vk) {
    UiEditField *f = app_text_target();
    if (!f) return false;
    bool shift = qlcg_platform_key_down(QL_KEY_SHIFT) != 0;
    bool ctrl  = qlcg_platform_key_down(QL_KEY_CONTROL) != 0;
    bool changed = false;
    bool handled = true;

    switch (vk) {
        case QL_KEY_LEFT:   ui_edit_move(f, -1, shift, ctrl); break;
        case QL_KEY_RIGHT:  ui_edit_move(f, +1, shift, ctrl); break;
        case QL_KEY_HOME:   ui_edit_home(f, shift);           break;
        case QL_KEY_END:    ui_edit_end(f, shift);            break;
        case QL_KEY_DELETE: changed = ui_edit_delete_forward(f); break;
        case QL_KEY_INSERT:
            if (ctrl)       ui_edit_copy(f);
            else if (shift) changed = ui_edit_paste(f);
            break;
        case 'A': if (ctrl) { ui_edit_select_all(f); } else handled = false; break;
        case 'C': if (ctrl) { ui_edit_copy(f); }       else handled = false; break;
        case 'X': if (ctrl) { changed = ui_edit_cut(f); }   else handled = false; break;
        case 'V': if (ctrl) { changed = ui_edit_paste(f); } else handled = false; break;
        default: handled = false; break;
    }
    if (!handled) return false;
    if (f->ed) {
        QLCG_DEV_INPUT_LOG("[in] key vk %u caret %u anchor %u len %u\n",
               vk, f->ed->caret, f->ed->anchor, (unsigned)strlen(f->buf));
    }
    if (changed) g_app.ui.input_dirty = true;
    return true;
}

bool app_want_mouse_lock(void) {
    return g_app.screen == SCREEN_IN_GAME
        && g_app.view_mode == VIEW_MODE_TERRAIN
        && !g_app.esc_paused
        && !g_app.settings_in_drawer
        && !g_app.touch.enabled;
}

void app_input_key_down(unsigned key, bool repeat) {
            QLCG_DEV_INPUT_LOG("[in] kd vk %u screen %d input %u chat %d\n",
                       (unsigned)key, (int)g_app.screen,
                       g_app.ui.input_id, (int)g_app.chat_open);
            /* 1) 回车：字段提交 / 聊天发送，绝不被吞 */
            if (key == QL_KEY_RETURN) {
                if (g_app.ui.input_id != 0) {
                    g_app.ui.input_commit = true;
                } else if (g_app.screen == SCREEN_IN_GAME && g_app.chat_open) {
                    app_chat_send();
                }
                return;
            }
            /* 2) Esc：先放掉文本键盘占用，再关当前叠层 */
            if (key == QL_KEY_ESCAPE) {
                if (app_text_input_active()) {
                    app_text_focus_clear();
                    if (g_app.settings_in_drawer) {
                        g_app.settings_in_drawer    = false;
                        g_app.settings_back_to_game = false;
                        g_app.esc_paused            = true;
                    } else if (g_app.esc_paused) {
                        g_app.esc_paused = false;
                    }
                    return;
                }
                if (g_app.ui.select_id) g_app.ui.select_id = 0;
                else if (settings_confirm_active()) settings_confirm_cancel();
                else if (g_app.touch_layout_edit) settings_layout_cancel();
                else if (g_app.settings_in_drawer) {
                    /* 逐级退回：先收起设置抽屉回到菜单，再按一次才关菜单 */
                    g_app.settings_in_drawer    = false;
                    g_app.settings_back_to_game = false;
                    g_app.esc_paused            = true;
                }
                else if (g_app.screen == SCREEN_IN_GAME) g_app.esc_paused = !g_app.esc_paused;
                else g_app.screen = SCREEN_START_HOME;
                return;
            }
            /* 编辑键处理 */
            if (app_text_key_edit(key)) return;
            /* 4)文本占用键盘：其余键不入玩法表 */
            if (app_text_input_active()) return;
            /* 5)键位捕获：吃掉本次，Esc取消，修饰键略 */
            if (g_app.ui.capture_action >= 0) {
                if (key == QL_KEY_PROCESSKEY || key == QL_KEY_PACKET) return;
                int act = g_app.ui.capture_action;
                bool  modifier_only = (key == QL_KEY_SHIFT || key == QL_KEY_CONTROL || key == QL_KEY_MENU ||
                                       key == QL_KEY_LSHIFT || key == QL_KEY_RSHIFT ||
                                       key == QL_KEY_LCONTROL || key == QL_KEY_RCONTROL ||
                                       key == QL_KEY_LMENU || key == QL_KEY_RMENU);
                if (key != QL_KEY_ESCAPE && !modifier_only) {
                    uint8_t mods = 0;
                    if (qlcg_platform_key_down(QL_KEY_CONTROL)) mods |= QL_MOD_CTRL;
                    if (qlcg_platform_key_down(QL_KEY_SHIFT)) mods |= QL_MOD_SHIFT;
                    if (qlcg_platform_key_down(QL_KEY_MENU)) mods |= QL_MOD_ALT;
                    int conflict = qlcg_bind_conflict(g_app.settings.binds, act, (uint16_t)key, mods);
                    if (conflict >= 0) {
                        qlcg_bind_swap_key(g_app.settings.binds, act, conflict);
                        app_push_toast_k(UI_TOAST_WARNING, NULL, tr("bind.conflict"));
                    } else {
                        g_app.settings.binds[act].key  = (uint16_t)key;
                        g_app.settings.binds[act].mods = mods;
                    }
                    qlcg_save_settings_disk(&g_app.settings);
                }
                g_app.ui.capture_action = -1;
                return;
            }
            /* 6)玩法：仅未被文本消费的键进按键表 */
            app_key_track((unsigned int)key, true);
            if (app_action_match(ACT_DEBUG, key)) {
                g_app.show_debug_f12 = !g_app.show_debug_f12;
                return;
            }
            /* 斜杠键展开菜单并预填 '/'，对齐 base */
            if (key == QL_KEY_OEM_2 && g_app.screen == SCREEN_IN_GAME) {
                g_app.ui.input_id = 0;   /* 打字目标切到聊天条 */
                g_app.esc_paused  = true;   /* 输入条只在展开态出现 */
                g_app.chat_open   = true;
                if (g_app.chat_input[0] == '\0') {
                    snprintf(g_app.chat_input, sizeof(g_app.chat_input), "/");
                }
                g_app.ui.edit_chat.caret  = (uint32_t)strlen(g_app.chat_input);
                g_app.ui.edit_chat.anchor = g_app.ui.edit_chat.caret;
                g_app.ui.chat_seen_frame   = g_app.ui.frame_idx;
                app_key_track_clear();
                QLCG_DEV_INPUT_LOG("[in] chat open\n");
                return;
            }
            if (app_action_match(ACT_VIEW_TOGGLE, key) && g_app.screen == SCREEN_IN_GAME) {
                app_toggle_world_view();
                return;
            }
            if (key == 'L' && g_app.screen == SCREEN_IN_GAME && g_app.view_mode == VIEW_MODE_ATLAS &&
                g_app.world && g_app.world->opt.play_mode == PLAY_MODE_LEISURE) {
                g_app.atlas.layer_panel_open = !g_app.atlas.layer_panel_open;
                return;
            }
            /* 双击跳跃由玩家模块处理 */
            if (app_action_match(ACT_UNDO, key) && g_app.screen == SCREEN_IN_GAME) {
                int season = g_app.world->plant_season_forced ? g_app.snap.season_index
                           : qlcg_climate_season_index(g_app.world, g_app.region->site_cell, &g_app.snap);
                if (qlcg_eng_undo(g_app.region, g_app.world, season, &g_app.settings)) {
                    g_app.region->climate_season = season;
                    qlcg_terrain_set_season(&g_app.terrain, season);
                    qlcg_terrain_upload_region(&g_app.terrain, g_app.region, &g_app.settings);
                    qlcg_world_rebuild_atlas_fields(g_app.world);
                    app_push_toast_k(UI_TOAST_SUCCESS, NULL, tr("eng.undo"));
                }
                return;
            }
            if (app_action_match(ACT_QUICKSAVE, key) && !repeat) {
                app_quick_save();
                return;
            }
            if (key >= '1' && key <= '8' && g_app.screen == SCREEN_IN_GAME) {
                EngToolType t = (EngToolType)(key - '1');
                g_app.atlas.eng_tool   = t;
                g_app.terrain.eng_tool = t;
                return;
            }
            return;
}

void app_input_character(uint32_t unit) {
            UiEditField *f = app_text_target();
            if (f) {
                if (unit == QL_KEY_BACK) {
                    if (ui_edit_backspace(f)) g_app.ui.input_dirty = true;
                } else if (g_app.ui.input_id != 0 && g_app.ui.input_mode == UI_INPUT_NUM) {
                    if ((unit >= '0' && unit <= '9') || unit == '.') {
                        if (unit != '.' || strchr(g_app.ui.input_buf, '.') == NULL) {
                            if (ui_edit_insert_cp(f, (uint32_t)unit)) g_app.ui.input_dirty = true;
                        }
                    }
                } else {
                    app_input_push_utf16(&g_app.ui, (uint32_t)unit);
                }
                if (f->ed) {
                    QLCG_DEV_INPUT_LOG("[in] char %u caret %u len %u\n",
                           (unsigned)unit, f->ed->caret,
                           (unsigned)strlen(f->buf));
                }
            }
            return;
}
