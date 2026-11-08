/* 游戏内顶栏：玻璃条 + 右上菜单钮（对齐 base） */
#include "app/app_state.h"
#include "hud_layout.h"
#include "hud_glass.h"
#include "ui/ui_sprite.h"

/* base line-height 与字距 */
#define BAR_TRACK_NAME 0.18f
#define BAR_TRACK_WHO  0.10f
#define BAR_TRACK_DATE 0.10f

/* UTF-8 字形步长 */
static int bar_utf8_step(const char *p) {
    unsigned char c = (unsigned char)*p;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/* 带字距的文本宽度：每字后加 track */
static float bar_text_width(UiContext *ui, UiFaceKind face, float px, bool bold,
                            const char *text, float track) {
    float w = 0.0f;
    int n = 0;
    for (const char *p = text; *p; p += bar_utf8_step(p)) {
        char g[5] = {0};
        memcpy(g, p, (size_t)bar_utf8_step(p));
        w += (float)ui_text_width_px(ui, face, px, bold, g);
        n++;
    }
    return w + track * (float)n;
}

/* 带字距绘制，超出 max_w 时补省略号 */
static void bar_draw_text(UiContext *ui, UiFaceKind face, float px, bool bold,
                          float x, float y, float max_w, const char *text,
                          uint32_t color, float track) {
    if (!text || !text[0]) return;
    float cx = x;
    for (const char *p = text; *p; p += bar_utf8_step(p)) {
        int step = bar_utf8_step(p);
        char g[5] = {0};
        memcpy(g, p, (size_t)step);
        float gw = (float)ui_text_width_px(ui, face, px, bold, g);
        if (cx - x + gw + track > max_w) {
            ui_draw_text_px(ui, face, px, bold, cx, y, "\xE2\x80\xA6", color);
            return;
        }
        ui_draw_text_px(ui, face, px, bold, cx, y, g, color);
        cx += gw + track;
    }
}

/* 顶栏可见：舆图常显，山川看设置 */
static bool top_bar_visible(void) {
    if (!g_app.world) return false;
    if (g_app.view_mode == VIEW_MODE_ATLAS) return true;
    return g_app.settings.show_top_bar || g_app.esc_paused;
}

/* base：min-height 52 与 40+18 取大 */
float game_top_bar_height(UiContext *ui) {
    if (!top_bar_visible()) return 0.0f;
    float z = ui->met.size;
    return qlcg_maxf(52.0f * z, 40.0f * z + 18.0f * z) + 1.0f * z;
}

/* 菜单图标：三横线 + 中间主色短线 */
static void draw_menu_glyph(UiContext *ui, float cx, float cy, float size, uint32_t ink, uint32_t accent) {
    float k = size / 24.0f;
    float gx = cx - 12.0f * k;
    float th = 1.6f * k;
    ui_draw_rect(ui, gx,             cy - 5.5f * k - th * 0.5f, 18.0f * k, th, ink);
    ui_draw_rect(ui, gx,             cy            - th * 0.5f, 13.0f * k, th, ink);
    ui_draw_rect(ui, gx,             cy + 5.5f * k - th * 0.5f, 18.0f * k, th, ink);
    ui_draw_rect(ui, gx + 13.0f * k, cy            - 1.1f * k,  5.0f * k, 2.2f * k, accent);
}

/* 钮位：舆图嵌顶栏，山川悬浮右上 */
static void menu_btn_rect(UiContext *ui, float *bx, float *by, float *bs) {
    float z     = ui->met.size;
    float s     = 40.0f * z;
    bool  atlas = g_app.view_mode == VIEW_MODE_ATLAS;
    float right = (atlas ? 20.0f : 12.0f) * z;   /* base 顶栏右内边距 20 / 悬浮距右 12 */
    *bx = (float)ui->win_w - ui->safe_edges[2] - right - s;
    *bs = s;
    /* 舆图居中，山川距上 10 */
    *by = atlas ? ui->safe_y_px + (game_top_bar_height(ui) - s) * 0.5f
                : ui->safe_y_px + 10.0f * z;
}

void game_top_bar(UiContext *ui) {
    if (!top_bar_visible()) return;

    float z  = ui->met.size;
    float sc = ui->met.scale;
    float sx = ui->safe_x_px;
    float sy = ui->safe_y_px;

    float bar_h = game_top_bar_height(ui);
    float bar_x = sx;
    float bar_y = sy;
    float bar_w = (float)ui->win_w - (ui->safe_edges[0]+ui->safe_edges[2]);

    /* 玻璃：与 ESC 抽屉同料，日夜随令牌 */
    hud_glass_strip(ui, bar_x, bar_y, bar_w, bar_h);

    /* 右上角菜单钮，先算出来给左侧文字留位 */
    float btn_x, btn_y, btn_sz;
    menu_btn_rect(ui, &btn_x, &btn_y, &btn_sz);
    float text_end = btn_x - 12.0f * z;

    /* 字号：标题15 / 人名13 / 日历10 */
    float name_px = 15.0f * sc;
    float who_px  = 13.0f * sc;
    float date_px = 10.0f * sc;
    float cy      = bar_y + bar_h * 0.5f;

    char cal_bar[192];
    qlcg_calendar_format_topbar(&g_app.snap, g_app.clock.mode, cal_bar, sizeof(cal_bar));

    float x = bar_x + 20.0f * z;
    const char *title = g_app.loaded_slot_name[0] ? g_app.loaded_slot_name : g_app.world->opt.world_name;
    char plane_title[320];
    if(qlcg_world_flat(g_app.world)) {
        snprintf(plane_title,sizeof(plane_title),"%s [%d, %d]",title,g_app.world->opt.plane_x,g_app.world->opt.plane_z);
        title=plane_title;
    }
    const char *who   = g_app.world->opt.ren_ming;
    float gap = 14.0f * z;

    float trk_name = name_px * BAR_TRACK_NAME;
    float trk_who  = who_px  * BAR_TRACK_WHO;
    float trk_date = date_px * BAR_TRACK_DATE;

    /* 按比例分配宽度，长名字只吃省略号，不挤后续 */
    float total    = qlcg_maxf(0.0f, text_end - x);
    float name_cap = total * 0.50f;
    float who_cap  = total * 0.30f;

    float w_name = bar_text_width(ui, UI_FACE_DISPLAY, name_px, false, title, trk_name);
    float draw_n = qlcg_minf(w_name, name_cap);
    bar_draw_text(ui, UI_FACE_DISPLAY, name_px, false, x, cy - name_px * 0.66f,
                  draw_n, title, tk_of(ui, QL_INK), trk_name);
    x += draw_n + gap;

    if (text_end - x > 40.0f * z) {
        float w_who  = bar_text_width(ui, UI_FACE_DISPLAY, who_px, false, who, trk_who);
        float draw_w = qlcg_minf(w_who, who_cap);
        /* 人名取偏好主色，纸面上不够亮则压深 */
        bar_draw_text(ui, UI_FACE_DISPLAY, who_px, false, x, cy - who_px * 0.66f,
                      draw_w, who, hud_ink_name(ui, g_app.settings.profile.main_color, 235),
                      trk_who);
        x += draw_w + gap;
    }
    if (text_end - x > 60.0f * z) {
        bar_draw_text(ui, UI_FACE_MONO, date_px, false, x, cy - date_px * 0.70f,
                      text_end - x, cal_bar, with_alpha(tk_of(ui, QL_INK_2), 0.80f), trk_date);
    }

    /* 钮由 game_menu_button 单独画 */
    (void)btn_y; (void)btn_sz;
}

/* 自动存档图标：右上角直接浮现，无底盘无描边 */
void game_save_indicator(UiContext *ui) {
    if (!g_app.world || g_app.auto_save_flash <= 0.0f) return;
    if (g_app.screen != SCREEN_IN_GAME) return;
    const SpriteDef *sp = ui_sprite_by_name("sys-save");
    if (!sp) return;
    float z = ui->met.size;
    float bx, by, bs;
    menu_btn_rect(ui, &bx, &by, &bs);
    float size = 26.0f * z;
    float x = bx - size - 10.0f * z;
    float y = by + (bs - size) * 0.5f;
    float a = 1.0f;
    if (!ui->reduce_motion && g_app.auto_save_flash < 0.6f)
        a = g_app.auto_save_flash / 0.6f;
    float save_a = ui->global_alpha;
    ui->global_alpha = save_a * a;
    ui_sprite_icon4(ui, sp, x, y, size,
                    ui_rgba(255, 255, 255, 235), ui_rgba(255, 255, 255, 235),
                    ui_rgba(255, 255, 255, 235));
    ui->global_alpha = save_a;
}

/* 静置计时：30 秒后淡到 0.3 */
static bool  s_resting;
static float s_rest_since;

/* 悬停即复位，其余恒为 1 */
static float menu_btn_alpha(UiContext *ui, bool hov) {
    bool resting = g_app.view_mode == VIEW_MODE_TERRAIN && !g_app.esc_paused;
    if (resting && !s_resting) s_rest_since = ui->time;   /* 重新进入常驻态即重新计时 */
    s_resting = resting;
    if (!resting) return 1.0f;
    if (hov) { s_rest_since = ui->time; return 1.0f; }    /* base 悬停取消动画 */
    float e = ui->time - s_rest_since;
    if (e <= 30.0f) return 1.0f;
    float k = (e - 30.0f) / 0.25f;                        /* transition .25s ease */
    return k >= 1.0f ? 0.3f : 1.0f - 0.7f * k;
}

/* 常驻最上层，点它开关菜单 */
void game_menu_button(UiContext *ui) {
    static bool first_click;
    if (!g_app.world) { first_click = false; return; }

    float z  = ui->met.size;
    float bx, by, bs;
    menu_btn_rect(ui, &bx, &by, &bs);
    bool hov   = ui_hit(ui, bx, by, bs, bs);
    if (!hov) first_click = false;
    bool open  = g_app.esc_paused;
    bool atlas = g_app.view_mode == VIEW_MODE_ATLAS;

    float save_a = ui->global_alpha;
    ui->global_alpha = save_a * menu_btn_alpha(ui, hov);

    if (atlas) {
        /* 舆图 quiet 钮：仅悬停垫色 */
        if (hov) ui_draw_rect(ui, bx, by, bs, bs, tk_of(ui, QL_ACCENT_SOFT));
    } else {
        /* 山川：HUD 玻璃 + 暖边 + 高光 */
        ui_draw_rect_glass(ui, bx, by, bs, bs, tk_of(ui, QL_AERO_TINT));
        if (hov) ui_draw_rect(ui, bx, by, bs, bs, with_alpha(tk_of(ui, QL_PRIMARY), 0.66f));
        ui_draw_border(ui, bx, by, bs, bs, 1.0f, tk_of(ui, QL_LINE_SOFT));
        ui_draw_rect(ui, bx + 1.0f, by + 1.0f, bs - 2.0f, 1.0f, tk_of(ui, QL_AERO_LINE));
    }

    const SpriteDef *sp = ui_sprite_by_name(open ? "menu-open" : "menu");
    if (sp) {
        /* L1 点缀，L2 展开染装饰色 */
        ui_sprite_icon4(ui, sp, bx + 8.0f * z, by + 8.0f * z, bs - 16.0f * z,
                        tk_of(ui, QL_PRIMARY_SOFT),
                        tk_of(ui, QL_PRIMARY_UI),
                        open ? tk_of(ui, QL_ACCENT_UI) : tk_of(ui, QL_PRIMARY_UI));
    } else {
        draw_menu_glyph(ui, bx + bs * 0.5f, by + bs * 0.5f, bs * 0.42f,
                        open ? tk_of(ui, QL_ACCENT_UI) : tk_of(ui, QL_INK),
                        tk_of(ui, QL_PRIMARY_UI));
    }

    ui->global_alpha = save_a;

    if (hov && ui->mouse_clicked) {
        bool quick_save = first_click && ui->mouse_dblclicked;
        first_click = !ui->mouse_dblclicked;
        g_app.esc_paused = !g_app.esc_paused;
        if (!g_app.esc_paused) g_app.settings_in_drawer = false;
        if (quick_save) app_quick_save();
    }
}
