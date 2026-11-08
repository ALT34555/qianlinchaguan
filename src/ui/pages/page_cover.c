/* 封面页：书签栏、茶席、页框 */
#include "app/app_state.h"
#include "game/hud_layout.h"
#include "ui/ui_sprite.h"
#include "ui/ui_scroll.h"
#include "ui/ui_layout.h"

#define RAIL_LANGUAGE_SELECT 0x4C414E47u
#define RAIL_LANGUAGE_SCROLL 0x4C31304Eu

static bool draw_rail_icon_button(UiContext *ui, float cx, float cy,
                                  const SpriteDef *sp, bool active) {
    float z = ui->met.size;
    float sw = 30.0f * z, sh = 26.0f * z;
    float px0 = cx - sw * 0.5f;
    float py0 = cy - sh * 0.5f;
    bool hov = ui_hit(ui, px0, py0, sw, sh);

    ui_draw_rect(ui, px0, py0, sw, sh, tk_of(ui, QL_SURFACE_HI));
    ui_draw_border(ui, px0, py0, sw, sh, 1.0f,
                   (hov || active) ? tk_of(ui, QL_LINE_PRIMARY) : tk_of(ui, QL_LINE_SOFT));

    if (sp) {
        float isz = sh - 4.0f * z;
        ui_sprite_icon4(ui, sp, cx - isz * 0.5f, cy - isz * 0.5f, isz,
                        tk_of(ui, QL_SURFACE), tk_of(ui, QL_PRIMARY_UI),
                        tk_of(ui, QL_ACCENT_UI));
    }
    return hov && ui->mouse_clicked;
}

static void draw_theme_switch(UiContext *ui, float cx, float cy) {
    float z = ui->met.size;
    bool dark = (ui->theme == THEME_DARK);
    const SpriteDef *sp = ui_sprite_by_name(dark ? "nav-moon" : "nav-sun");
    bool clicked = draw_rail_icon_button(ui, cx, cy, sp, false);
    if (!sp) {
        float ir = 4.4f * z;
        if (dark) {
            ui_draw_circle(ui, cx, cy, ir, tk_of(ui, QL_INK_2));
            ui_draw_circle(ui, cx + ir * 0.62f, cy - ir * 0.44f, ir * 0.92f, tk_of(ui, QL_SURFACE_HI));
        } else {
            ui_draw_circle(ui, cx, cy, ir * 0.62f, tk_of(ui, QL_INK_2));
            for (int i = 0; i < 8; i++) {
                float a = (float)i * (QLCG_TAU / 8.0f);
                ui_draw_line(ui, cx + cosf(a) * ir * 1.02f, cy + sinf(a) * ir * 1.02f,
                             cx + cosf(a) * ir * 1.48f, cy + sinf(a) * ir * 1.48f,
                             1.3f * z, tk_of(ui, QL_INK_2));
            }
        }
    }
    if (clicked) {
        ThemeMode next = dark ? THEME_LIGHT : THEME_DARK;
        g_app.settings.theme = next;
        ui_apply_theme(ui, next);
        qlcg_save_settings_disk(&g_app.settings);
    }
}

#define COVER_RATIO_WIDE  1.25f
#define COVER_MIN_RAIL_W  860.0f

static float calc_rail_w(const UiContext *ui) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    if (g_app.rail_collapsed) return 54.0f * z;
    float want = qlcg_maxf(112.0f * z, 13.0f * sc * 4.2f + 26.0f * z);
    return qlcg_minf(want, (float)ui->win_w * 0.22f);
}

static bool cover_compact(const UiContext *ui) {
    float z = (ui->met.size > 0.0f) ? ui->met.size : 1.0f;
    float h = (ui->win_h > 0) ? (float)ui->win_h : 1.0f;
    if ((float)ui->win_w / h < COVER_RATIO_WIDE) return true;
    return ((float)ui->win_w < COVER_MIN_RAIL_W * z);
}

static void draw_language_switch(UiContext *ui, float cx, float cy) {
    bool open = ui->select_id == RAIL_LANGUAGE_SELECT;
    ui->select_input = open;
    bool clicked = draw_rail_icon_button(ui, cx, cy, ui_sprite_by_name("nav-language"), open);
    ui->select_input = false;

    if (clicked) {
        open = !open;
        ui->select_id = open ? RAIL_LANGUAGE_SELECT : 0;
        ui->select_count = 0;
        app_text_focus_clear();
        ui->capture_action = -1;
        ui->drag_slider_id = -1;
        ui_scroll_reset(ui, RAIL_LANGUAGE_SCROLL);
        ui_pointer_consume(ui);
    }
    if (open) {
        ui->select_x = cx;
        ui->select_y = cy;
    }
}

static bool draw_language_row(UiContext *ui, int i, float x, float y, float w, float h) {
    const QlLocaleMeta *locale = qlcg_i18n_meta(i);
    bool current = i == qlcg_i18n_get_index();
    bool hover = ui_hit(ui, x, y, w, h);
    if (current || hover) {
        ui_draw_rect(ui, x, y, w, h,
                     with_alpha(tk_of(ui, QL_PRIMARY), current ? 0.12f : 0.06f));
    }

    float z = ui->met.size;
    float px = 12.0f * ui->met.scale;
    float date_px = 10.0f * ui->met.scale;
    float right = x + w - 9.0f * z;
    float label_right = right;
    if (locale->modified_date[0]) {
        ui_draw_text_px_right(ui, UI_FACE_BODY, date_px, false, right,
                              y + (h - date_px * 1.35f) * 0.5f,
                              locale->modified_date, tk_of(ui, QL_INK_2));
        label_right -= (float)ui_text_width_px(ui, UI_FACE_BODY, date_px, false,
                                              locale->modified_date) + 8.0f * z;
    }
    ui_draw_text_ellipsis(ui, UI_FACE_BODY, px, false, x + 9.0f * z,
                          y + (h - px * 1.35f) * 0.5f, label_right - x - 9.0f * z, 0,
                          locale->label, current ? tk_of(ui, QL_PRIMARY_TEXT) : tk_of(ui, QL_INK));
    return hover && ui->mouse_clicked;
}

void render_rail_language_popup(UiContext *ui) {
    if (ui->select_id != RAIL_LANGUAGE_SELECT) return;

    float z = ui->met.size;
    float left = ui->safe_x_px + 8.0f * z;
    float top = ui->safe_y_px + 8.0f * z;
    float bottom = (float)ui->win_h - top;
    float w = qlcg_minf(qlcg_maxf(200.0f * z, 195.0f * ui->met.scale),
                        (float)ui->win_w - left * 2.0f);
    float x = qlcg_clampf(ui->select_x - w * 0.5f, left, (float)ui->win_w - left - w);
    float row_h = ui_ctl_height(28.0f * z, 12.0f * ui->met.scale);
    float builtin_h = 2.0f * row_h;
    float gap = 5.0f * z;
    bool compact = cover_compact(ui);
    float edge = ui->select_y + (compact ? 19.0f : -19.0f) * z;
    float space = compact ? bottom - edge : edge - top;
    int count = qlcg_i18n_count();
    float mod_h = qlcg_minf((float)(count - 2), 5.0f) * row_h;
    mod_h = qlcg_minf(mod_h, qlcg_maxf(row_h, space - builtin_h - gap));
    float h = builtin_h + (mod_h > 0.0f ? gap + mod_h : 0.0f);
    float y = qlcg_clampf(compact ? edge : edge - h, top, qlcg_maxf(top, bottom - h));

    ui->select_input = true;
    if (!ui_pointer_blocked(ui) && ui->mouse_clicked && !ui_hit(ui, x, y, w, h)) {
        ui->select_id = 0;
        ui_pointer_consume(ui);
        ui->select_input = false;
        return;
    }

    int picked = -1;
    ui_panel(ui, x, y, w, builtin_h, 5.0f * z, tk_of(ui, QL_SURFACE_HI),
             tk_of(ui, QL_LINE_SOFT), tk_of(ui, QL_SHADOW_SM));
    for (int i = 0; i < 2; ++i) {
        if (draw_language_row(ui, i, x + 1.0f, y + (float)i * row_h, w - 2.0f, row_h))
            picked = i;
    }

    if (mod_h > 0.0f) {
        float mod_y = y + builtin_h + gap;
        ui_panel(ui, x, mod_y, w, mod_h, 5.0f * z, tk_of(ui, QL_SURFACE_HI),
                 tk_of(ui, QL_LINE_SOFT), tk_of(ui, QL_SHADOW_SM));
        float off = ui_scroll_begin(ui, RAIL_LANGUAGE_SCROLL, x + 1.0f, mod_y, w - 2.0f, mod_h);
        for (int i = 2; i < count; ++i) {
            float ry = mod_y + (float)(i - 2) * row_h - off;
            if (draw_language_row(ui, i, x + 1.0f, ry, w - 7.0f * z, row_h))
                picked = i;
        }
        ui_scroll_end(ui, RAIL_LANGUAGE_SCROLL, (float)(count - 2) * row_h);
    }
    if (picked >= 0) {
        const char *code = qlcg_i18n_meta(picked)->code;
        qlcg_i18n_set_locale(code);
        snprintf(g_app.settings.locale, sizeof(g_app.settings.locale), "%s", code);
        qlcg_save_settings_disk(&g_app.settings);
        ui->select_id = 0;
        ui_pointer_consume(ui);
    }
    ui->select_input = false;
}

/* 左侧书签栏 */
static void draw_tea_rail(UiContext *ui) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float pad_x  = 14.0f * z + ui->safe_x_px;
    float rail_w = calc_rail_w(ui);
    float top    = 14.0f * z + ui->safe_y_px;
    float cx     = pad_x + rail_w * 0.5f;

    /* 品牌徽标：点击折叠 */
    float logo = 34.0f * z;
    float ly   = top + 4.0f * z;
    bool brand_hot = ui_hit(ui, pad_x, top, rail_w, logo + 8.0f * z);
    if (ui->has_teacup) {
        uint32_t tint = brand_hot ? ui_rgba(255, 255, 255, 255) : ui_rgba(255, 255, 255, 244);
        ui_draw_atlas_rect(ui, cx - logo * 0.5f, ly, logo, logo,
                           ui->cup_u0, ui->cup_v0, ui->cup_u1, ui->cup_v1, tint);
    } else {
        ui_draw_circle(ui, cx, ly + logo * 0.5f, logo * 0.42f, tk_of(ui, QL_PRIMARY_UI));
    }
    if (brand_hot && ui->mouse_clicked) g_app.rail_collapsed = !g_app.rail_collapsed;

    float sep_y;
    if (!g_app.rail_collapsed) {
        float bpx = 11.5f * sc;
        float label_y = ly + logo + 3.0f * z;
        ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, bpx, false, pad_x + 2.0f, label_y,
                              rail_w - 4.0f, 1, tr("app.name"), tk_of(ui, QL_INK));
        sep_y = label_y + ui_line_height(UI_FACE_DISPLAY, bpx) + 6.0f * z;
    } else {
        sep_y = ly + logo + 10.0f * z;
    }
    ui_draw_rect(ui, pad_x, sep_y, rail_w, 1.0f, tk_of(ui, QL_LINE_SOFT));

    /* 三项竖排页签：行高自适应，图标用sprite */
    {
        const char *LABEL[3] = { tr("nav.home"), tr("nav.saves"), tr("nav.settings") };
        static const char *ICON[3]  = { "nav-home", "nav-story", "nav-settings" };
        ScreenState target[3] = { SCREEN_START_HOME, SCREEN_MEMORIES_LOAD, SCREEN_SETTINGS };
        float text_px = 13.0f * sc;
        float row_h   = ui_ctl_height(30.0f * z, text_px);
        float ry0     = sep_y + 10.0f * z;
        for (int i = 0; i < 3; i++) {
            float ry  = ry0 + (float)i * row_h;
            bool  on  = ((int)g_app.rail_tab == i);
            bool  hot = ui_hit(ui, pad_x, ry, rail_w, row_h);
            if (on) {
                ui_draw_rect(ui, pad_x, ry, rail_w, row_h,
                             with_alpha(tk_of(ui, QL_PRIMARY), 0.10f));
                ui_draw_rect(ui, pad_x, ry, 3.0f * z, row_h, tk_of(ui, QL_PRIMARY_UI));
            } else if (hot) {
                ui_draw_rect(ui, pad_x, ry, rail_w, row_h,
                             with_alpha(tk_of(ui, QL_PRIMARY), 0.05f));
            }

            /* 图标：未选中整体压到 72%，免得和选中态抢注意力 */
            const SpriteDef *ic = ui_sprite_by_name(ICON[i]);
            float icon_sz = qlcg_minf(18.0f * z, row_h * 0.62f);
            float icon_x = g_app.rail_collapsed
                         ? pad_x + (rail_w - icon_sz) * 0.5f
                         : pad_x + (on ? 10.0f : 9.0f) * z;
            float icon_y = ry + (row_h - icon_sz) * 0.5f;
            if (ic) {
                float save_a = ui->global_alpha;
                if (!on) ui->global_alpha = save_a * 0.72f;
                ui_sprite_icon4(ui, ic, icon_x, icon_y, icon_sz,
                                tk_of(ui, QL_SURFACE), tk_of(ui, QL_PRIMARY_UI),
                                tk_of(ui, QL_ACCENT_UI));
                ui->global_alpha = save_a;
            }

            if (!g_app.rail_collapsed) {
                float tx = pad_x + (on ? 34.0f : 33.0f) * z;
                float tw = qlcg_maxf(8.0f, pad_x + rail_w - tx - 4.0f * z);
                ui_draw_text_ellipsis(ui, UI_FACE_BODY, text_px, on, tx,
                                      ry + (row_h - text_px * 1.35f) * 0.5f, tw, 0,
                                      LABEL[i], on ? tk_of(ui, QL_INK) : tk_of(ui, QL_INK_2));
            }
            if (hot && ui->mouse_clicked) {
                g_app.rail_tab = (RailTabKey)i;
                g_app.screen   = target[i];
                if (target[i] == SCREEN_MEMORIES_LOAD) {
                    g_app.save_count = qlcg_saves_scan(g_app.saves, MAX_SAVE_SLOTS);
                }
            }
        }
    }

    float theme_y = (float)ui->win_h - ui->safe_edges[3] - 27.0f * z;
    draw_language_switch(ui, cx, theme_y - 32.0f * z);
    draw_theme_switch(ui, cx, theme_y);
}

/* 顶部横排导航：紧凑布局用 */
static float draw_tea_rail_top(UiContext *ui) {
    float z = ui->met.size;
    float sc = ui->met.scale;
    float pad_x = 14.0f * z + ui->safe_x_px;
    float top = ui->safe_y_px + 8.0f * z;
    float tab_px = 12.5f * sc;
    float tab_h  = ui_ctl_height(30.0f * z, tab_px);
    float bar_h  = qlcg_maxf(40.0f * z, tab_h + 10.0f * z);

    /* 品牌：徽标 + 名字 */
    float logo = 26.0f * z;
    float ly = top + (bar_h - logo) * 0.5f;
    if (ui->has_teacup) {
        ui_draw_atlas_rect(ui, pad_x, ly, logo, logo,
                           ui->cup_u0, ui->cup_v0, ui->cup_u1, ui->cup_v1,
                           ui_rgba(255, 255, 255, 244));
    } else {
        ui_draw_circle(ui, pad_x + logo * 0.5f, ly + logo * 0.5f, logo * 0.4f,
                       tk_of(ui, QL_PRIMARY_UI));
    }

    const char *LABEL[3] = { tr("nav.home"), tr("nav.saves"), tr("nav.settings") };
    static const char *ICON[3]  = { "nav-home", "nav-story", "nav-settings" };
    ScreenState target[3] = { SCREEN_START_HOME, SCREEN_MEMORIES_LOAD, SCREEN_SETTINGS };
    float icon_sz = qlcg_minf(16.0f * z, tab_h * 0.5f);
    float gap     = 6.0f * z;
    float text_max = 0.0f;
    for (int i = 0; i < 3; i++) {
        text_max = qlcg_maxf(text_max,
                             (float)ui_text_width_px(ui, UI_FACE_BODY, tab_px, false, LABEL[i]));
    }
    float tab_w = text_max + icon_sz + gap + 24.0f * z;
    float tabs_avail = (float)ui->win_w - 2.0f * (pad_x + 68.0f * z);
    tab_w = qlcg_minf(tab_w, qlcg_maxf(0.0f, (tabs_avail - 12.0f * z) / 3.0f));
    float total = tab_w * 3.0f + 6.0f * z * 2.0f;
    float tx = (float)ui->win_w * 0.5f - total * 0.5f;
    float ty = top + (bar_h - tab_h) * 0.5f;

    float brand_px = 12.5f * sc;
    float brand_avail = tx - (pad_x + logo + 12.0f * z);
    if (brand_avail > 28.0f) {
        ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, brand_px, false, pad_x + logo + 7.0f * z,
                              top + (bar_h - brand_px * 1.35f) * 0.5f, brand_avail, 0,
                              tr("app.name"), tk_of(ui, QL_INK));
    }

    for (int i = 0; i < 3; i++) {
        bool on  = ((int)g_app.rail_tab == i);
        bool hot = ui_hit(ui, tx, ty, tab_w, tab_h);
        if (on) {
            ui_draw_rect(ui, tx, ty, tab_w, tab_h, with_alpha(tk_of(ui, QL_PRIMARY), 0.10f));
            ui_draw_rect(ui, tx + tab_w * 0.5f - 6.0f * z, ty + tab_h - 2.0f * z,
                         12.0f * z, 2.0f * z, tk_of(ui, QL_PRIMARY_UI));
        } else if (hot) {
            ui_draw_rect(ui, tx, ty, tab_w, tab_h, with_alpha(tk_of(ui, QL_PRIMARY), 0.05f));
        }
        /* 图标 + 文字作为一组左对齐，tab 宽度已含两侧留白 */
        float ix = tx + 12.0f * z;
        float iy = ty + (tab_h - icon_sz) * 0.5f;
        const SpriteDef *ic = ui_sprite_by_name(ICON[i]);
        if (ic) {
            float save_a = ui->global_alpha;
            if (!on) ui->global_alpha = save_a * 0.72f;
            ui_sprite_icon4(ui, ic, ix, iy, icon_sz,
                            tk_of(ui, QL_SURFACE), tk_of(ui, QL_PRIMARY_UI),
                            tk_of(ui, QL_ACCENT_UI));
            ui->global_alpha = save_a;
        }
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, tab_px, on, ix + icon_sz + gap,
                              ty + (tab_h - tab_px * 1.35f) * 0.5f,
                              qlcg_maxf(8.0f, tx + tab_w - (ix + icon_sz + gap) - 8.0f * z), 0,
                              LABEL[i], on ? tk_of(ui, QL_INK) : tk_of(ui, QL_INK_2));
        if (hot && ui->mouse_clicked) {
            g_app.rail_tab = (RailTabKey)i;
            g_app.screen   = target[i];
            if (target[i] == SCREEN_MEMORIES_LOAD) {
                g_app.save_count = qlcg_saves_scan(g_app.saves, MAX_SAVE_SLOTS);
            }
        }
        tx += tab_w + 6.0f * z;
    }

    float theme_x = (float)ui->win_w - pad_x - 15.0f * z;
    float button_y = top + bar_h * 0.5f;
    draw_language_switch(ui, theme_x - 34.0f * z, button_y);
    draw_theme_switch(ui, theme_x, button_y);
    return top + bar_h;
}

/* 通用页面外框：书签栏 + 卷面，返回内容区 */
UiRect draw_page_shell(UiContext *ui) {
    float z = ui->met.size;
    float pad_x = 14.0f * z + ui->safe_x_px;

    /* 设置抽屉：base.pause-settings */
    if (g_app.settings_in_drawer) {
        float ratio = 1.0f - 2.0f * ui->safe_x_pct * 0.01f;
        if (ratio < 0.2f) ratio = 0.2f;
        float w = qlcg_minf(880.0f * z, (float)ui->win_w) * ratio;
        float h = (float)ui->win_h - (ui->safe_edges[1]+ui->safe_edges[3]);
        float x = (float)ui->win_w - ui->safe_edges[2] - w;
        float y = ui->safe_y_px;
        /* 玻璃底+1px边，简化时半透明 */
        ui_draw_rect_glass(ui, x, y, w, h, tk_of(ui, QL_SURFACE_GLASS));
        ui_draw_border(ui, x, y, w, h, 1.0f, tk_of(ui, QL_LINE_SOFT));
        float pad_l = 24.0f * z;
        float pad_t = game_top_bar_height(ui) + 8.0f * z;
        float pad_b = 20.0f * z;
        /* 标题绝对定位在面板左上角 */
        ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY,
                              qlcg_clampf((float)ui->win_w * 0.023f, 20.0f, 27.0f) * ui->met.scale,
                              false, x + pad_l, y + 10.0f * z, w - pad_l * 2.0f, 0,
                              tr("settings.title"), tk_of(ui, QL_INK));
        UiRect r = { x + pad_l, y + pad_t, w - pad_l * 2.0f, h - pad_t - pad_b };
        return r;
    }

    float sheet_x, sheet_w, sheet_y, sheet_h;

    if (cover_compact(ui)) {
        sheet_x = pad_x;
        sheet_w = (float)ui->win_w - pad_x * 2.0f;
        sheet_y = draw_tea_rail_top(ui) + 8.0f * z;
        sheet_h = (float)ui->win_h - sheet_y - ui->safe_edges[3];
    } else {
        float rail_w = calc_rail_w(ui);
        float gap    = 14.0f * z;
        sheet_x = pad_x + rail_w + gap;
        sheet_w = (float)ui->win_w - sheet_x - pad_x;
        sheet_y = ui->safe_y_px;
        sheet_h = (float)ui->win_h - (ui->safe_edges[1]+ui->safe_edges[3]);
        draw_tea_rail(ui);
    }

    float panel_blur = 0.0f;
    if (ui->safe_y_px > 0.5f) {
        panel_blur = qlcg_clampf(ui->safe_y_px * 0.6f, 2.0f, 14.0f * z);
    }
    ui_panel(ui, sheet_x, sheet_y, sheet_w, sheet_h, panel_blur, tk_of(ui, QL_SURFACE),
             tk_of(ui, QL_LINE_SOFT), tk_of(ui, QL_SHADOW_LG));

    float pad = ui->met.pad_sheet;
    UiRect r = { sheet_x + pad, sheet_y + pad, sheet_w - pad * 2.0f, sheet_h - pad * 2.0f };
    return r;
}

/* 封面主页（自适应行高与滚动容错） */
void render_screen_home(UiContext *ui, float dt) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    UiRect page = draw_page_shell(ui);

    float sc_off = ui_scroll_begin(ui, 0x484F4D45u, page.x - 4.0f, page.y, page.w + 8.0f, page.h);
    float cx0  = page.x;
    float cy0  = page.y - sc_off;
    float cw   = page.w;
    float content_bottom = page.y + page.h;

    /* hero 两列：字号限制不超出左列宽度 */
    float hgap    = qlcg_clampf((float)ui->win_w * 0.016f, 12.0f, 24.0f);
    float left_w  = (cw - hgap) * 0.5676f;
    float right_w = cw - hgap - left_w;

    float h1     = qlcg_minf(29.0f * sc, qlcg_maxf(16.0f, left_w / 9.2f));
    float ty     = cy0 + qlcg_clampf((float)ui->win_h * 0.02f, 10.0f, 22.0f);
    float line_h = h1 * 1.22f;

    ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, h1, false, cx0, ty, left_w, 0, tr("home.title1"), tk_of(ui, QL_INK));
    float x2 = cx0;
    ui_draw_text_px(ui, UI_FACE_DISPLAY, h1, false, x2, ty + line_h, tr("home.title2"), tk_of(ui, QL_INK));
    x2 += (float)ui_text_width_px(ui, UI_FACE_DISPLAY, h1, false, tr("home.title2"));
    int hi_w = ui_text_width_px(ui, UI_FACE_DISPLAY, h1, false, tr("home.titleHi"));
    ui_draw_text_px(ui, UI_FACE_DISPLAY, h1, false, x2, ty + line_h, tr("home.titleHi"), tk_of(ui, QL_PRIMARY_TEXT));
    x2 += (float)hi_w;
    ui_draw_text_px(ui, UI_FACE_DISPLAY, h1, false, x2, ty + line_h, tr("home.title3"), tk_of(ui, QL_INK));

    float lead_px = 13.5f * sc;
    float lead_lh = lead_px * 1.7f;
    float lead_y  = ty + line_h * 2.0f + 12.0f * z;
    int lead_lines = ui_draw_text_wrapped(ui, UI_FACE_BODY, lead_px, false, cx0, lead_y,
                                          left_w, lead_lh, tr("home.lead"), tk_of(ui, QL_INK_2));

    float bfont   = 15.0f * sc;
    float btn_w   = qlcg_minf(qlcg_maxf(260.0f * z, bfont * 8.5f), left_w);
    float btn_h   = ui_ctl_height(48.0f * z, bfont);
    float btn_gap = 9.0f * z;
    float by = lead_y + lead_lh * (float)(lead_lines > 0 ? lead_lines : 1) + 14.0f * z;
    if (ui_btn(ui, cx0, by, btn_w, btn_h, tr("menu.create"), BTN_PRIMARY, true, bfont, false)) {
        g_app.rail_tab = RAIL_IDX_HOME;
        g_app.screen   = SCREEN_PLAY_MODE_SELECT;
    }
    by += btn_h + btn_gap;
    if (ui_btn(ui, cx0, by, btn_w, btn_h, tr("menu.read"), BTN_GHOST, true, bfont, false)) {
        g_app.rail_tab   = RAIL_IDX_SAVES;
        g_app.screen     = SCREEN_MEMORIES_LOAD;
        g_app.save_count = qlcg_saves_scan(g_app.saves, MAX_SAVE_SLOTS);
    }
    by += btn_h + btn_gap;
    if (ui_btn(ui, cx0, by, btn_w, btn_h, tr("menu.settings"), BTN_QUIET, true, bfont, false)) {
        g_app.rail_tab = RAIL_IDX_SETTINGS;
        g_app.screen   = SCREEN_SETTINGS;
    }
    by += btn_h;

    float hero_h = qlcg_maxf(qlcg_minf((float)ui->win_h * 0.32f, 290.0f * z), (by - cy0) + 14.0f * z);

    /* 右列：茶盏舞台 */
    float cup = qlcg_minf(390.0f * z, right_w);
    float cup_x = cx0 + left_w + hgap + (right_w - cup) * 0.5f;
    float cup_y = cy0 + (hero_h - cup) * 0.5f;
    float cup_cx = cup_x + cup * 0.5f;
    float cup_cy = cup_y + cup * 0.5f;

    for (int i = 10; i >= 1; i--) {
        float t = (float)i / 10.0f;
        ui_draw_circle(ui, cup_cx, cup_cy, cup * 0.33f * (0.4f + t * 1.1f),
                       with_alpha(tk_of(ui, QL_PRIMARY), 0.030f * (1.0f - t * 0.85f)));
    }
    if (ui->has_teacup) {
        ui_draw_atlas_rect(ui, cup_x, cup_y, cup, cup,
                           ui->cup_u0, ui->cup_v0, ui->cup_u1, ui->cup_v1, ui_rgba(255, 255, 255, 255));
    } else {
        ui_draw_circle(ui, cup_cx, cup_y + cup * 0.62f, cup * 0.3f, tk_of(ui, QL_CELADON));
        ui_draw_circle(ui, cup_cx, cup_y + cup * 0.58f, cup * 0.24f, tk_of(ui, QL_TEA));
    }
    ui_update_and_draw_petals(ui, dt, cup_x - cup * 0.08f, cup_y - cup * 0.08f, cup * 1.16f, cup * 1.16f);

    /* 统计横带 */
    float meta_y = cy0 + hero_h;
    ui_draw_rect(ui, cx0, meta_y, cw, 1.0f, tk_of(ui, QL_LINE_FAINT));
    float ns = 15.0f * sc, ls = 9.5f * sc;
    float y1 = meta_y + 7.0f * z;
    float y2 = y1 + ns * 1.25f;
    float meta_bottom = y2 + ls * 1.5f + 8.0f * z;
    {
        char count_buf[32];
        snprintf(count_buf, sizeof(count_buf), "%d", g_app.save_count);
        float gap2 = 22.0f * z;
        float rx = cx0 + cw;
        struct { const char *val, *lab; uint32_t vc; } cols[4] = {
            { count_buf,                tr("home.metaSaves"), tk_of(ui, QL_INK) },
            { "2",                      tr("home.metaModes"), tk_of(ui, QL_INK) },
            { "2",                      tr("home.metaViews"), tk_of(ui, QL_INK) },
            { g_app.settings.ren_ming,  tr("profile.name"),   tk_of(ui, QL_PRIMARY_TEXT) }
        };
        for (int i = 3; i >= 0; i--) {
            float vw = (float)ui_text_width_px(ui, UI_FACE_DISPLAY, ns, false, cols[i].val);
            float lw = (float)ui_text_width_px(ui, UI_FACE_BODY, ls, false, cols[i].lab);
            float wm = vw > lw ? vw : lw;
            if (rx - wm < cx0) break;
            ui_draw_text_px_right(ui, UI_FACE_DISPLAY, ns, false, rx, y1, cols[i].val, cols[i].vc);
            ui_draw_text_px_right(ui, UI_FACE_BODY, ls, false, rx, y2, cols[i].lab, tk_of(ui, QL_INK_3));
            rx -= wm + gap2;
        }
    }

    /* 继续上次 */
    bool has_save = (g_app.save_count > 0 && g_app.saves[0].valid);
    float card_h  = qlcg_maxf(ui_ctl_height(28.0f * z, 11.5f * sc) + 22.0f * z,
                              11.0f * z * 2.0f + 13.5f * sc * 1.62f + 4.0f * z + 10.0f * sc * 1.62f);
    float empty_h = 22.0f * z * 2.0f + 34.0f * z + 10.0f * z + 2.0f * 11.5f * sc * 1.6f;
    float block_h = has_save ? card_h : empty_h;

    float h2    = 18.0f * sc;
    float h2_lh = ui_line_height(UI_FACE_DISPLAY, h2);
    float all_h = ui_ctl_height(28.0f * z, 11.5f * sc);
    float sec_h = qlcg_maxf(h2_lh, all_h) + 12.0f * z;

    float natural_head_y = (content_bottom - sc_off) - block_h - sec_h;
    float head_y = qlcg_maxf(meta_bottom + 14.0f * z, natural_head_y);
    float card_y = head_y + sec_h;

    float hicon = 13.0f * z;
    {
        const SpriteDef *flag = ui_sprite_by_name("flag-pin");
        if (flag) {
            ui_sprite_icon4(ui, flag, cx0, head_y + (h2_lh - hicon) * 0.5f, hicon,
                            tk_of(ui, QL_SURFACE), tk_of(ui, QL_PRIMARY_UI),
                            tk_of(ui, QL_ACCENT_UI));
        }
    }
    ui_draw_text_px(ui, UI_FACE_DISPLAY, h2, false, cx0 + hicon + 7.0f * z, head_y,
                    tr("home.continue"), tk_of(ui, QL_INK));
    const char *s_all = tr("home.allSaves");
    float all_w = (float)ui_text_width_px(ui, UI_FACE_BODY, 11.5f * sc, false, s_all) + 22.0f * z;
    if (ui_btn(ui, cx0 + cw - all_w, head_y + (h2_lh - all_h) * 0.5f, all_w, all_h, s_all,
               BTN_QUIET, false, 11.5f * sc, false)) {
        g_app.rail_tab   = RAIL_IDX_SAVES;
        g_app.screen     = SCREEN_MEMORIES_LOAD;
        g_app.save_count = qlcg_saves_scan(g_app.saves, MAX_SAVE_SLOTS);
    }
    ui_draw_rect(ui, cx0, head_y + qlcg_maxf(h2_lh, all_h) + 4.0f * z, cw, 1.0f, tk_of(ui, QL_LINE_SOFT));

    if (has_save) {
        const SaveFileInfo *last = &g_app.saves[0];
        int last_slot = last->newest_slot;
        ui_panel(ui, cx0, card_y, cw, card_h, 5.0f, tk_of(ui, QL_SURFACE),
                 tk_of(ui, QL_LINE_FAINT), tk_of(ui, QL_SHADOW_SM));
        const char *s_load = tr("saves.loadBtn");
        float rw2 = (float)ui_text_width_px(ui, UI_FACE_BODY, 11.5f * sc, false, s_load) + 26.0f * z;
        float rh2 = ui_ctl_height(28.0f * z, 11.5f * sc);
        float text_avail = qlcg_maxf(40.0f, cw - rw2 - 34.0f * z);

        float ix = cx0 + 13.0f * z;
        float iy = card_y + 11.0f * z;
        float name_px = 13.5f * sc;
        ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, name_px, false, ix, iy, text_avail * 0.55f, 0,
                              last->world_name, tk_of(ui, QL_INK));
        float nw = qlcg_minf(text_avail * 0.55f,
                             (float)ui_text_width_px(ui, UI_FACE_DISPLAY, name_px, false, last->world_name));
        float nx = ix + nw + 7.0f * z;
        const char *tag = (last->play_mode == PLAY_MODE_SURVIVAL) ? tr("mode.survival") : tr("mode.leisure");
        float tag_px = 10.5f * sc;
        float tag_w = (float)ui_text_width_px(ui, UI_FACE_BODY, tag_px, false, tag) + 10.0f * z;
        if (nx + tag_w < ix + text_avail) {
            ui_draw_border(ui, nx, iy + 2.0f * z, tag_w, tag_px + 6.0f * z, 1.0f,
                           with_alpha(tk_of(ui, QL_ACCENT), 0.40f));
            ui_draw_text_px_center(ui, UI_FACE_BODY, tag_px, false, nx + tag_w * 0.5f, iy + 4.0f * z,
                                   tag, tk_of(ui, QL_ACCENT_TEXT));
            float px_x = nx + tag_w + 8.0f * z;
            if (px_x + 20.0f < ix + text_avail) {
                ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, 13.0f * sc, false, px_x, iy,
                                      ix + text_avail - px_x, 0,
                                      g_app.settings.ren_ming, tk_of(ui, QL_PRIMARY_TEXT));
            }
        }

        char meta[192];
        char slot_lab[32];
        app_slot_kind_label(last_slot, slot_lab, sizeof(slot_lab));
        snprintf(meta, sizeof(meta), "%s %u · %s · %s %s", tr("creator.seedShort"), last->seed,
                 qlcg_calendar_mode_name(last->calendar_mode), slot_lab, last->saved_at_str);
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, 10.0f * sc, false, ix,
                              iy + name_px * 1.62f + 3.0f * z, text_avail, 0,
                              meta, tk_of(ui, QL_INK_3));

        if (ui_btn(ui, cx0 + cw - 13.0f * z - rw2, card_y + (card_h - rh2) * 0.5f,
                   rw2, rh2, s_load, BTN_PRIMARY, false, 11.5f * sc, false)) {
            app_load_from_save_file(last, last_slot);
        }
    } else {
        float ex = cx0 + cw * 0.5f;
        float ey = card_y + 22.0f * z;
        {
            const SpriteDef *flower = ui_sprite_by_name("decor-flower");
            if (flower) {
                ui_sprite_icon(ui, flower, ex - 15.0f * z, ey, 30.0f * z,
                               ui_rgba(255, 255, 255, 150));
            }
        }
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, 11.5f * sc, false, cx0, ey + 38.0f * z, cw, 1,
                              tr("home.noSaves"), tk_of(ui, QL_INK_3));
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, 11.5f * sc, false, cx0,
                              ey + 38.0f * z + 11.5f * sc * 1.6f, cw, 1,
                              tr("home.noSavesHint"), tk_of(ui, QL_INK_3));
    }

    ui_scroll_end(ui, 0x484F4D45u, (card_y + block_h + sc_off) - page.y);
}
