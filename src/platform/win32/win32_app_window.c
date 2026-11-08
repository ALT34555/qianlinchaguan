#ifdef _WIN32
#include "platform/win32/native.h"
#include "platform/keys.h"
#include "platform/paths.h"
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


/* 插入一个 UTF-16 码元；代理对暂存高位 */


/* 编辑键：方向 Home End Del + 剪贴板 */


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
        case WM_CHAR:
            app_input_character((uint32_t)wparam);
            return 0;
        case WM_PASTE:
            if (app_text_target() && ui_edit_paste(app_text_target())) g_app.ui.input_dirty = true;
            return 0;
        case WM_COPY:
            if (app_text_target()) ui_edit_copy(app_text_target());
            return 0;
        case WM_CUT:
            if (app_text_target() && ui_edit_cut(app_text_target())) g_app.ui.input_dirty = true;
            return 0;
        case WM_CLEAR:
            if (app_text_target() && ui_edit_delete_forward(app_text_target())) g_app.ui.input_dirty = true;
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
        case WM_KEYDOWN:
            app_input_key_down((unsigned)wparam, (lparam & 0x40000000) != 0);
            return 0;

    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* 高分屏：逻辑尺寸＝物理尺寸/mul */
void app_apply_hidpi_scale(void) {
    app_sync_layout_axes();
}

/* 创建窗口和 GL 上下文 */
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
    if (!g_app.hwnd) return false;
    g_app.hdc = GetDC(g_app.hwnd);
    if (!g_app.hdc) return false;
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
    if (!pf || !SetPixelFormat(g_app.hdc, pf, &pfd)) return false;

    HGLRC tmp_rc = wglCreateContext(g_app.hdc);
    if (!tmp_rc) return false;
    if (!wglMakeCurrent(g_app.hdc, tmp_rc)) { wglDeleteContext(tmp_rc); return false; }
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

    if (!qlcg_gl_load_functions()) return false;
    if (!qlcg_gl_context_init()) return false;
    if (wglSwapIntervalEXT) wglSwapIntervalEXT(0);
    return true;
}

__declspec(dllexport) DWORD NvOptimusEnablement = 0;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0;

void qlcg_platform_prepare(void) {
    SetProcessDPIAware();
    wchar_t module[2048]; char base[4096],resources[4096],cache[4096];
    DWORD n=GetModuleFileNameW(NULL,module,2048);
    if(!n || n>=2048 || !qlcg_wide_to_utf8(module,base,sizeof(base))) return;
    char *slash=strrchr(base,'\\'); if(!slash) slash=strrchr(base,'/');
    if(!slash) return; *slash=0;
    const char *candidates[]={"/assets","/../assets","","/.."};
    for(int i=0;i<4;i++) {
        char probe[4096]; wchar_t wide[4096];
        snprintf(resources,sizeof(resources),"%s%s",base,candidates[i]);
        snprintf(probe,sizeof(probe),"%s/content",resources);
        if(!qlcg_utf8_to_wide(probe,wide,4096) || GetFileAttributesW(wide)==INVALID_FILE_ATTRIBUTES) continue;
        const char *data=i==3?resources:base;
        snprintf(cache,sizeof(cache),"%s/cache",data);
        qlcg_paths_configure(resources,data,cache);
        return;
    }
}
bool qlcg_platform_window_open(int w, int h) {
    if (init_win32_gl(GetModuleHandleW(NULL), w, h)) return true;
    qlcg_platform_window_close();
    return false;
}
void qlcg_platform_present(void) { SwapBuffers(g_app.hdc); }
void qlcg_platform_capture(void) { SetCapture(g_app.hwnd); }
void qlcg_platform_poll(void) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) g_app.running = false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
void qlcg_platform_window_close(void) {
    qlcg_gl_context_reset();
    wglMakeCurrent(NULL, NULL);
    if (g_app.hglrc) wglDeleteContext(g_app.hglrc);
    if (g_app.hdc) ReleaseDC(g_app.hwnd, g_app.hdc);
    if (g_app.hwnd) DestroyWindow(g_app.hwnd);
    g_app.hglrc = g_app.hdc = g_app.hwnd = NULL;
}
#endif
