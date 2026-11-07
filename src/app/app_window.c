/* Win32 窗口与 OpenGL 上下文 */
#include "app/app_state.h"
#include "app/app_icon.h"
#include "core/dev_tools.h"
#include "systems/world/climate_season.h"
#include "ui/pages/game/view_atlas_layers.h"
#include <imm.h>

/* 客户区像素 → UI 逻辑坐标的缩放比 */
static inline float ui_scale_x(void) {
    return (g_app.win_w > 0) ? (float)g_app.ui.win_w / (float)g_app.win_w : 1.0f;
}
static inline float ui_scale_y(void) {
    return (g_app.win_h > 0) ? (float)g_app.ui.win_h / (float)g_app.win_h : 1.0f;
}

/* 触屏兼容鼠标消息只交给普通界面 */
static bool mouse_from_touch(void) {
    ULONG_PTR extra = (ULONG_PTR)GetMessageExtraInfo();
    return (extra & 0xFFFFFF80u) == 0xFF515780u;
}

/* 当前编辑目标：字段优先，其次聊天条，无则NULL */
static UiEditField *text_target(void) {
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

/* 插入一个 UTF-16 码元；代理对暂存高位 */
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

    UiEditField *f = text_target();
    if (f && ui_edit_insert_cp(f, cp)) ui->input_dirty = true;
}

/* 编辑键：方向 Home End Del + 剪贴板 */
static bool text_key_edit(unsigned int vk) {
    UiEditField *f = text_target();
    if (!f) return false;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool changed = false;
    bool handled = true;

    switch (vk) {
        case VK_LEFT:   ui_edit_move(f, -1, shift, ctrl); break;
        case VK_RIGHT:  ui_edit_move(f, +1, shift, ctrl); break;
        case VK_HOME:   ui_edit_home(f, shift);           break;
        case VK_END:    ui_edit_end(f, shift);            break;
        case VK_DELETE: changed = ui_edit_delete_forward(f); break;
        case VK_INSERT:
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

/* 把输入法组合窗与候选窗移到光标处（物理客户区坐标） */
static void ime_follow_caret(HWND hwnd) {
    if (!g_app.ui.ime_caret_valid) return;
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return;
    float sx = ui_scale_x(), sy = ui_scale_y();
    if (sx <= 0.0f) sx = 1.0f;
    if (sy <= 0.0f) sy = 1.0f;
    int cx = (int)(g_app.ui.ime_caret_x / sx);
    int cy = (int)(g_app.ui.ime_caret_y / sy);
    int ch = (int)(g_app.ui.ime_caret_h / sy);
    if (ch < 8) ch = 8;

    COMPOSITIONFORM cf;
    memset(&cf, 0, sizeof(cf));
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = cx;
    cf.ptCurrentPos.y = cy + ch;

    CANDIDATEFORM cand;
    memset(&cand, 0, sizeof(cand));
    cand.dwIndex = 0;
    cand.dwStyle = CFS_CANDIDATEPOS;
    cand.ptCurrentPos.x = cx;
    cand.ptCurrentPos.y = cy + ch + 2;

    ImmSetCompositionWindow(himc, &cf);
    ImmSetCandidateWindow(himc, &cand);
    ImmReleaseContext(hwnd, himc);
}

static bool  s_cursor_hidden = false;
static POINT s_cursor_saved;              /* 锁定前的屏幕坐标 */
static bool  s_cursor_saved_ok = false;

void app_set_mouse_lock(bool on) {
    if (!g_app.hwnd) return;
    if (on == g_app.terrain.mouse_captured) return;
    g_app.terrain.mouse_captured = on;
    if (on) {
        POINT p;
        if (GetCursorPos(&p)) { s_cursor_saved = p; s_cursor_saved_ok = true; }
        RECT r;
        GetClientRect(g_app.hwnd, &r);
        POINT c = { (r.right - r.left) / 2, (r.bottom - r.top) / 2 };
        ClientToScreen(g_app.hwnd, &c);
        SetCursorPos(c.x, c.y);
        if (!s_cursor_hidden) { ShowCursor(FALSE); s_cursor_hidden = true; }
        SetCapture(g_app.hwnd);
    } else {
        if (s_cursor_hidden) { ShowCursor(TRUE); s_cursor_hidden = false; }
        if (GetCapture() == g_app.hwnd) ReleaseCapture();   /* 别抢舆图拖拽的捕获 */
        if (s_cursor_saved_ok) {
            SetCursorPos(s_cursor_saved.x, s_cursor_saved.y);
            s_cursor_saved_ok = false;
        }
    }
}

/* 该锁鼠标：山川、未暂停、无设置抽屉、非触屏 */
bool app_want_mouse_lock(void) {
    return g_app.screen == SCREEN_IN_GAME
        && g_app.view_mode == VIEW_MODE_TERRAIN
        && !g_app.esc_paused
        && !g_app.settings_in_drawer
        && !g_app.touch.enabled;
}

LRESULT CALLBACK app_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CLOSE:
            g_app.running = false;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            g_app.win_w = LOWORD(lparam);
            g_app.win_h = HIWORD(lparam);
            glViewport(0, 0, g_app.win_w, g_app.win_h);
            return 0;
        case WM_TOUCH: {
            /* 百分之一像素转 UI 坐标 */
            UINT n = LOWORD(wparam);
            TOUCHINPUT ti[10];
            if (n > 10) n = 10;
            if (n > 0 && GetTouchInputInfo((HANDLE)lparam, n, ti, sizeof(TOUCHINPUT))) {
                float sx = (g_app.win_w > 0) ? (float)g_app.ui.win_w / (float)g_app.win_w : 1.0f;
                float sy = (g_app.win_h > 0) ? (float)g_app.ui.win_h / (float)g_app.win_h : 1.0f;
                POINT origin = {0, 0};
                ClientToScreen(hwnd, &origin);
                for (UINT i = 0; i < n; i++) {
                    float ux = (float)((double)ti[i].x * 0.01 - origin.x) * sx;
                    float uy = (float)((double)ti[i].y * 0.01 - origin.y) * sy;
                    int   id = (int)ti[i].dwID;
                    if (ti[i].dwFlags & TOUCHEVENTF_PALM) {
                        ui_touch_up(&g_app.touch, &g_app.ui, id);
                        continue;
                    }
                    /* 记录通用触摸点，供通知卡片悬停与划动 */
                    if (ti[i].dwFlags & TOUCHEVENTF_UP) {
                        g_app.ui.touch_active = false;
                    } else {
                        g_app.ui.touch_active = true;
                        g_app.ui.touch_x = ux;
                        g_app.ui.touch_y = uy;
                    }
                    if (ti[i].dwFlags & TOUCHEVENTF_DOWN) {
                        int saved_x = g_app.ui.mouse_x, saved_y = g_app.ui.mouse_y;
                        g_app.ui.mouse_x = (int)ux;
                        g_app.ui.mouse_y = (int)uy;
                        g_app.ui.toast_blocked = ui_toast_over_ui(&g_app.ui, &g_app.toasts);
                        if (!game_scene_over_ui(&g_app.ui))
                            ui_touch_down(&g_app.touch, &g_app.ui, id, ux, uy);
                        g_app.ui.mouse_x = saved_x;
                        g_app.ui.mouse_y = saved_y;
                    }
                    else if (ti[i].dwFlags & TOUCHEVENTF_UP) {
                        if (id == g_app.touch.look_pointer)
                            ui_touch_move(&g_app.touch, &g_app.ui, id, ux, uy);
                        ui_touch_up(&g_app.touch, &g_app.ui, id);
                    }
                    else                                       ui_touch_move(&g_app.touch, &g_app.ui, id, ux, uy);
                }
                CloseTouchInputHandle((HANDLE)lparam);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lparam);
            int my = GET_Y_LPARAM(lparam);
            int dx = mx - g_app.ui.mouse_x;
            int dy = my - g_app.ui.mouse_y;
            g_app.ui.mouse_x = mx;
            g_app.ui.mouse_y = my;
            g_app.ui.toast_blocked = ui_toast_over_ui(&g_app.ui, &g_app.toasts);
            app_river_craft_pointer(false,g_app.ui.mouse_down);

            if (g_app.touch.enabled && !mouse_from_touch()) {
                ui_touch_move(&g_app.touch, &g_app.ui, UI_TOUCH_MOUSE_POINTER,
                              (float)mx * ui_scale_x(), (float)my * ui_scale_y());
            }

            /* 指针锁定：偏移即增量，光标钉回中心 */
            if (g_app.terrain.mouse_captured && g_app.view_mode == VIEW_MODE_TERRAIN) {
                int cx = g_app.win_w / 2;
                int cy = g_app.win_h / 2;
                int ldx = mx - cx, ldy = my - cy;
                if (ldx == 0 && ldy == 0) return 0;
                app_look_apply(-(float)ldx * 0.0022f, -(float)ldy * 0.0022f, 1.5608f);
                POINT cc = { cx, cy };
                ClientToScreen(g_app.hwnd, &cc);
                SetCursorPos(cc.x, cc.y);
                return 0;
            }

            if (g_app.atlas.dragging && (g_app.esc_paused || g_app.settings_in_drawer ||
                g_app.ui.select_id || g_app.chat_open)) g_app.atlas.dragging = false;
            if (g_app.atlas.dragging && g_app.world) {
                AtlasRenderer *ar = &g_app.atlas;
                qlcg_atlas_drag(ar, g_app.world, mx - ar->last_vp_x, my - ar->last_vp_y, dx, dy);
            } else if (g_app.screen == SCREEN_IN_GAME && g_app.view_mode == VIEW_MODE_TERRAIN &&
                       g_app.ui.right_down && !game_scene_over_ui(&g_app.ui)) {
                app_look_apply((float)dx * 0.0045f, -(float)dy * 0.0045f, 1.48f);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            g_app.ui.mouse_x = GET_X_LPARAM(lparam);
            g_app.ui.mouse_y = GET_Y_LPARAM(lparam);
            QLCG_DEV_INPUT_LOG("[in] down %d,%d screen %d,%d\n",
                       g_app.ui.mouse_x, g_app.ui.mouse_y,
                       (int)g_app.win_w, (int)g_app.win_h);
            g_app.ui.toast_blocked = ui_toast_over_ui(&g_app.ui, &g_app.toasts);
            /* 兼容鼠标只探测命中，不更新触点 */
            bool touch_mouse = mouse_from_touch();
            bool touch_taken = false;
            if (g_app.touch.enabled && !game_scene_over_ui(&g_app.ui)) {
                POINT pt;
                pt.x = GET_X_LPARAM(lparam);
                pt.y = GET_Y_LPARAM(lparam);
                UiTouch probe;
                UiTouch *target = &g_app.touch;
                if (touch_mouse) {
                    probe = g_app.touch;
                    target = &probe;
                }
                touch_taken = ui_touch_down(target, &g_app.ui, UI_TOUCH_MOUSE_POINTER,
                                            (float)pt.x * ui_scale_x(), (float)pt.y * ui_scale_y());
            }
            g_app.ui.mouse_down    = true;
            g_app.ui.mouse_clicked = !touch_taken;
            if(!touch_taken) app_river_craft_pointer(true,true);
            if(g_app.river_craft.drawing) SetCapture(hwnd);
            if (touch_taken && !touch_mouse) SetCapture(hwnd);
            if (!touch_taken && g_app.screen == SCREEN_IN_GAME && g_app.view_mode == VIEW_MODE_ATLAS &&
                (!g_app.river_craft.active||g_app.river_craft.tool==0) &&
                g_app.atlas.eng_tool == ENG_TOOL_NONE && !game_atlas_over_ui(&g_app.ui)) {
                g_app.atlas.dragging = true;
            }
            if (g_app.atlas.dragging || (g_app.screen == SCREEN_IN_GAME &&
                g_app.view_mode == VIEW_MODE_ATLAS && g_app.atlas.layer_panel_open)) SetCapture(hwnd);
            return 0;
        }
        case WM_LBUTTONUP:
            g_app.ui.mouse_x = GET_X_LPARAM(lparam);
            g_app.ui.mouse_y = GET_Y_LPARAM(lparam);
            app_river_craft_pointer(false,false);
            if (g_app.touch.enabled && !mouse_from_touch()) {
                ui_touch_up(&g_app.touch, &g_app.ui, UI_TOUCH_MOUSE_POINTER);
            }
            g_app.ui.mouse_down     = false;
            g_app.atlas.dragging    = false;
            if (!g_app.terrain.mouse_captured && GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED:
            if (!g_app.ui.mouse_down) return 0;
        case WM_KILLFOCUS:
            app_key_track_clear();   /* 丢焦点时别把按键卡住 */
            ui_touch_reset(&g_app.touch);
            for (int act = 0; act < ACT_COUNT; act++) app_action_set_virtual(act, false);
            g_app.atlas.dragging = false;
            g_app.atlas.layer_dragging = false;
            g_app.ui.mouse_down = false;
            g_app.river_craft.drawing = false;
            g_app.ui.drag_slider_id = -1;
            return 0;
        case WM_RBUTTONDOWN:
            g_app.ui.mouse_x = GET_X_LPARAM(lparam);
            g_app.ui.mouse_y = GET_Y_LPARAM(lparam);
            g_app.ui.toast_blocked = ui_toast_over_ui(&g_app.ui, &g_app.toasts);
            g_app.ui.right_down    = !game_scene_over_ui(&g_app.ui);
            g_app.ui.right_clicked = true;
            return 0;
        case WM_RBUTTONUP:
            g_app.ui.right_down = false;
            return 0;
        case WM_MOUSEWHEEL:
            g_app.ui.wheel_delta += GET_WHEEL_DELTA_WPARAM(wparam);
            return 0;
        case WM_CHAR: {
            UiEditField *f = text_target();
            if (f) {
                if (wparam == VK_BACK) {
                    if (ui_edit_backspace(f)) g_app.ui.input_dirty = true;
                } else if (g_app.ui.input_id != 0 && g_app.ui.input_mode == UI_INPUT_NUM) {
                    if ((wparam >= '0' && wparam <= '9') || wparam == '.') {
                        if (wparam != '.' || strchr(g_app.ui.input_buf, '.') == NULL) {
                            if (ui_edit_insert_cp(f, (uint32_t)wparam)) g_app.ui.input_dirty = true;
                        }
                    }
                } else {
                    app_input_push_utf16(&g_app.ui, (uint32_t)wparam);
                }
                if (f->ed) {
                    QLCG_DEV_INPUT_LOG("[in] char %u caret %u len %u\n",
                           (unsigned)wparam, f->ed->caret,
                           (unsigned)strlen(f->buf));
                }
            }
            return 0;
        }
        case WM_PASTE:
            if (text_target() && ui_edit_paste(text_target())) g_app.ui.input_dirty = true;
            return 0;
        case WM_COPY:
            if (text_target()) ui_edit_copy(text_target());
            return 0;
        case WM_CUT:
            if (text_target() && ui_edit_cut(text_target())) g_app.ui.input_dirty = true;
            return 0;
        case WM_CLEAR:
            if (text_target() && ui_edit_delete_forward(text_target())) g_app.ui.input_dirty = true;
            return 0;
        case WM_IME_STARTCOMPOSITION:
        case WM_IME_COMPOSITION:
            /* 候选窗跟随光标；上屏仍转 WM_CHAR */
            ime_follow_caret(hwnd);
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        case WM_IME_ENDCOMPOSITION:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        case WM_KEYUP:
            app_key_track((unsigned int)wparam, false);
            return 0;
        case WM_KEYDOWN: {
            QLCG_DEV_INPUT_LOG("[in] kd vk %u screen %d input %u chat %d\n",
                       (unsigned)wparam, (int)g_app.screen,
                       g_app.ui.input_id, (int)g_app.chat_open);
            /* 1) 回车：字段提交 / 聊天发送，绝不被吞 */
            if (wparam == VK_RETURN) {
                if (g_app.ui.input_id != 0) {
                    g_app.ui.input_commit = true;
                } else if (g_app.screen == SCREEN_IN_GAME && g_app.chat_open) {
                    app_chat_send();
                }
                return 0;
            }
            /* 2) Esc：先放掉文本键盘占用，再关当前叠层 */
            if (wparam == VK_ESCAPE) {
                if (app_text_input_active()) {
                    app_text_focus_clear();
                    if (g_app.settings_in_drawer) {
                        g_app.settings_in_drawer    = false;
                        g_app.settings_back_to_game = false;
                        g_app.esc_paused            = true;
                    } else if (g_app.esc_paused) {
                        g_app.esc_paused = false;
                    }
                    return 0;
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
                return 0;
            }
            /* 3)编辑器键：方向 Home End Del 与剪贴板 */
            if (text_key_edit(wparam)) return 0;
            /* 4)文本占用键盘：其余键不入玩法表 */
            if (app_text_input_active()) return 0;
            /* 5)键位捕获：吃掉本次，Esc取消，修饰键略 */
            if (g_app.ui.capture_action >= 0) {
                if (wparam == VK_PROCESSKEY || wparam == VK_PACKET) return 0;
                int act = g_app.ui.capture_action;
                bool  modifier_only = (wparam == VK_SHIFT || wparam == VK_CONTROL || wparam == VK_MENU ||
                                       wparam == VK_LSHIFT || wparam == VK_RSHIFT ||
                                       wparam == VK_LCONTROL || wparam == VK_RCONTROL ||
                                       wparam == VK_LMENU || wparam == VK_RMENU);
                if (wparam != VK_ESCAPE && !modifier_only) {
                    uint8_t mods = 0;
                    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= QL_MOD_CTRL;
                    if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= QL_MOD_SHIFT;
                    if (GetKeyState(VK_MENU)    & 0x8000) mods |= QL_MOD_ALT;
                    int conflict = qlcg_bind_conflict(g_app.settings.binds, act, (uint16_t)wparam, mods);
                    if (conflict >= 0) {
                        qlcg_bind_swap_key(g_app.settings.binds, act, conflict);
                        app_push_toast_k(UI_TOAST_WARNING, NULL, tr("bind.conflict"));
                    } else {
                        g_app.settings.binds[act].key  = (uint16_t)wparam;
                        g_app.settings.binds[act].mods = mods;
                    }
                    qlcg_save_settings_disk(&g_app.settings);
                }
                g_app.ui.capture_action = -1;
                return 0;
            }
            /* 6)玩法：仅未被文本消费的键进按键表 */
            app_key_track((unsigned int)wparam, true);
            if (app_action_match(ACT_DEBUG, wparam)) {
                g_app.show_debug_f12 = !g_app.show_debug_f12;
                return 0;
            }
            /* 斜杠键展开菜单并预填 '/'，对齐 base */
            if (wparam == VK_OEM_2 && g_app.screen == SCREEN_IN_GAME) {
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
                return 0;
            }
            if (app_action_match(ACT_VIEW_TOGGLE, wparam) && g_app.screen == SCREEN_IN_GAME) {
                app_toggle_world_view();
                return 0;
            }
            if (wparam == 'L' && g_app.screen == SCREEN_IN_GAME && g_app.view_mode == VIEW_MODE_ATLAS &&
                g_app.world && g_app.world->opt.play_mode == PLAY_MODE_LEISURE) {
                g_app.atlas.layer_panel_open = !g_app.atlas.layer_panel_open;
                return 0;
            }
            /* 双击跳跃切飞行，见 terrain_player.c */
            if (app_action_match(ACT_UNDO, wparam) && g_app.screen == SCREEN_IN_GAME) {
                int season = g_app.world->plant_season_forced ? g_app.snap.season_index
                           : qlcg_climate_season_index(g_app.world, g_app.region->site_cell, &g_app.snap);
                if (qlcg_eng_undo(g_app.region, g_app.world, season, &g_app.settings)) {
                    g_app.region->climate_season = season;
                    qlcg_terrain_set_season(&g_app.terrain, season);
                    qlcg_terrain_upload_region(&g_app.terrain, g_app.region, &g_app.settings);
                    qlcg_world_rebuild_atlas_fields(g_app.world);
                    app_push_toast_k(UI_TOAST_SUCCESS, NULL, tr("eng.undo"));
                }
                return 0;
            }
            if (app_action_match(ACT_QUICKSAVE, wparam) && !(lparam & 0x40000000)) {
                app_quick_save();
                return 0;
            }
            if (wparam >= '1' && wparam <= '8' && g_app.screen == SCREEN_IN_GAME) {
                EngToolType t = (EngToolType)(wparam - '1');
                g_app.atlas.eng_tool   = t;
                g_app.terrain.eng_tool = t;
                return 0;
            }
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* 高分屏：逻辑尺寸＝物理尺寸/mul */
void app_apply_hidpi_scale(void) {
    app_sync_layout_axes();
}

/* 创建 Win32 窗口与 OpenGL 3.3 上下文 */
bool init_win32_gl(HINSTANCE hinst, int w, int h) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = app_wnd_proc;
    wc.hInstance     = hinst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm       = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(IDI_APP_ICON),
                                         IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                         GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = L"QianlinChaguanBaseC";
    RegisterClassExW(&wc);

    RECT rc = {0, 0, w, h};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    g_app.hwnd = CreateWindowExW(0, wc.lpszClassName, L"茜林茶馆 (Qianlin Chaguan - C11 Base)",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 CW_USEDEFAULT, CW_USEDEFAULT,
                                 rc.right - rc.left, rc.bottom - rc.top,
                                 NULL, NULL, hinst, NULL);
    g_app.hdc = GetDC(g_app.hwnd);
    /* 触摸消息优先直达，手掌由应用过滤 */
    RegisterTouchWindow(g_app.hwnd, TWF_FINETOUCH | TWF_WANTPALM);

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(g_app.hdc, &pfd);
    SetPixelFormat(g_app.hdc, pf, &pfd);

    HGLRC tmp_rc = wglCreateContext(g_app.hdc);
    wglMakeCurrent(g_app.hdc, tmp_rc);
    qlcg_gl_load_functions();

    if (wglCreateContextAttribsARB) {
        int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };
        g_app.hglrc = wglCreateContextAttribsARB(g_app.hdc, 0, attribs);
        if (g_app.hglrc) {
            wglMakeCurrent(NULL, NULL);
            wglDeleteContext(tmp_rc);
            wglMakeCurrent(g_app.hdc, g_app.hglrc);
        } else {
            g_app.hglrc = tmp_rc;
        }
    } else {
        g_app.hglrc = tmp_rc;
    }

    if (wglSwapIntervalEXT) wglSwapIntervalEXT(0);
    return true;
}
