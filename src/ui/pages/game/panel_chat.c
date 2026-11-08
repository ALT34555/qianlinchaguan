/* 游戏内聊天：气泡态只读历史条，展开态左侧抽屉 */
#include "app/app_state.h"
#include "core/dev_tools.h"
#include "hud_layout.h"
#include "hud_glass.h"
#include "view_atlas_layers.h"
#include "ui/ui_sprite.h"
#include "ui/ui_scroll.h"

/* UTF-8 字节常量，避开源码中文字面量 */
#define UTF8_LBRACKET0  ((char)0xE3)
#define UTF8_LBRACKET1  ((char)0x80)
#define UTF8_LBRACKET2  ((char)0x90)   /* 【 */
#define UTF8_RBRACKET2  ((char)0x91)   /* 】 */

/* 拆昵称两段着色，draw 为假时只测量 */
static int draw_chat_line(UiContext *ui, float x, float y, const char *line, float px,
                          float max_w, float line_h, bool draw) {
    if (!line || !line[0]) return 0;

    bool has_tag = (line[0] == UTF8_LBRACKET0 && line[1] == UTF8_LBRACKET1 &&
                    line[2] == UTF8_LBRACKET2);
    const char *end = NULL;
    if (has_tag) {
        size_t len = strlen(line);
        for (size_t i = 3; i + 2 < len; i++) {
            if (line[i] == UTF8_LBRACKET0 && line[i + 1] == UTF8_LBRACKET1 &&
                line[i + 2] == UTF8_RBRACKET2) {
                end = line + i;
                break;
            }
        }
    }

    if (has_tag && end) {
        int nlen = (int)(end - (line + 3));
        if (nlen > 0 && nlen < 191) {
            char name[196];
            memcpy(name, line + 3, (size_t)nlen);
            /* 补 UTF-8 全角冒号 EF BC 9A，避开中文字面量 */
            name[nlen]     = (char)0xEF;
            name[nlen + 1] = (char)0xBC;
            name[nlen + 2] = (char)0x9A;
            name[nlen + 3] = '\0';
            if (draw) {
                ui_draw_text_px(ui, UI_FACE_BODY, px, true, x, y, name,
                                tk_of(ui, QL_PRIMARY_UI));
            }
            float nx = x + (float)ui_text_width_px(ui, UI_FACE_BODY, px, true, name);
            float seg_w = qlcg_maxf(max_w - (nx - x), 24.0f);
            int n = draw ? ui_draw_text_wrapped(ui, UI_FACE_BODY, px, false, nx, y, seg_w,
                                                line_h, end + 3,
                                                tk_of(ui, QL_INK_2))
                         : ui_measure_text_wrapped(ui, UI_FACE_BODY, px, false, seg_w,
                                                   end + 3);
            return (n > 0) ? n : 1;
        }
    }

    int n = draw ? ui_draw_text_wrapped(ui, UI_FACE_BODY, px, false, x, y, max_w, line_h,
                                        line, tk_of(ui, QL_INK_3))
                 : ui_measure_text_wrapped(ui, UI_FACE_BODY, px, false, max_w, line);
    return (n > 0) ? n : 1;
}

/* 输入条：整条可点聚焦，返回占用高度 */
static float chat_composer(UiContext *ui, float x, float y, float w, float h, float px, bool focused) {
    float z   = ui->met.size;
    float pad = 16.0f * z;
    float icon_sz = px * 1.25f;
    float right_reserve = icon_sz + 12.0f * z;
    float max_w = qlcg_maxf(12.0f, w - pad - right_reserve);
    float line_h = px * 1.55f;
    float box_h  = qlcg_maxf(h, line_h + 14.0f * z);
    float icon_x = x + w - right_reserve + 4.0f * z;
    float icon_y = y + (box_h - icon_sz) * 0.5f;

    if (g_app.chat_open) ui->chat_seen_frame = ui->frame_idx;

    QLCG_DEV_ONLY({
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            QLCG_DEV_INPUT_LOG("[in] sendbtn %.0f,%.0f %.0fx%.0f\n", icon_x, icon_y, icon_sz, icon_sz);
        }
    })

    bool hit      = ui_hit(ui, x, y, w, box_h);
    bool send_hit = ui_hit(ui, icon_x, icon_y, icon_sz, icon_sz);
    bool send_clk = send_hit && ui->mouse_clicked;

    bool just_opened = false;
    if (hit && ui->mouse_clicked && !focused) {
        g_app.chat_open = true;
        focused = true;
        just_opened = true;
        ui->chat_seen_frame = ui->frame_idx;
    }
    bool do_send = send_clk && focused && !just_opened;

    UiEditField f = { g_app.chat_input, sizeof(g_app.chat_input),
                      QLCG_CHAT_MAX_CHARS, &ui->edit_chat };
    if (focused && !do_send) {
        float tx = x + pad;
        if (hit && !send_hit && (ui->mouse_dblclicked || ui->mouse_clicked)) {
            ui->ime_request_frame = ui->frame_idx;
            if (ui->mouse_dblclicked) {
                ui_edit_select_all(&f);
            } else {
                ui->edit_chat.caret = ui_edit_hit_line(ui, &f, max_w, UI_FACE_BODY, px, false,
                                                       (float)ui->mouse_x - tx);
                ui->edit_chat.anchor   = ui->edit_chat.caret;
                ui->edit_chat.dragging = true;
            }
        } else if (ui->mouse_down && ui->edit_chat.dragging) {
            ui->edit_chat.caret = ui_edit_hit_line(ui, &f, max_w, UI_FACE_BODY, px, false,
                                                   (float)ui->mouse_x - tx);
        }
        if (!ui->mouse_down) ui->edit_chat.dragging = false;
    }

    uint32_t col_sel = (tk_of(ui, QL_PRIMARY_UI) & 0x00FFFFFFu) | (0x38u << 24);
    ui_edit_draw_line(ui, &f, x + pad, y, max_w, box_h, UI_FACE_BODY, px, false,
                      focused && !do_send, focused && !do_send,
                      tk_of(ui, QL_INK), with_alpha(tk_of(ui, QL_INK), 0.46f),
                      col_sel, tk_of(ui, QL_PRIMARY_UI),
                      tr("chat.placeholder"));

    /* 发送按钮：点击等同回车 */
    const SpriteDef *sp = ui_sprite_by_name("chat-send");
    if (sp) {
        if (send_hit) {
            ui_draw_rect(ui, icon_x - 2.0f * z, icon_y - 2.0f * z,
                         icon_sz + 4.0f * z, icon_sz + 4.0f * z,
                         with_alpha(tk_of(ui, QL_PRIMARY_UI), 0.16f));
        }
        ui_sprite_icon4(ui, sp, icon_x, icon_y, icon_sz,
                        tk_of(ui, QL_SURFACE), tk_of(ui, QL_PRIMARY_UI),
                        tk_of(ui, QL_ACCENT_UI));
    }

    /* 输入条底部分隔线 */
    ui_draw_rect(ui, x, y + box_h - 1.0f, w, 1.0f, tk_of(ui, QL_LINE_SOFT));

    if (do_send) {
        app_chat_send();
        focused = false;
    }
    return box_h;
}

bool game_chat_over_ui(UiContext *ui) {
    bool sidebar = g_app.esc_paused || g_app.settings_in_drawer;
    if (!sidebar) return false;   /* 气泡态只读，不吃指针 */
    float z = ui->met.size, sx = ui->safe_x_px, sy = ui->safe_y_px;
    float bar = game_top_bar_height(ui);
    float avail = (float)ui->win_w - (ui->safe_edges[0]+ui->safe_edges[2]);
    float w = qlcg_minf(330 * z, qlcg_maxf(0, avail - game_esc_drawer_width(ui)));
    float h = qlcg_maxf((float)ui->win_h - (ui->safe_edges[1]+ui->safe_edges[3]) - bar, 120 * z);
    return ui_pointer_in_rect(ui, sx, sy + bar, w, h);
}

void game_chat_panel(UiContext *ui) {
    /* 菜单与设置抽屉都展开侧栏，对齐 base */
    bool sidebar = g_app.esc_paused || g_app.settings_in_drawer;
    bool focused = g_app.chat_open;

    float z  = ui->met.size;
    float sc = ui->met.scale;
    float sx = ui->safe_x_px;
    float sy = ui->safe_y_px;
    float bar_h = game_top_bar_height(ui);

    float chat_top = sy + bar_h;
    float avail    = (float)ui->win_w - (ui->safe_edges[0]+ui->safe_edges[2]);
    float cw       = qlcg_minf(330.0f * z, avail);
    float px       = 12.0f * sc * 1.3f;   /* base 12px × text-scale */
    float line_h   = px * 1.55f;

    if (!sidebar) {
        if(g_app.river_craft.active) return;
        if (g_app.chat_log_count <= 0) return;
        double idle_ms = qlcg_now_ms() - g_app.chat_last_message_ms;
        float fade = ui->reduce_motion ? (idle_ms < 30000.0 ? 1.0f : 0.0f)
                    : 1.0f - qlcg_smoothf(30000.0f, 30250.0f, (float)idle_ms);
        if (fade <= 0.0f) return;

        bool atlas = g_app.view_mode == VIEW_MODE_ATLAS;
        if (atlas) chat_top += game_atlas_tools_height(ui);
        float bottom = (float)ui->win_h - sy - (atlas ? game_atlas_detail_height(ui) : 0.0f);
        float hist_h = (bottom - chat_top) * 0.5f;
        if (hist_h <= 0.0f || cw <= 0.0f) return;

        float pad_x  = 14.0f * z;
        float pad_y  = 12.0f * z;
        float wrap_w = cw - pad_x * 2.0f;
        float gap    = 10.0f * z;

        float solid_h = hist_h * 0.8f;
        uint32_t paper = with_alpha(tk_of(ui, QL_SURFACE_HI), 0.6f);
        float alpha = ui->global_alpha;
        ui->global_alpha *= fade;
        ui_draw_rect(ui, sx, chat_top, cw, solid_h, paper);
        ui_draw_rect_grad(ui, sx, chat_top + solid_h, cw, hist_h - solid_h,
                          paper, with_alpha(paper, 0.0f));

        ui->text_fade_top = chat_top + solid_h;
        ui->text_fade_bottom = chat_top + hist_h;
        float ky = chat_top + pad_y;
        ui_clip_push(ui, sx, chat_top, cw, hist_h);
        for (int i = g_app.chat_log_count - 1; i >= 0 && ky < ui->text_fade_bottom; i--) {
            int lines = draw_chat_line(ui, sx + pad_x, ky, g_app.chat_logs[i], px,
                                       wrap_w, line_h, true);
            ky += lines * line_h + gap;
        }
        ui_clip_pop(ui);
        ui->text_fade_top = ui->text_fade_bottom = 0.0f;
        ui->global_alpha = alpha;
        return;
    }

    /* ---- 展开态：左侧对话抽屉 ---- */
    float dw     = game_esc_drawer_width(ui);
    float avail2 = qlcg_maxf(0.0f, avail - dw);
    cw = qlcg_minf(330.0f * z, avail2);
    float ph = (float)ui->win_h - sy - chat_top;
    if (ph < 120.0f * z) ph = 120.0f * z;

    /* 同料纸玻璃：含顶右细线与外投影 */
    hud_glass_panel(ui, sx, chat_top, cw, ph);

    /* 输入条绝对定位在顶部，高度随内容换行增长 */
    float composer_h = chat_composer(ui, sx, chat_top, cw, bar_h, px, focused);

    float pad = 14.0f * z;
    float list_top = chat_top + composer_h + pad;

    /* 清空按钮：11px×text-scale */
    float clear_px = 11.0f * sc * 1.3f;
    float clear_h  = clear_px * 1.62f + 12.0f * z + 2.0f * z;
    float clear_y  = chat_top + ph - pad - clear_h;
    float clear_x  = sx + pad;
    float clear_w  = cw - pad * 2.0f;
    bool chov = ui_hit(ui, clear_x, clear_y, clear_w, clear_h);
    ui_draw_rect(ui, clear_x, clear_y, clear_w, clear_h,
                 chov ? with_alpha(tk_of(ui, QL_INK), 0.13f)
                      : with_alpha(tk_of(ui, QL_INK), 0.06f));
    ui_draw_border(ui, clear_x, clear_y, clear_w, clear_h, 1.0f, tk_of(ui, QL_LINE_SOFT));
    {
        const char *ct = tr("chat.clear");
        float tw = (float)ui_text_width_px(ui, UI_FACE_BODY, clear_px, false, ct);
        ui_draw_text_px(ui, UI_FACE_BODY, clear_px, false,
                        clear_x + (clear_w - tw) * 0.5f,
                        clear_y + (clear_h - clear_px * 1.35f) * 0.5f,
                        ct, tk_of(ui, QL_INK_2));
    }
    if (chov && ui->mouse_clicked) {
        g_app.chat_log_count = 0;
        ui_scroll_reset(ui, UIS_CHAT_LIST);
    }

    /* 消息区：透明，最新在最上面，逐条按宽度折行 */
    float list_h   = clear_y - 8.0f * z - list_top;
    if (list_h <= 0.0f) return;

    float wrap_w = cw - pad * 2.0f;
    float hgt[MAX_CHAT_MSGS];
    float total = 0.0f;
    for (int i = g_app.chat_log_count - 1; i >= 0; i--) {
        int nl = draw_chat_line(ui, sx + pad, 0.0f, g_app.chat_logs[i], px,
                                wrap_w, line_h, false);
        hgt[i] = (float)nl * line_h + 10.0f * z;   /* 条间距 10px，对齐 base */
        total += hgt[i];
    }

    /* 先量总高，再按偏移画可见部分 */
    float off = ui_scroll_begin(ui, UIS_CHAT_LIST, sx, list_top, cw, list_h);
    float ly  = list_top - off;
    for (int i = g_app.chat_log_count - 1; i >= 0; i--) {
        if (ly + hgt[i] > list_top && ly < list_top + list_h) {
            draw_chat_line(ui, sx + pad, ly, g_app.chat_logs[i], px,
                           wrap_w, line_h, true);
        }
        ly += hgt[i];
    }
    ui_scroll_end(ui, UIS_CHAT_LIST, total);
}
