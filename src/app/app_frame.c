/* 页面调度与单帧渲染 */
#include "app/app_state.h"
#include "core/dev_tools.h"

/* 触屏状态映射虚拟动作，关闭时清零防卡键 */
static void touch_sync_actions(void) {
    const UiTouch *t = &g_app.touch;
    bool on = t->enabled;
    app_action_set_virtual(ACT_LEFT,    on && t->axis_x < 0);
    app_action_set_virtual(ACT_RIGHT,   on && t->axis_x > 0);
    app_action_set_virtual(ACT_FORWARD, on && t->axis_y < 0);
    app_action_set_virtual(ACT_BACK,    on && t->axis_y > 0);
    app_action_set_virtual(ACT_SPRINT,  on && (t->sprint_locked || t->btn_down[0]));
    app_action_set_virtual(ACT_JUMP,    on && t->btn_down[1]);
    app_action_set_virtual(ACT_CROUCH,  on && t->btn_down[2]);

    /* 空白区拖动转视角，增量由 UI 层累计 */
    if (t->look_dx != 0.0f || t->look_dy != 0.0f) {
        app_look_apply(t->look_dx * 0.0045f, -t->look_dy * 0.0045f, 1.48f);
        g_app.touch.look_dx  = 0.0f;
        g_app.touch.look_dy  = 0.0f;
    }
}

/* 主循环单帧渲染 */
void app_render_frame(float dt) {
    /* 屏幕切换时重放入场淡入 */
    static int last_screen = -1;
    if ((int)g_app.screen != last_screen) {
        if (last_screen == SCREEN_SETTINGS && g_app.screen != SCREEN_SETTINGS) {
            settings_confirm_cancel();
        }
        if ((last_screen == SCREEN_IN_GAME && g_app.screen == SCREEN_START_HOME) ||
            (last_screen == SCREEN_WORLD_CREATOR && g_app.screen != SCREEN_IN_GAME))
            app_release_world();
        last_screen = (int)g_app.screen;
        ui_page_enter(&g_app.ui);
    }

    if (g_app.win_w <= 0 || g_app.win_h <= 0) {
        app_lowres_release();
        return;
    }

    if (g_app.screen != SCREEN_IN_GAME || g_app.view_mode != VIEW_MODE_TERRAIN || !g_app.region)
        app_lowres_release();

    /* 昼夜闸：山川按区块时刻切，其余交回系统 */
    app_sync_hud_daynight();

    uint32_t bg = g_app.ui.pal.bg_canvas;    glClearColor((float)(bg & 0xFF) / 255.0f,
                 (float)((bg >> 8) & 0xFF) / 255.0f,
                 (float)((bg >> 16) & 0xFF) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ui_begin_frame(&g_app.ui, g_app.win_w, g_app.win_h);

    /* 状态反馈：自动 001 只在 3D 前进受阻时出现 */
    {
        int auto_fx = SFX_NONE;
        if (g_app.screen == SCREEN_IN_GAME && g_app.view_mode == VIEW_MODE_TERRAIN &&
            g_app.region && !g_app.esc_paused && !g_app.chat_open &&
            g_app.settings.status_feedback && g_app.terrain.blocked_forward)
            auto_fx = SFX_001;
        ui_status_feedback_update(&g_app.status_feedback, auto_fx,
                                  g_app.settings.status_feedback, g_app.ui.time);
    }

    /* 文本焦点：换占用清按键表，控件消失即失焦 */
    {
        static uint32_t s_last_input = 0;
        static bool     s_last_chat  = false;
        if ((g_app.ui.input_id != 0 && g_app.ui.input_id != s_last_input) ||
            (g_app.chat_open && !s_last_chat)) {
            app_key_track_clear();
        }
        s_last_input = g_app.ui.input_id;
        s_last_chat  = g_app.chat_open;
        if (g_app.screen != SCREEN_IN_GAME && g_app.chat_open) {
            app_text_focus_clear();   /* 离开游戏内就没有聊天条了 */
        }
        uint32_t fr = g_app.ui.frame_idx;
        bool field_dead = g_app.ui.input_id != 0 &&
                          g_app.ui.input_seen_frame != 0 &&
                          fr - g_app.ui.input_seen_frame > 2;
        bool chat_dead  = g_app.chat_open &&
                          g_app.ui.chat_seen_frame != 0 &&
                          fr - g_app.ui.chat_seen_frame > 2;
        if (field_dead || chat_dead) app_text_focus_clear();
    }
    ui_toast_update(&g_app.toasts, g_app.ui.dt);
    g_app.ui.toast_blocked = ui_toast_over_ui(&g_app.ui, &g_app.toasts);
    /* 触屏控件仅游戏内显示，重算后同步虚拟动作 */
    {
        bool touch_on = g_app.screen == SCREEN_IN_GAME && g_app.view_mode == VIEW_MODE_TERRAIN
                        && !g_app.esc_paused && !g_app.settings_in_drawer && !g_app.chat_open
                        && g_app.settings.touch_controls;
        ui_touch_begin(&g_app.touch, &g_app.ui, touch_on, g_app.settings.touch_opacity,
                       (const float (*)[3])g_app.settings.touch_layout);
        touch_sync_actions();
    }
    switch (g_app.screen) {
        case SCREEN_START_HOME:       render_screen_home(&g_app.ui, dt);       break;
        case SCREEN_PLAY_MODE_SELECT: render_screen_play_mode_select(&g_app.ui); break;
        case SCREEN_WORLD_CREATOR:    render_screen_world_creator(&g_app.ui);  break;
        case SCREEN_MEMORIES_LOAD:    render_screen_memories(&g_app.ui);       break;
        case SCREEN_SETTINGS:         render_screen_settings(&g_app.ui);       break;
        case SCREEN_IN_GAME:          render_screen_in_game(&g_app.ui);        break;
        case SCREEN_SPRITE_TEST:      qlcg_dev_render_sprites(&g_app.ui);        break;
        default: break;
    }

    render_rail_language_popup(&g_app.ui);

    /* 右上堆叠通知：先推进计时再绘制 */
    ui_toast_render(&g_app.ui, &g_app.toasts);

    ui_end_frame(&g_app.ui);
    SwapBuffers(g_app.hdc);
}
