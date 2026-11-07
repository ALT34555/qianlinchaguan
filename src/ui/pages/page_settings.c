/* 设置页：档案/界面/图像/操作 */
#include "app/app_state.h"
#include "core/dev_tools.h"
#include "ui/ui_scroll.h"
#include "ui/ui_layout.h"
#include "ui/ui_touch.h"
#include "ui/ui_sprite.h"

/* 虚拟按键布局编辑器：全屏自由拖动，写回相对坐标 */
static int   s_layout_drag = -1;
static float s_layout_off_x = 0.0f, s_layout_off_y = 0.0f;

#define SEL_ID_FONT     0x5E1EC7u
#define SEL_ID_RES      0x5E1EC8u
#define SEL_ID_PRESET   0x5E1EC9u
#define SEL_ID_VEG_FORM 0x5E1ECAu
#define SEL_ID_MEAS     0x5E1ECBu
#define SEL_ID_LANG     0x5E1ECCu
#define SEL_ID_NAME     0x5E1ECDu
#define SEL_ID_SPEED    0x5E1ECEu
#define SEL_ID_HIDPI    0x5E1ECFu
#define SEL_ID_TOUCH_STYLE 0x5E1ED0u

static const char *const k_res_ids[7] = {
    "auto", "320x240", "640x480", "960x540",
    "1280x720", "1920x1080", "2560x1440"
};

/* 高分辨率显示设备档位，顺序对应下拉项 */
static const float k_hidpi_vals[5] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };

static const GraphicsPreset k_preset_vals[6] = {
    PRESET_EXTREME, PRESET_HIGH, PRESET_BALANCED,
    PRESET_SMOOTH,  PRESET_LOW,  PRESET_MINIMAL
};

/* 12 秒设置确认弹窗状态 */
typedef struct {
    bool  active;
    float timer;
    float prev_safe_x;
    float prev_safe_y;
    float prev_text_scale;
    float prev_ui_scale;
} SettingsConfirmModal;

static SettingsConfirmModal s_confirm;

void settings_confirm_begin(float safe_x, float safe_y, float text_scale, float ui_scale) {
    s_confirm.prev_safe_x = safe_x;
    s_confirm.prev_safe_y = safe_y;
    s_confirm.prev_text_scale = text_scale;
    s_confirm.prev_ui_scale = ui_scale;
    s_confirm.active = true;
    s_confirm.timer = 12.0f;
}

bool settings_confirm_active(void) {
    return s_confirm.active;
}

void settings_confirm_cancel(void) {
    GameSettings *s = &g_app.settings;
    if (s_confirm.active) {
        s->safe_x     = s_confirm.prev_safe_x;
        s->safe_y     = s_confirm.prev_safe_y;
        s->text_scale = s_confirm.prev_text_scale;
        s->ui_scale   = s_confirm.prev_ui_scale;
        s_confirm.active = false;
        app_sync_layout_axes();
        ql_metrics_build(&g_app.ui.met, g_app.win_w, g_app.win_h,
                         g_app.ui.user_scale, g_app.ui.user_size, g_app.ui.hidpi_mul);
        app_push_toast_k(UI_TOAST_WARNING, NULL, tr("settings.confirm.reverted"));
    } else {
        /* user_scale 含hidpi，须除回再写回 */
        float hd = g_app.ui.hidpi_mul > 0.0f ? g_app.ui.hidpi_mul : 1.0f;
        s->safe_x     = g_app.ui.safe_x_pct;
        s->safe_y     = g_app.ui.safe_y_pct;
        s->text_scale = (g_app.ui.user_scale > 0.0f ? g_app.ui.user_scale : 1.0f) / hd;
        s->ui_scale   = (g_app.ui.user_size  > 0.0f ? g_app.ui.user_size  : 1.0f) / hd;
    }
}

/* 独立于缩放 (zoom:1) 的顶层确认弹窗绘制 */
static void draw_settings_confirm_modal(UiContext *ui) {
    if (!s_confirm.active) return;

    s_confirm.timer -= ui->dt;
    if (s_confirm.timer <= 0.0f) {
        settings_confirm_cancel();
        return;
    }

    float saved_size  = ui->met.size;
    float saved_scale = ui->met.scale;
    ui->met.size  = 1.0f;
    ui->met.scale = 1.0f;

    ui_draw_rect(ui, 0.0f, 0.0f, (float)ui->win_w, (float)ui->win_h, ui_rgba(32, 25, 16, 166));

    float dw  = qlcg_minf(420.0f, (float)ui->win_w - 24.0f);
    float pad = 24.0f;
    float iw  = qlcg_maxf(60.0f, dw - pad * 2.0f);

    int secs = (int)ceilf(s_confirm.timer);
    if (secs < 1) secs = 1;
    char msg[192];
    snprintf(msg, sizeof(msg), tr("settings.confirm.countdown"), secs);

    float title_px = 20.0f;
    float body_px  = 14.0f;
    float btn_px   = 14.0f;
    float title_lh = ui_line_height(UI_FACE_DISPLAY, title_px);
    float body_lh  = ui_line_height(UI_FACE_BODY, body_px);
    int   lines    = ui_measure_text_wrapped(ui, UI_FACE_BODY, body_px, false, iw, msg);
    if (lines < 1) lines = 1;
    float btn_h    = 40.0f;

    float dh = pad * 2.0f + title_lh + 12.0f + (float)lines * body_lh + 20.0f + btn_h;
    float dx = ((float)ui->win_w - dw) * 0.5f;
    float dy = ((float)ui->win_h - dh) * 0.5f;

    ui_panel(ui, dx, dy, dw, dh, 12.0f, tk_of(ui, QL_SURFACE_HI),
             tk_of(ui, QL_LINE), tk_of(ui, QL_SHADOW_LG));

    float cy = dy + pad;
    ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, title_px, true, dx + pad, cy, iw, 0,
                          tr("settings.confirm.title"), tk_of(ui, QL_INK));
    cy += title_lh + 12.0f;

    ui_draw_text_wrapped(ui, UI_FACE_BODY, body_px, false, dx + pad, cy, iw, body_lh,
                         msg, tk_of(ui, QL_INK_2));
    cy += (float)lines * body_lh + 20.0f;

    const char *s_yes = tr("settings.confirm.yes");
    const char *s_no  = tr("settings.confirm.no");
    float w_yes = qlcg_maxf(72.0f, (float)ui_text_width_px(ui, UI_FACE_BODY, btn_px, false, s_yes) + 36.0f);
    float w_no  = (float)ui_text_width_px(ui, UI_FACE_BODY, btn_px, false, s_no) + 32.0f;
    float bx_yes = dx + dw - pad - w_yes;
    float bx_no  = bx_yes - 12.0f - w_no;

    bool click_no  = ui_btn(ui, bx_no,  cy, w_no,  btn_h, s_no,  BTN_GHOST,   false, btn_px, false);
    bool click_yes = ui_btn(ui, bx_yes, cy, w_yes, btn_h, s_yes, BTN_PRIMARY, false, btn_px, false);

    ui->met.size  = saved_size;
    ui->met.scale = saved_scale;

    if (click_no) {
        settings_confirm_cancel();
    } else if (click_yes) {
        s_confirm.active = false;
        qlcg_save_settings_disk(&g_app.settings);
        app_push_toast_k(UI_TOAST_SUCCESS, NULL, tr("settings.status.applied"));
    }
}

void settings_apply_palette(UiContext *ui) {
    (void)ui;
    app_apply_ui_palette();
}

bool settings_color_editor(UiContext *ui, float x, float y, float w, const char *title,
                           uint32_t *color) {
    UiLayout L;
    ui_layout_begin(&L, ui, x, y, w);
    return ui_layout_color(&L, title, color);
}

/* 编辑期布局备份，取消时还原 */

static float s_layout_backup[UI_TOUCH_ITEM_COUNT][3];

void settings_layout_cancel(void) {
    if (!g_app.touch_layout_edit) return;
    memcpy(g_app.settings.touch_layout, s_layout_backup, sizeof(s_layout_backup));
    g_app.touch_layout_edit = false;
    s_layout_drag = -1;
    g_app.ui.drag_slider_id = -1;
}

/* 四项控件的当前框，换算与 ui_touch.c 同源 */
static void touch_edit_boxes(UiContext *ui, const GameSettings *s,
                             float ox, float oy, float sw, float sh,
                             float *bx, float *by, float *bw) {
    float z = ui->met.size;
    for (int i = 0; i < UI_TOUCH_ITEM_COUNT; i++) {
        float base = (i == UI_TOUCH_ITEM_MOVE)
                     ? qlcg_clampf(168.0f * z, 150.0f, 210.0f)
                     : qlcg_clampf(62.0f * z, 56.0f, 78.0f);
        float box = base * s->touch_layout[i][2];
        bx[i] = ox + s->touch_layout[i][0] * (sw - box);
        by[i] = oy + s->touch_layout[i][1] * (sh - box);
        bw[i] = box;
    }
}

static void draw_touch_layout_editor(UiContext *ui) {
    if (!g_app.touch_layout_edit) return;
    GameSettings *s = &g_app.settings;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float ox = ui->safe_x_px, oy = ui->safe_y_px;
    float sw = (float)ui->win_w - ox * 2.0f;
    float sh = (float)ui->win_h - oy * 2.0f;
    float dw = qlcg_minf(380.0f * z, (float)ui->win_w * 0.72f);
    float dh = 306.0f * z;
    float dx = ((float)ui->win_w - dw) * 0.5f;
    float dy = ((float)ui->win_h - dh) * 0.5f;
    bool over_panel = ui_hit(ui, dx, dy, dw, dh);

    ui_draw_rect(ui, 0, 0, (float)ui->win_w, (float)ui->win_h, ui_rgba(20, 14, 10, 198));

    float bx[UI_TOUCH_ITEM_COUNT], by[UI_TOUCH_ITEM_COUNT], bw[UI_TOUCH_ITEM_COUNT];
    touch_edit_boxes(ui, s, ox, oy, sw, sh, bx, by, bw);

    if (!ui->mouse_down) s_layout_drag = -1;
    for (int i = 0; i < UI_TOUCH_ITEM_COUNT; i++) {
        if (!over_panel && ui_hit(ui, bx[i], by[i], bw[i], bw[i]) &&
            ui->mouse_clicked && s_layout_drag < 0) {
            s_layout_drag  = i;
            s_layout_off_x = (float)ui->mouse_x - bx[i];
            s_layout_off_y = (float)ui->mouse_y - by[i];
        }
        if (s_layout_drag == i && ui->mouse_down && !ui_pointer_blocked(ui)) {
            float nx = (float)ui->mouse_x - s_layout_off_x - ox;
            float ny = (float)ui->mouse_y - s_layout_off_y - oy;
            s->touch_layout[i][0] = qlcg_clampf(nx / qlcg_maxf(sw - bw[i], 1.0f), 0.0f, 1.0f);
            s->touch_layout[i][1] = qlcg_clampf(ny / qlcg_maxf(sh - bw[i], 1.0f), 0.0f, 1.0f);
        }
    }

    for (int i = 0; i < UI_TOUCH_ITEM_COUNT; i++) {
        uint32_t c0 = tk_of(ui, QL_SURFACE);
        uint32_t c1 = tk_of(ui, QL_PRIMARY_UI);
        uint32_t c2 = tk_of(ui, QL_ACCENT_UI);
        if (i == UI_TOUCH_ITEM_MOVE) {
            const SpriteDef *sb = ui_sprite_by_name("joy-base");
            if (sb) ui_sprite_icon4(ui, sb, bx[i], by[i], bw[i], c0, c1, c2);
            float knob = bw[i] * 0.40f;
            const SpriteDef *sk = ui_sprite_by_name("joy-knob");
            if (sk)
                ui_sprite_icon4(ui, sk, bx[i] + (bw[i] - knob) * 0.5f,
                                by[i] + (bw[i] - knob) * 0.5f, knob, c0, c1, c2);
        } else {
            const SpriteDef *sp = ui_sprite_by_name("action-plate");
            if (sp) ui_sprite_icon4(ui, sp, bx[i], by[i], bw[i], c0, c1, c2);
            static const char *BTN_ICON[UI_TOUCH_BTN_COUNT] = {
                "btn-sprint", "btn-jump", "btn-crouch"
            };
            float icon = bw[i] * 0.64f;
            const SpriteDef *si = ui_sprite_by_name(BTN_ICON[i - 1]);
            if (si)
                ui_sprite_icon4(ui, si, bx[i] + (bw[i] - icon) * 0.5f,
                                by[i] + (bw[i] - icon) * 0.5f, icon, c0, c1, c2);
        }
        if (s_layout_drag == i)
            ui_draw_border(ui, bx[i] - 2.0f * z, by[i] - 2.0f * z,
                           bw[i] + 4.0f * z, bw[i] + 4.0f * z, 2.0f * z,
                           tk_of(ui, QL_PRIMARY_UI));
    }

    /* 中央对话框：四条尺寸滑条 + 三个按钮 */
    ui_draw_rect(ui, dx, dy, dw, dh, tk_of(ui, QL_SURFACE));
    ui_draw_border(ui, dx, dy, dw, dh, 1.0f, tk_of(ui, QL_LINE));

    float pad = 18.0f * z;
    float px  = 11.5f * sc * 1.3f;
    float lpx = 11.5f * sc;
    float lh  = ui_line_height(UI_FACE_BODY, lpx);
    ui_draw_text_px(ui, UI_FACE_BODY, 14.0f * sc * 1.3f, true, dx + pad, dy + pad,
                    tr("settings.touchLayout"), tk_of(ui, QL_INK));

    static const char *SIZE_KEY[UI_TOUCH_ITEM_COUNT] = {
        "settings.touchLayout.moveSize", "settings.touchLayout.sprintSize",
        "settings.touchLayout.jumpSize", "settings.touchLayout.crouchSize"
    };
    float sy = dy + pad + 26.0f * z;
    for (int i = 0; i < UI_TOUCH_ITEM_COUNT; i++) {
        char vb[16];
        snprintf(vb, sizeof(vb), "%.0f%%", s->touch_layout[i][2] * 100.0f);
        float vw = (float)ui_text_width_px(ui, UI_FACE_MONO, 11.0f * sc, false, vb) + 8.0f * z;
        ui_draw_text_px(ui, UI_FACE_MONO, 11.0f * sc, false,
                        dx + dw - pad - vw, sy, vb, tk_of(ui, QL_INK));
        ui_draw_text_px(ui, UI_FACE_BODY, lpx, false, dx + pad, sy,
                        tr(SIZE_KEY[i]), tk_of(ui, QL_INK_2));
        ui_slider_at(ui, dx + pad, sy + lh + 4.0f * z, dw - pad * 2.0f,
                     &s->touch_layout[i][2], 0.5f, 2.0f);
        sy += lh + 4.0f * z + 16.0f * z;
    }

    float by2 = dy + dh - pad - 32.0f * z;
    float bwd = (dw - pad * 2.0f - 24.0f * z) / 3.0f;
    static const char *BTN_KEY[3] = {
        "settings.touchLayoutConfirm", "settings.touchLayoutCancel",
        "settings.touchLayoutReset"
    };
    for (int i = 0; i < 3; i++) {
        bool primary = (i == 0);
        float bxp = dx + pad + (float)i * (bwd + 12.0f * z);
        bool hov = ui_hit(ui, bxp, by2, bwd, 32.0f * z);
        ui_draw_rect(ui, bxp, by2, bwd, 32.0f * z,
                     primary ? (hov ? tk_of(ui, QL_PRIMARY_DEEP) : tk_of(ui, QL_PRIMARY_UI))
                             : (hov ? tk_of(ui, QL_SURFACE_HI) : tk_of(ui, QL_SURFACE)));
        ui_draw_border(ui, bxp, by2, bwd, 32.0f * z, 1.0f,
                       primary ? tk_of(ui, QL_PRIMARY_DEEP) : tk_of(ui, QL_LINE));
        uint32_t tc = primary ? tk_of(ui, QL_ON_PRIMARY) : tk_of(ui, QL_INK);
        float tw = (float)ui_text_width_px(ui, UI_FACE_BODY, px, false, tr(BTN_KEY[i]));
        ui_draw_text_px(ui, UI_FACE_BODY, px, false, bxp + (bwd - tw) * 0.5f,
                        by2 + (32.0f * z - px * 1.35f) * 0.5f, tr(BTN_KEY[i]), tc);
        if (hov && ui->mouse_clicked) {
            if (i == 0) {
                g_app.touch_layout_edit = false;
                qlcg_save_settings_disk(s);
            } else if (i == 1) {
                settings_layout_cancel();
            } else {
                qlcg_touch_layout_default(s->touch_layout);
            }
        }
    }

    ui_draw_text_ellipsis(ui, UI_FACE_BODY, lpx, false, dx + pad, dy + dh - pad - 58.0f * z,
                          dw - pad * 2.0f, 0, tr("settings.touchLayoutPrompt"),
                          tk_of(ui, QL_INK_3));
}

void render_screen_settings(UiContext *ui) {
    float z  = ui->met.size;
    float sc = ui->met.scale;

    qlcg_dev_settings_fixture(ui);

    bool orig_click = ui->mouse_clicked;
    bool orig_down  = ui->mouse_down;
    int  orig_wheel = ui->wheel_delta;
    bool orig_blocked = ui->input_blocked;
    bool had_confirm = s_confirm.active;
    bool had_editor = g_app.touch_layout_edit;
    if (had_confirm || had_editor) {
        ui->input_blocked = true;
        ui->select_id = 0;
        ui->capture_action = -1;
        ui->input_id = 0;
        ui->mouse_clicked = false;
        ui->mouse_down    = false;
        ui->wheel_delta   = 0;
    }

    UiRect page = draw_page_shell(ui);
    GameSettings *s = &g_app.settings;
    bool changed = false;
    bool need_theme = false;

    /* 页头：抽屉态由 shell 画在左上角 */
    float h1 = qlcg_clampf((float)ui->win_w * 0.023f, 20.0f, 27.0f) * sc;
    float head_h = g_app.settings_in_drawer ? 0.0f : ui_line_height(UI_FACE_DISPLAY, h1);
    if (!g_app.settings_in_drawer) {
        ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, h1, false, page.x, page.y, page.w, 0,
                              tr("settings.title"), tk_of(ui, QL_INK));
    }

    float fbtn_px = 12.5f * sc;
    float fbtn_h  = ui_ctl_height(30.0f * z, fbtn_px);
    float foot_h  = fbtn_h + 16.0f * z;

    /* 三套上下文：主页/悠游/生存 */
    bool world_active = app_world_active();
    bool cheats_on    = app_cheats_allowed();
    bool mystery_on   = world_active && cheats_on;   /* 玄学页签：世界激活且允许作弊 */

    /* 左列书签页签 */
    const char *tabs[5] = {
        tr("settings.profile"),
        tr("settings.interface"),
        tr("settings.graphics"),
        tr("settings.controls"),
        tr("settings.mystery")
    };
    int tab_count = mystery_on ? 5 : 4;
    if (!mystery_on && g_app.set_tab == SET_TAB_METAPHYSICS) {
        g_app.set_tab = SET_TAB_PROFILE;
    }
    float tab_px = 14.0f * sc;
    float tab_w  = 96.0f * z;
    for (int i = 0; i < tab_count; i++) {
        float tw = (float)ui_text_width_px(ui, UI_FACE_DISPLAY, tab_px, false, tabs[i]) + 36.0f * z;
        if (tw > tab_w) tab_w = tw;
    }
    tab_w = qlcg_minf(tab_w, page.w * 0.28f);
    float tab_h = ui_ctl_height(40.0f * z, tab_px);
    float tx = page.x;
    float ty = page.y + head_h + 10.0f * z;
    for (int i = 0; i < tab_count; i++) {
        bool act = ((int)g_app.set_tab == i);
        bool hov = ui_hit(ui, tx, ty, tab_w, tab_h);
        float off = act ? 4.0f * z : 0.0f;
        if (act) {
            ui_shadow_soft(ui, tx + off, ty, tab_w, tab_h, 5.0f, 2.0f, tk_of(ui, QL_SHADOW_SM));
            ui_draw_rect(ui, tx + off, ty, tab_w, tab_h, tk_of(ui, QL_SURFACE));
            ui_draw_border(ui, tx + off, ty, tab_w, tab_h, 1.0f, tk_of(ui, QL_LINE_SOFT));
            ui_draw_rect(ui, tx, ty, 2.0f * z, tab_h, tk_of(ui, QL_PRIMARY_UI));
        } else if (hov) {
            ui_draw_rect(ui, tx, ty, tab_w, tab_h, tk_of(ui, QL_ACCENT_SOFT));
        }
        ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, tab_px, false, tx + off + 14.0f * z,
                              ty + (tab_h - tab_px * 1.25f) * 0.5f,
                              qlcg_maxf(12.0f, tab_w - 20.0f * z), 0, tabs[i],
                              act ? tk_of(ui, QL_INK) : tk_of(ui, QL_INK_2));
        if (hov && ui->mouse_clicked) {
            ui->select_id = 0;
            ui->input_id  = 0;
            g_app.set_tab = (SettingsSubTab)i;
        }
        ty += tab_h + 6.0f * z;
    }

    /* 右侧内容面板 */
    float cx = page.x + tab_w + 16.0f * z;
    float cw = qlcg_maxf(60.0f, page.x + page.w - cx);
    float cy = page.y + head_h + 10.0f * z;
    float ch = qlcg_maxf(60.0f, page.y + page.h - cy - foot_h);
    ui_panel(ui, cx, cy, cw, ch, 6.0f, tk_of(ui, QL_SURFACE), tk_of(ui, QL_LINE_SOFT), tk_of(ui, QL_SHADOW_SM));

    float ix = cx + 20.0f * z;
    float iw = qlcg_maxf(40.0f, cw - 40.0f * z);
    float sc_off = ui_scroll_begin(ui, 0x5E771Du, cx, cy, cw, ch);
    float start_y = cy + 18.0f * z - sc_off;
    char buf[256];

    UiLayout L;
    ui_layout_begin(&L, ui, ix, start_y, iw);

    switch (g_app.set_tab) {
        case SET_TAB_PROFILE: {
            /* base：主页才显示通用默认档案提示 */
            if (!world_active) ui_layout_banner(&L, tr("profile.defaultHint"));

            /* 未解锁：档案锁定，压暗并吞点击 */
            bool locked = world_active && !cheats_on;
            bool  saved_click = ui->mouse_clicked;
            float saved_alpha = ui->global_alpha;
            if (locked) {
                ui->mouse_clicked = false;
                ui->global_alpha *= 0.55f;
            }

            /* 名字实时写回，上限40字（同base） */
            if (ui_layout_text_input(&L, SEL_ID_NAME, tr("profile.name"),
                                     s->ren_ming, sizeof(s->ren_ming), 40,
                                     tr("profile.nameHint"))) {
                snprintf(s->profile.name, sizeof(s->profile.name), "%s", s->ren_ming);
                changed = true;
            }

            /* 身高：解锁8~1024，否则64~256 */
            int hmin = 64, hmax = 256;
            app_profile_height_range(&hmin, &hmax);
            const char *h_key = (s->measurement_system == MEAS_MING) ? "profile.height.ming"
                              : (s->measurement_system == MEAS_IMPERIAL ? "profile.height.imperial" : "profile.height.metric");
            if (ui_layout_height_input(&L, 0x48475401u, tr(h_key), s->measurement_system,
                                       &s->profile.height_cm, hmin, hmax)) {
                changed = true;
            }

            ui_layout_divider(&L);
            if (ui_layout_color(&L, tr("profile.mainColor"), &s->profile.main_color)) {
                changed = true;
                need_theme = true;
            }

            ui_layout_divider(&L);
            if (ui_layout_color(&L, tr("profile.trimColor"), &s->profile.trim_color)) {
                changed = true;
                need_theme = true;
            }

            if (locked) {
                ui->mouse_clicked = saved_click;
                ui->global_alpha   = saved_alpha;
                ui_layout_gap(&L, 4.0f * z);
                ui_layout_note(&L, tr("settings.profileLocked"), true);
            }

            /* base：只在主页提供「偏好色应用到界面」 */
            if (!world_active) {
                ui_layout_gap(&L, 4.0f * z);
                if (ui_layout_checkbox(&L, tr("settings.profilePaletteToInterface"), &s->profile_palette_to_ui)) {
                    changed = true;
                    need_theme = true;
                }
            }
            if (need_theme) {
                if (s->profile_palette_to_ui) settings_apply_palette(ui);
                else {
                    QlColor p = ql_rgb(0xCB3A56), a = ql_rgb(0x5D3F51);
                    ui_set_palette(ui, ql_to_hex(p), ql_to_hex(a));
                }
            }
            break;
        }
        case SET_TAB_UI: {
            /* 度量衡：下拉选择（明制 / 公制 / 英制，默认公制） */
            const char *meas[3] = {
                tr("settings.measurementSystem.ming"),
                tr("settings.measurementSystem.metric"),
                tr("settings.measurementSystem.imperial")
            };
            int mcur = qlcg_clampi((int)s->measurement_system, 0, 2);
            ui_layout_select(&L, SEL_ID_MEAS, tr("settings.measurementSystem"), meas, 3, mcur);

            /* 界面语言：内置中英 + l10n 外置包 */
            {
                const char *lnames[QLCG_MAX_LOCALES];
                int nloc = qlcg_i18n_count();
                if (nloc > 12) nloc = 12;
                for (int i = 0; i < nloc; i++) {
                    lnames[i] = qlcg_i18n_meta(i)->label;
                }
                int lcur = qlcg_clampi(qlcg_i18n_get_index(), 0, nloc - 1);
                float lh = ui_select_height(ui, 30.0f * z);
                const char *rescan_lbl = tr("settings.language.rescan");
                float btn_w = (float)ui_text_width_px(ui, UI_FACE_BODY, 11.5f * sc, false, rescan_lbl) + 28.0f * z;
                UiRow rl = ui_row_auto(&L, tr("settings.language"), false, btn_w + 140.0f * z, lh);
                if (rl.ctl_w >= btn_w + 100.0f * z) {
                    /* 下拉 + 按钮整组贴右 */
                    float sw = qlcg_minf(rl.ctl_w - btn_w - 8.0f * z, 320.0f * z);
                    float gx = rl.ctl_x + rl.ctl_w - (sw + 8.0f * z + btn_w);
                    float gy = rl.y + (rl.h - lh) * 0.5f;
                    ui_select(ui, SEL_ID_LANG, gx, gy, sw, lh, lnames, nloc, lcur);
                    if (ui_btn(ui, gx + sw + 8.0f * z, gy,
                               btn_w, lh, rescan_lbl, BTN_GHOST, false, 11.5f * sc, false)) {
                        qlcg_i18n_rescan(QLCG_L10N_DIR);
                    }
                } else {
                    ui_select(ui, SEL_ID_LANG, rl.ctl_x, rl.y, rl.ctl_w, lh, lnames, nloc, lcur);
                    float by = L.cursor;
                    if (ui_btn(ui, rl.ctl_x, by, qlcg_minf(rl.ctl_w, btn_w), lh,
                               rescan_lbl, BTN_GHOST, false, 11.5f * sc, false)) {
                        qlcg_i18n_rescan(QLCG_L10N_DIR);
                    }
                    L.cursor = by + lh + L.row_gap;
                }
                ui_layout_note(&L, tr("settings.language.hint"), true);
            }

            const char *themes[3] = {
                tr("settings.theme.light"),
                tr("settings.theme.dark"),
                tr("settings.theme.auto")
            };
            int tp = ui_layout_labeled_segments(&L, tr("settings.theme"), themes, 3, (int)s->theme);
            if (tp >= 0) {
                s->theme = (ThemeMode)tp;
                ui_apply_theme(ui, s->theme);
                changed = true;
            }

            if (ui_layout_checkbox(&L, tr("settings.showTopBar"), &s->show_top_bar)) changed = true;
            if (ui_layout_checkbox(&L, tr("settings.reduceMotion"), &s->reduce_motion)) {
                ui->reduce_motion = s->reduce_motion;
                changed = true;
            }

            int cp = ui_layout_crosshair(&L, tr("settings.crosshair"), (int)s->crosshair);
            if (cp >= 0) {
                s->crosshair = (CrosshairStyle)cp;
                changed = true;
            }

            /* 字体下拉 + 重新扫描按钮 */
            {
                const char *names[13];
                static char tmp[12][48];
                int nf = 1;
                int cur = 0;
                names[0] = tr("settings.font.system");
                if (strcmp(s->font_id, "system") == 0) cur = 0;
                for (int i = 0; i < g_app.mod_fonts.count && i < 12; i++) {
                    WideCharToMultiByte(CP_UTF8, 0, g_app.mod_fonts.items[i].label, -1,
                                        tmp[i], sizeof(tmp[i]), NULL, NULL);
                    names[nf] = tmp[i];
                    if (strcmp(s->font_id, g_app.mod_fonts.items[i].id) == 0) cur = nf;
                    nf++;
                }
                float fh = ui_select_height(ui, 30.0f * z);
                const char *frescan = tr("settings.font.rescan");
                float btn_w = (float)ui_text_width_px(ui, UI_FACE_BODY, 11.5f * sc, false, frescan) + 28.0f * z;
                UiRow rf = ui_row_auto(&L, tr("settings.font"), false, btn_w + 140.0f * z, fh);
                if (rf.ctl_w >= btn_w + 100.0f * z) {
                    float sw = qlcg_minf(rf.ctl_w - btn_w - 8.0f * z, 320.0f * z);
                    ui_select(ui, SEL_ID_FONT, rf.ctl_x, rf.y + (rf.h - fh) * 0.5f, sw, fh, names, nf, cur);
                    if (ui_btn(ui, rf.ctl_x + sw + 8.0f * z, rf.y + (rf.h - fh) * 0.5f,
                               btn_w, fh, frescan, BTN_GHOST, false, 11.5f * sc, false)) {
                        app_rescan_fonts();
                    }
                } else {
                    ui_select(ui, SEL_ID_FONT, rf.ctl_x, rf.y, rf.ctl_w, fh, names, nf, cur);
                    float by = L.cursor;
                    if (ui_btn(ui, rf.ctl_x, by, qlcg_minf(rf.ctl_w, btn_w), fh,
                               frescan, BTN_GHOST, false, 11.5f * sc, false)) {
                        app_rescan_fonts();
                    }
                    L.cursor = by + fh + L.row_gap;
                }
                snprintf(buf, sizeof(buf), tr("settings.font.hintFmt"), g_app.mod_fonts.count);
                ui_layout_note(&L, buf, true);
            }

            /* 三项尺寸/安全区仅改草稿，点应用后弹窗确认 */
            float sfx = s->safe_x;
            snprintf(buf, sizeof(buf), "%.0f%%", sfx);
            if (ui_layout_slider(&L, tr("settings.safeX"), &sfx, 0.0f, 35.0f, buf)) {
                s->safe_x = sfx;
            }

            float sfy = s->safe_y;
            snprintf(buf, sizeof(buf), "%.0f%%", sfy);
            if (ui_layout_slider(&L, tr("settings.safeY"), &sfy, 0.0f, 35.0f, buf)) {
                s->safe_y = sfy;
            }

            float ts = s->text_scale;
            snprintf(buf, sizeof(buf), tr("settings.scale.multiplier"), ts);
            if (ui_layout_slider(&L, tr("settings.textScale"), &ts, 0.6f, 4.0f, buf)) {
                s->text_scale = ts;
            }

            float us = s->ui_scale;
            snprintf(buf, sizeof(buf), tr("settings.scale.multiplier"), us);
            if (ui_layout_slider(&L, tr("settings.uiScale"), &us, 0.25f, 4.0f, buf)) {
                s->ui_scale = us;
            }
            ui_layout_note(&L, tr("settings.scaleConfirmHint"), true);

            ui_layout_divider(&L);
            ui_layout_note(&L, tr("settings.paletteNote"), false);
            if (ui_layout_color(&L, tr("settings.interfaceMainColor"), &s->interface_main_color)) {
                s->profile_palette_to_ui = false;
                settings_apply_palette(ui);
                changed = true;
            }
            if (ui_layout_color(&L, tr("settings.interfaceTrimColor"), &s->interface_trim_color)) {
                s->profile_palette_to_ui = false;
                settings_apply_palette(ui);
                changed = true;
            }
            break;
        }
        case SET_TAB_GRAPHICS: {
            const char *preset_names[6] = {
                tr("settings.graphicsPreset.extreme"),
                tr("settings.graphicsPreset.high"),
                tr("settings.graphicsPreset.balanced"),
                tr("settings.graphicsPreset.smooth"),
                tr("settings.graphicsPreset.low"),
                tr("settings.graphicsPreset.minimal")
            };
            int preset_cur = 2;
            for (int i = 0; i < 6; i++) {
                if (s->graphics_preset == k_preset_vals[i]) { preset_cur = i; break; }
            }
            /* 高分辨率显示设备：base 没有，放在画面页首行 */
            {
                const char *hidpi_names[5] = {
                    tr("settings.hidpi.v1"),  tr("settings.hidpi.v15"),
                    tr("settings.hidpi.v2"),  tr("settings.hidpi.v3"),
                    tr("settings.hidpi.v4")
                };
                int hidpi_cur = 0;
                float best = 1e9f;
                for (int i = 0; i < 5; i++) {
                    float d = fabsf(s->hidpi_scale - k_hidpi_vals[i]);
                    if (d < best) { best = d; hidpi_cur = i; }
                }
                ui_layout_select(&L, SEL_ID_HIDPI, tr("settings.hidpi"),
                                 hidpi_names, 5, hidpi_cur);
                ui_layout_note(&L, tr("settings.hidpi.hint"), true);
            }

            ui_layout_select(&L, SEL_ID_PRESET, tr("settings.graphicsPreset"), preset_names, 6, preset_cur);
            ui_layout_note(&L, tr("settings.graphicsPreset.hint"), true);

            if (ui_layout_row_checkbox(&L, tr("settings.antialias"), tr("common.enable"), &s->antialias)) changed = true;

            {
                char b[24];
                snprintf(b, sizeof(b), "%d°", s->fov);
                float fov = (float)s->fov;
                if (ui_layout_slider(&L, tr("settings.fov"), &fov, 30.0f, 120.0f, b)) {
                    s->fov = (int)roundf(fov);
                    changed = true;
                }
            }

            /* 视距与植被同尺，供限制竖线对齐 */
            float rd_ratio = 0.0f, rd_y = 0.0f;
            {
                char b[48];
                snprintf(b, sizeof(b), tr("settings.chunksUnit"), s->render_distance);
                float rd = (float)s->render_distance;
                float rd_row = L.cursor;
                if (ui_layout_slider(&L, tr("settings.renderDistance"), &rd, 4.0f, 32.0f, b)) {
                    s->render_distance = (int)roundf(rd);
                    if (s->vegetation_distance > s->render_distance) {
                        s->vegetation_distance = s->render_distance;
                    }
                    changed = true;
                }
                rd_ratio = ((float)s->render_distance - 4.0f) / 28.0f;
                rd_y = rd_row + ui_line_height(UI_FACE_BODY, 11.5f * sc) + 9.0f * z;
            }
            {
                char b[48];
                if (s->vegetation_distance > s->render_distance) {
                    s->vegetation_distance = s->render_distance;
                }
                snprintf(b, sizeof(b), tr("settings.chunksUnit"), s->vegetation_distance);
                float vd = (float)s->vegetation_distance;
                float y0 = L.cursor;
                /* 视距同尺4..32，上限截到视距 */
                if (ui_layout_slider_steps(&L, tr("settings.vegetation"), &vd, 4.0f, 32.0f,
                                           0, (float)s->render_distance, NULL, b)) {
                    s->vegetation_distance = (int)roundf(vd);
                    changed = true;
                }
                float veg_y = y0 + ui_line_height(UI_FACE_BODY, 11.5f * sc) + 9.0f * z;
                if (veg_y > rd_y) {
                    float mx = L.x + L.indent + (L.w - L.indent) * rd_ratio;
                    ui_draw_rect(ui, mx - 0.5f * z, rd_y, 1.0f * z, veg_y - rd_y,
                                 tk_of(ui, QL_LINE));
                }
            }
            {
                char b[160];
                snprintf(b, sizeof(b), tr("settings.renderBudgetFmt"),
                         s->render_distance * 32, s->render_distance);
                ui_layout_note(&L, b, true);
            }

            if (ui_layout_row_checkbox(&L, tr("settings.topBarLabel"), tr("common.show"), &s->show_top_bar)) changed = true;

            ui_layout_divider(&L);

            bool open = g_app.set_fold_visual;
            if (ui_layout_fold(&L, 0x5E1F01u, tr("settings.advancedVisuals"), &open)) {
                ui_layout_indent_push(&L);
                struct { const char *key; bool *v; } flags[] = {
                    { "settings.particles",          &s->particles },
                    { "settings.waterReflections",   &s->water_reflections },
                    { "settings.waterWaves",         &s->water_waves },
                    { "settings.weatherAtmosphere",  &s->weather_atmosphere },
                    { "settings.decorativeClouds",   &s->decorative_clouds },
                    { "settings.naturalPhysics",     &s->natural_physics },
                    { "settings.statusFeedback",     &s->status_feedback },
                };
                for (int i = 0; i < (int)(sizeof(flags) / sizeof(flags[0])); i++) {
                    if (ui_layout_row_checkbox(&L, tr(flags[i].key), tr("common.enable"), flags[i].v)) changed = true;
                }
                ui_layout_kv(&L, tr("settings.mirrorReflections"), tr("settings.comingSoon"));

                const char *veg_names[3] = {
                    tr("settings.vegetationForm.simple"),
                    tr("settings.vegetationForm.normal"),
                    tr("settings.vegetationForm.rich")
                };
                int vf_cur = qlcg_clampi((int)s->vegetation_form, 0, 2);
                ui_layout_select(&L, SEL_ID_VEG_FORM, tr("settings.vegetationForm"), veg_names, 3, vf_cur);

                {
                    int max_level = QLCG_MAX_VEGETATION_DENSITY;
                    int dv_idx = qlcg_clampi(s->dense_vegetation, 0, max_level);
                    VegetationDensity density = qlcg_vegetation_density(dv_idx);
                    char b[32];
                    snprintf(b, sizeof(b), "%d · %g× / %g×", dv_idx, density.understory, density.trees);
                    float dv = (float)dv_idx;
                    if (ui_layout_slider_steps(&L, tr("settings.denseVegetation"), &dv, 0.0f,
                                               (float)max_level, max_level + 1, 0.0f, NULL, b)) {
                        s->dense_vegetation = (int)dv;
                        changed = true;
                    }
                }
                ui_layout_note(&L, tr("settings.denseVegetation.hint"), true);
                ui_layout_indent_pop(&L);
            }
            g_app.set_fold_visual = open;

            ui_layout_divider(&L);

            {
                const char *res_names[7] = {
                    tr("settings.resolution.auto"),
                    "320 × 240", "640 × 480", "960 × 540",
                    "1280 × 720", "1920 × 1080", "2560 × 1440"
                };
                int cur = 0;
                for (int i = 0; i < 7; i++) {
                    if (strcmp(s->resolution, k_res_ids[i]) == 0) cur = i;
                }
                ui_layout_select(&L, SEL_ID_RES, tr("settings.resolution"), res_names, 7, cur);
            }

            {
                /* 六档 + 无上限：步进吸附，不做连续取值 */
                static const int k_fps_stops[7] = { 24, 30, 60, 90, 120, 180, 0 };
                int cur_idx = 6;
                if (s->frame_rate > 0) {
                    int best_diff = 999999;
                    for (int i = 0; i < 6; i++) {
                        int d = abs(s->frame_rate - k_fps_stops[i]);
                        if (d < best_diff) { best_diff = d; cur_idx = i; }
                    }
                }
                char b[48];
                if (k_fps_stops[cur_idx] <= 0) snprintf(b, sizeof(b), "%s", tr("settings.frameRate.unlimited"));
                else snprintf(b, sizeof(b), "%d fps", k_fps_stops[cur_idx]);
                float fv = (float)cur_idx;
                const char *FPS_LABELS[7] = {
                    "24", "30", "60", "90", "120", "180", tr("settings.frameRate.unlimited")
                };
                if (ui_layout_slider_steps(&L, tr("settings.frameRate"), &fv, 0.0f, 6.0f, 7, 0.0f,
                                           FPS_LABELS, b)) {
                    s->frame_rate = k_fps_stops[qlcg_clampi((int)(fv + 0.5f), 0, 6)];
                    changed = true;
                }
            }

            break;
        }
        case SET_TAB_CONTROLS: {
            /* 视角反转：鼠标、触屏与手柄共用 */
            if (ui_layout_checkbox(&L, tr("settings.invertLookX"), &s->invert_look_x)) changed = true;
            if (ui_layout_checkbox(&L, tr("settings.invertLookY"), &s->invert_look_y)) changed = true;
            ui_layout_note(&L, tr("settings.lookInvert.hint"), true);
            ui_layout_divider(&L);

            /* 触屏：开关+透明度，样式另有8种 */
            if (ui_layout_checkbox(&L, tr("settings.touch"), &s->touch_controls)) changed = true;
            {
                char tbuf[32];
                snprintf(tbuf, sizeof(tbuf), "%.0f%%", s->touch_opacity * 100.0f);
                if (ui_layout_slider(&L, tr("settings.touchOpacity"), &s->touch_opacity,
                                     0.1f, 1.0f, tbuf)) {
                    changed = true;
                }
            }
            ui_layout_note(&L, tr("settings.touch.hint"), true);

            /* 虚拟按键：8种样式 + 全屏拖动布局 */
            if (s->touch_controls) {
                {
                    static const char *STYLE_KEY[8] = {
                        "settings.touchStyle.jxqy",  "settings.touchStyle.bagua",
                        "settings.touchStyle.luopan","settings.touchStyle.lotus",
                        "settings.touchStyle.minimal","settings.touchStyle.classic",
                        "settings.touchStyle.cross", "settings.touchStyle.octagon"
                    };
                    const char *names[8];
                    for (int i = 0; i < 8; i++) names[i] = tr(STYLE_KEY[i]);
                    int cur = qlcg_clampi(s->joystick_style, 0, 7);
                    ui_layout_select(&L, SEL_ID_TOUCH_STYLE, tr("settings.touchStyle"),
                                     names, 8, cur);
                }
                {
                    float lpx = 12.0f * sc;
                    float row_h = qlcg_maxf(32.0f * z, lpx * 1.35f + 12.0f * z);
                    float ry = L.cursor;
                    bool hov = ui_hit(ui, ix, ry, iw, row_h);
                    if (hov) ui_draw_rect(ui, ix, ry, iw, row_h, tk_of(ui, QL_SURFACE_HI));
                    ui_draw_text_px(ui, UI_FACE_BODY, lpx, true, ix + 10.0f * z,
                                    ry + (row_h - lpx * 1.35f) * 0.5f,
                                    tr("settings.touchLayoutEdit"),
                                    hov ? tk_of(ui, QL_PRIMARY_TEXT) : tk_of(ui, QL_INK));
                    if (hov && ui->mouse_clicked) {
                        g_app.touch_layout_edit = true;
                        ui->input_blocked = true;
                        ui->drag_slider_id = -1;
                        ui->capture_action = -1;
                        ui->input_id = 0;
                        ui_pointer_consume(ui);
                        s_layout_drag = -1;
                        memcpy(s_layout_backup, s->touch_layout, sizeof(s_layout_backup));
                    }
                    L.cursor += row_h + 8.0f * z;
                }
                {
                    float lpx = 12.0f * sc;
                    float row_h = qlcg_maxf(32.0f * z, lpx * 1.35f + 12.0f * z);
                    float ry = L.cursor;
                    bool hov = ui_hit(ui, ix, ry, iw, row_h);
                    if (hov) ui_draw_rect(ui, ix, ry, iw, row_h, tk_of(ui, QL_SURFACE_HI));
                    ui_draw_text_px(ui, UI_FACE_BODY, lpx, false, ix + 10.0f * z,
                                    ry + (row_h - lpx * 1.35f) * 0.5f,
                                    tr("settings.touchLayoutReset"),
                                    hov ? tk_of(ui, QL_PRIMARY_TEXT) : tk_of(ui, QL_INK_2));
                    if (hov && ui->mouse_clicked) {
                        qlcg_touch_layout_default(s->touch_layout);
                        changed = true;
                    }
                    L.cursor += row_h + 8.0f * z;
                }
            }

            float kpx = 11.5f * sc;
            float klh = ui_line_height(UI_FACE_BODY, kpx);
            float row_h = qlcg_maxf(30.0f * z, klh + 10.0f * z);
            char kb[64], akey[24];
            for (int i = 0; i < ACT_COUNT; i++) {
                bool capturing = (ui->capture_action == i);
                float ry = L.cursor;
                bool hov = ui_hit(ui, ix, ry, iw, row_h);
                if (capturing) {
                    ui_draw_rect(ui, ix, ry, iw, row_h, tk_of(ui, QL_PRIMARY_SOFT));
                    ui_draw_rect(ui, ix, ry, 3.0f * z, row_h, tk_of(ui, QL_PRIMARY_UI));
                } else if (hov) {
                    ui_draw_rect(ui, ix, ry, iw, row_h, tk_of(ui, QL_SURFACE_HI));
                }
                float ty2 = ry + (row_h - klh) * 0.5f;
                if (capturing) {
                    snprintf(kb, sizeof(kb), "%s", tr("settings.pressKey"));
                } else {
                    qlcg_bind_key_label(s->binds[i].key, s->binds[i].mods, kb, sizeof(kb));
                }
                snprintf(akey, sizeof(akey), "action.%d", i);
                float kw = (float)ui_text_width_px(ui, UI_FACE_MONO, kpx, false, kb) + 16.0f * z;
                ui_draw_text_ellipsis(ui, UI_FACE_BODY, kpx, false, ix + 10.0f * z, ty2,
                                      qlcg_maxf(20.0f, iw - kw - 20.0f * z), 0,
                                      tr(akey), tk_of(ui, QL_INK_2));
                ui_draw_text_px_right(ui, UI_FACE_MONO, kpx, false, ix + iw - 10.0f * z, ty2,
                                      kb, capturing ? tk_of(ui, QL_PRIMARY_TEXT) : tk_of(ui, QL_INK));
                if (hov && ui->mouse_clicked) {
                    ui->capture_action = capturing ? -1 : i;
                }
                L.cursor += row_h + 2.0f * z;
            }
            ui_layout_gap(&L, 8.0f * z);
            const char *kr_lbl = tr("settings.keys.reset");
            float rbw = (float)ui_text_width_px(ui, UI_FACE_BODY, 11.5f * sc, false, kr_lbl) + 28.0f * z;
            float rbh = ui_ctl_height(28.0f * z, 11.5f * sc);
            if (ui_btn(ui, ix, L.cursor, qlcg_minf(iw, rbw), rbh, kr_lbl,
                       BTN_GHOST, false, 11.5f * sc, false)) {
                qlcg_binds_default(s->binds);
                changed = true;
            }
            L.cursor += rbh + L.row_gap;
            break;
        }
        case SET_TAB_METAPHYSICS: {
            /* 玄虚：只影响当前世界，随世界存档保存 */
            {
                /* 流速输入：缓冲跨帧保持，未聚焦时才从时钟同步 */
                static char spd_buf[32];
                if (ui->input_id != SEL_ID_SPEED) {
                    snprintf(spd_buf, sizeof(spd_buf), "%.1f", g_app.clock.time_speed);
                }
                if (ui_layout_text_input(&L, SEL_ID_SPEED, tr("settings.speed"),
                                         spd_buf, sizeof(spd_buf), 9, "72")) {
                    float v = (float)atof(spd_buf);
                    g_app.clock.time_speed = qlcg_clampf(v, 0.0f, 86400.0f);
                    g_app.clock.paused     = (g_app.clock.time_speed <= 0.001f);
                    changed = true;
                }
                ui_layout_note(&L, tr("settings.speed.hint"), true);
            }
            {
                char spd_labels[4][24];
                static const int k_spd_vals[4] = { 0, 1, 72, 360 };
                snprintf(spd_labels[0], sizeof(spd_labels[0]), "0");
                for (int i = 1; i < 4; i++) {
                    snprintf(spd_labels[i], sizeof(spd_labels[i]),
                             tr("settings.speed.presetFmt"), k_spd_vals[i]);
                }
                const char *presets[4] = {
                    spd_labels[0], spd_labels[1], spd_labels[2], spd_labels[3]
                };
                float v = g_app.clock.time_speed;
                int cur = (v < 0.01f) ? 0
                        : (fabsf(v - 1.0f)   < 0.01f) ? 1
                        : (fabsf(v - 72.0f)  < 0.01f) ? 2
                        : (fabsf(v - 360.0f) < 0.01f) ? 3 : -1;
                int picked = ui_layout_segments(&L, presets, 4, cur, 11.0f * sc);
                if (picked >= 0) {
                    g_app.clock.time_speed = (float)k_spd_vals[picked];
                    g_app.clock.paused     = (k_spd_vals[picked] <= 0);
                    changed = true;
                }
            }

            ui_layout_divider(&L);
            if (ui_layout_checkbox(&L, tr("settings.menuAutoPause"), &g_app.world_menu_auto_pause)) {
                changed = true;
            }
            if(g_app.world) {
                static bool expanded=false;
                UiRow row=ui_row_auto(&L,"",false,0,28.0f*sc);
                char title[160];snprintf(title,sizeof(title),"%s %s",expanded?"−":"+",tr("settings.astronomyBroadcast"));
                if(ui_button(ui,row.x,row.y,row.w,row.h,title,false,false))expanded=!expanded;
                if(expanded) {
                    const char *keys[]={"astra.category.meteor","astra.category.shower","astra.category.comet","astra.category.eclipse"};
                    const char *choices[]={tr("astra.policy.off"),tr("astra.policy.missed"),tr("astra.policy.visible"),tr("astra.policy.all")};
                    for(int i=0;i<4;i++) {
                        int selected=(g_app.world->astronomy_broadcast>>(i*2))&3;
                        int picked=ui_layout_labeled_segments(&L,tr(keys[i]),choices,4,selected);
                        if(picked>=0&&picked!=selected) {
                            g_app.world->astronomy_broadcast=(g_app.world->astronomy_broadcast&~(3u<<(i*2)))|((uint32_t)picked<<(i*2));
                            if(g_app.world->astra_events)g_app.world->astra_events->broadcast_ready=false;
                            changed=true;
                        }
                    }
                }
            }
            break;
        }
        default: break;
    }

    float total_h = ui_layout_end(&L) + 18.0f * z;
    ui_scroll_end(ui, 0x5E771Du, total_h);

    /* 底栏 */
    float fy = page.y + page.h - fbtn_h - 4.0f * z;
    ui_draw_rect(ui, page.x, fy - 8.0f * z, page.w, 1.0f, tk_of(ui, QL_LINE_SOFT));
    const char *s_apply = tr("settings.apply");
    const char *s_reset = tr("settings.reset");
    float aw  = (float)ui_text_width_px(ui, UI_FACE_BODY, fbtn_px, false, s_apply) + 32.0f * z;
    float rw2 = (float)ui_text_width_px(ui, UI_FACE_BODY, fbtn_px, false, s_reset) + 32.0f * z;
    /* 返回游戏按钮也要占位，否则说明文字会被它压住 */
    float back_w = 0.0f;
    if (g_app.settings_back_to_game) {
        back_w = (float)ui_text_width_px(ui, UI_FACE_BODY, fbtn_px, false,
                                         tr("game.pause.backToGame")) + 36.0f * z + 8.0f * z;
    }
    float note_px = 10.5f * sc;
    float note_lh = ui_line_height(UI_FACE_BODY, note_px);
    float note_avail = page.w - aw - rw2 - back_w - 20.0f * z;
    if (note_avail > 60.0f) {
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, note_px, false, page.x,
                              fy + (fbtn_h - note_lh) * 0.5f, note_avail, 0,
                              tr(world_active ? "settings.intro.world" : "settings.intro"), tk_of(ui, QL_INK_3));
    }
    if (ui_btn(ui, page.x + page.w - aw, fy, aw, fbtn_h, s_apply,
               BTN_PRIMARY, false, fbtn_px, false)) {
        float hd = ui->hidpi_mul > 0.0f ? ui->hidpi_mul : 1.0f;
        float cur_sx = ui->safe_x_pct;
        float cur_sy = ui->safe_y_pct;
        float cur_ts = (ui->user_scale > 0.0f ? ui->user_scale : 1.0f) / hd;
        float cur_us = (ui->user_size  > 0.0f ? ui->user_size  : 1.0f) / hd;
        bool need_confirm = (fabsf(s->safe_x - cur_sx) > 1e-3f)
                         || (fabsf(s->safe_y - cur_sy) > 1e-3f)
                         || (fabsf(s->text_scale - cur_ts) > 1e-3f)
                         || (fabsf(s->ui_scale - cur_us) > 1e-3f);
        app_apply_font();
        qlcg_i18n_set_locale(s->locale);
        if (need_confirm) {
            settings_confirm_begin(cur_sx, cur_sy, cur_ts, cur_us);
            app_sync_layout_axes();
            ql_metrics_build(&ui->met, ui->win_w, ui->win_h, ui->user_scale, ui->user_size, ui->hidpi_mul);
        } else {
            app_sync_layout_axes();
            qlcg_save_settings_disk(s);
            app_push_toast_k(UI_TOAST_SUCCESS, NULL, tr("settings.status.applied"));
        }
    }
    /* 从游戏抽屉进来的：与恢复默认并排，否则会困在设置页 */
    float reset_x = page.x + page.w - aw - rw2 - 8.0f * z;
    if (g_app.settings_back_to_game && back_w > 0.0f) {
        const char *back_lbl = tr("game.pause.backToGame");
        float bw = back_w - 8.0f * z;
        if (ui_btn(ui, reset_x - back_w, fy, bw, fbtn_h, back_lbl,
                   BTN_GHOST, false, fbtn_px, false)) {
            /* 返回：收起抽屉，菜单保持打开 */
            g_app.settings_in_drawer    = false;
            g_app.settings_back_to_game = false;
            g_app.esc_paused            = true;
            g_app.screen                = SCREEN_IN_GAME;
        }
    }

    if (ui_btn(ui, reset_x, fy, rw2, fbtn_h, s_reset,
               BTN_GHOST, false, fbtn_px, false)) {
        s_confirm.active = false;
        /* 语言属于使用习惯，恢复默认不重置 */
        qlcg_reset_settings_keep_locale(&g_app.settings);
        ui_apply_theme(ui, g_app.settings.theme);
        app_sync_layout_axes();
        app_apply_font();
        settings_apply_palette(ui);
        qlcg_save_settings_disk(&g_app.settings);
        app_push_toast_k(UI_TOAST_SUCCESS, NULL, tr("settings.status.resetDone"));
    }

    /* 下拉浮层分发 */
    {
        int picked = ui_select_popup(ui);
        if (picked >= 0) {
            if (ui->select_result_id == SEL_ID_FONT) {
                if (picked == 0) {
                    strncpy(s->font_id, "system", sizeof(s->font_id) - 1);
                } else if (picked - 1 < g_app.mod_fonts.count) {
                    strncpy(s->font_id, g_app.mod_fonts.items[picked - 1].id,
                            sizeof(s->font_id) - 1);
                }
                app_apply_font();
                changed = true;
            } else if (ui->select_result_id == SEL_ID_RES) {
                if (picked < 7) {
                    strncpy(s->resolution, k_res_ids[picked], sizeof(s->resolution) - 1);
                    changed = true;
                }
            } else if (ui->select_result_id == SEL_ID_PRESET) {
                if (picked < 6) {
                    qlcg_apply_graphics_preset(s, k_preset_vals[picked]);
                    changed = true;
                }
            } else if (ui->select_result_id == SEL_ID_HIDPI) {
                if (picked < 3) {
                    s->hidpi_scale = k_hidpi_vals[picked];
                    app_apply_hidpi_scale();
                    changed = true;
                }
            } else if (ui->select_result_id == SEL_ID_VEG_FORM) {
                if (picked < 3) {
                    s->vegetation_form = (VegetationForm)picked;
                    changed = true;
                }
            } else if (ui->select_result_id == SEL_ID_MEAS) {
                if (picked < 3) {
                    s->measurement_system = (MeasurementSystem)picked;
                    changed = true;
                }
            } else if (ui->select_result_id == SEL_ID_TOUCH_STYLE) {
                if (picked < 8) {
                    s->joystick_style = picked;
                    changed = true;
                }
            } else if (ui->select_result_id == SEL_ID_LANG) {
                if (picked < qlcg_i18n_count()) {
                    const QlLocaleMeta *m = qlcg_i18n_meta(picked);
                    strncpy(s->locale, m->code, sizeof(s->locale) - 1);
                    qlcg_i18n_set_locale(m->code);
                    changed = true;
                }
            }
        }
    }


    if (changed) {
        settings_apply_palette(ui);
    }

    ui->input_blocked = orig_blocked;
    if (s_confirm.active) {
        ui->select_id = 0;
        ui->drag_slider_id = -1;
        ui->mouse_clicked = had_confirm && orig_click;
        ui->mouse_down    = had_confirm && orig_down;
        ui->wheel_delta   = had_confirm ? orig_wheel : 0;
        draw_settings_confirm_modal(ui);
        ui_pointer_consume(ui);
    }

    /* 布局编辑器独占输入：先还原事件再画，遮罩盖住整页 */
    if (g_app.touch_layout_edit) {
        ui->select_id = 0;
        ui->mouse_clicked = had_editor && orig_click;
        ui->mouse_down    = had_editor && orig_down;
        ui->wheel_delta   = 0;
        draw_touch_layout_editor(ui);
        ui_pointer_consume(ui);
    }
}
