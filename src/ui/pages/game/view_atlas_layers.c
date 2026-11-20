/* 图层面板与输入边界：贴视口右上角（对齐 base） */
#include "view_atlas_layers.h"
#include "hud_layout.h"
#include "hud_civilization.h"
#include <stdio.h>

UiRect game_atlas_panel_bounds(UiContext *ui) {
    float z   = ui->met.size;
    float sx  = ui->safe_x_px;
    float top=ui->safe_y_px+game_top_bar_height(ui)+game_atlas_tools_height(ui);
    top=fmaxf(top,game_character_chat_top(ui));
    float det = game_atlas_detail_height(ui);
    /* 宽 min(260z,42vw)，贴右上 0 */
    float w = qlcg_minf(260.0f * z, (float)ui->win_w * 0.42f);
    float h = (float)ui->win_h - top - ui->safe_edges[3] - det;
    if (h < 80.0f * z) h = 80.0f * z;
    UiRect r = { (float)ui->win_w - sx - w, top, w, h };
    return r;
}

bool game_atlas_over_ui(UiContext *ui) {
    UiRect r = game_atlas_panel_bounds(ui);
    float  top = game_top_bar_height(ui) + game_atlas_tools_height(ui);
    float  det = game_atlas_detail_height(ui);
    return game_scene_over_ui(ui) || game_civilization_over_ui(ui) || app_river_craft_over_ui(ui) || ui_toast_over_ui(ui, &g_app.toasts) ||
           g_app.atlas.layer_dragging || ui->drag_slider_id >= 0 ||
           (g_app.atlas.layer_panel_open && ui_hit(ui, r.x, r.y, r.w, r.h)) ||
           ui_hit(ui, 0, 0, (float)ui->win_w, top) ||
           ui_hit(ui, 0, (float)ui->win_h - det, (float)ui->win_w, det);
}

AtlasPanelLayout atlas_panel_layout(UiContext *ui, const AtlasDisplayState *display, UiRect r) {
    float z = ui->met.size;
    AtlasPanelLayout layout = {0};
    layout.list = (UiRect){r.x + 12*z, r.y + 10*z, r.w - 24*z, r.h - 20*z};
    layout.check_h = ui_checkbox_height(ui);
    layout.line_h = qlcg_maxf(16*z, layout.check_h);
    layout.row_step = 10*z + layout.line_h;
    layout.handle_x = layout.list.x + 2*z;
    layout.handle_w = 16*z;
    layout.check_x = layout.handle_x + layout.handle_w + 6*z;
    layout.name_w = layout.list.x + layout.list.w - layout.check_x - layout.check_h - 8*z;
    for (int i = 0; i < LAYER_ID_COUNT; i++) {
        bool slider = qlcg_atlas_layer_has_opacity(display->layers[i].id);
        layout.row_top[i + 1] = layout.row_top[i] + layout.row_step + (slider ? 18*z : 0);
    }
    return layout;
}

void atlas_layer_panel(UiContext *ui, AtlasRenderer *ar, UiRect r, bool creator) {
    float z = ui->met.size;
    float font = 11.0f * ui->met.scale;
    AtlasPanelLayout layout = atlas_panel_layout(ui, &ar->display, r);
    UiRect list = layout.list;
    const float *row_top = layout.row_top;
    float line1 = layout.line_h, chk_h = layout.check_h;
    float total_h = row_top[LAYER_ID_COUNT];
    float max_scroll = qlcg_maxf(0.0f, total_h - list.h);
    bool in_list = ui_hit(ui, list.x, list.y, list.w, list.h);
    if (in_list && ui->wheel_delta && !ar->layer_dragging)
        ar->layer_scroll -= (float)ui->wheel_delta / 120.0f * layout.row_step * 2.0f;
    if (ar->layer_dragging && ui->mouse_down && in_list) {
        float step = layout.row_step;
        if (ui->mouse_y < list.y + step * 0.5f) ar->layer_scroll -= step * 8.0f * ui->dt;
        if (ui->mouse_y > list.y + list.h - step * 0.5f) ar->layer_scroll += step * 8.0f * ui->dt;
    }
    ar->layer_scroll = qlcg_clampf(ar->layer_scroll, 0.0f, max_scroll);
    if (ar->layer_dragging) {
        ar->dragging = false;
        if (in_list) {
            float rel = (float)ui->mouse_y - list.y + ar->layer_scroll;
            int to = 0;
            for (int k = 0; k < LAYER_ID_COUNT; k++)
                if (rel >= row_top[k]) to = k;
            ar->layer_drag_to = to;
        }
        if (!ui->mouse_down) {
            if (qlcg_atlas_layer_move(&ar->display, ar->layer_drag_from, ar->layer_drag_to))
                layout = atlas_panel_layout(ui, &ar->display, r);
            ar->layer_dragging = false;
        }
    }

    /* 面板：surface 底+line 边，顶不描 */
    ui_draw_rect(ui, r.x, r.y, r.w, r.h, tk_of(ui, QL_SURFACE));
    ui_draw_rect(ui, r.x, r.y, 1.0f, r.h, tk_of(ui, QL_LINE));
    ui_draw_rect(ui, r.x + r.w - 1.0f, r.y, 1.0f, r.h, tk_of(ui, QL_LINE));
    ui_draw_rect(ui, r.x, r.y + r.h - 1.0f, r.w, 1.0f, tk_of(ui, QL_LINE));

    bool clicked = ui->mouse_clicked, down = ui->mouse_down;
    ui_clip_push(ui, list.x, list.y, list.w, list.h);
    for (int i = 0; i < LAYER_ID_COUNT; i++) {
        AtlasLayerSetting *layer = &ar->display.layers[i];
        float y  = list.y + row_top[i] - ar->layer_scroll;
        float rh = row_top[i + 1] - row_top[i];
        bool visible = y + rh > list.y && y < list.y + list.h;
        ui->mouse_clicked = clicked && in_list && !ar->layer_dragging && visible;
        ui->mouse_down = down && in_list && !ar->layer_dragging && visible;

        float rx      = layout.handle_x;
        float drag_w  = layout.handle_w;
        float check_x = layout.check_x;
        float top_y   = y + 5.0f * z;
        float name_w  = layout.name_w;

        if (visible) {
            if (!creator)
                ui_draw_rect(ui, list.x, y + rh - 1.0f, list.w, 1.0f,
                             with_alpha(tk_of(ui, QL_LINE_FAINT), 0.55f));
            bool dragging = ar->layer_dragging && ar->layer_drag_from == i;
            if (dragging) ui_draw_rect(ui, list.x, y, list.w, rh,
                                       with_alpha(tk_of(ui, QL_INK), 0.06f));
            if (ar->layer_dragging && ar->layer_drag_to == i) {
                float line_y = y + (i > ar->layer_drag_from ? rh : 0.0f);
                ui_draw_line(ui, list.x, line_y, list.x + list.w, line_y, 2.0f * z,
                             tk_of(ui, QL_PRIMARY_UI));
            }
            /* 拖动把手 */
            bool handle = ui_hit(ui, rx, top_y, drag_w, line1);
            for (int j = 0; j < 3; j++) {
                float hy = top_y + line1 * 0.5f + (j - 1) * 5.0f * z;
                ui_draw_line(ui, rx + 2.0f * z, hy, rx + drag_w - 2.0f * z, hy, 1.5f * z,
                             handle || dragging ? tk_of(ui, QL_PRIMARY_UI) : tk_of(ui, QL_INK_3));
            }
            if (handle && ui->mouse_clicked && ui->mouse_down) {
                ar->layer_dragging = true;
                ar->layer_drag_from = ar->layer_drag_to = i;
                qlcg_platform_capture();
                ar->dragging = false;
                ui->mouse_clicked = ui->mouse_down = false;
            }
            /* 关闭层整行压淡（.is-off .55） */
            if (!layer->on) ui_draw_rect(ui, list.x, y, list.w, rh - 1.0f,
                                         with_alpha(tk_of(ui, QL_SURFACE), 0.45f));
        }

        const char *name = qlcg_atlas_layer_name(layer->id);
        if (creator) {
            ui_checkbox_row(ui, check_x, top_y, list.x + list.w - check_x - 2.0f * z,
                            line1, name, font, &layer->on);
        } else {
            float check_y = top_y + (line1 - chk_h) * 0.5f;
            ui_checkbox(ui, check_x, check_y, "", &layer->on);
            float name_x = check_x + chk_h + 6.0f * z;
            if (name_w > 4.0f && ui->mouse_clicked && ui_hit(ui, name_x, top_y, name_w, line1))
                layer->on = !layer->on;
            ui_clip_push(ui, name_x, top_y, name_w, line1);
            ui_draw_text_ellipsis(ui, UI_FACE_BODY, font, false, name_x,
                                 top_y + (line1 - font * 1.35f) * 0.5f, name_w, 0,
                                 name, layer->on ? tk_of(ui, QL_INK) : tk_of(ui, QL_INK_3));
            ui_clip_pop(ui);
        }

        /* 滑杆：第二行，从勾选列起占满，不带数值 */
        if (qlcg_atlas_layer_has_opacity(layer->id)) {
            float slider_y = top_y + line1 + 8.0f * z;
            if (ui->drag_slider_id >= 0 && ui->drag_slider_id != ui->slider_seq)
                ui->mouse_down = false;
            float track_w = name_w;
            if (track_w < 20.0f * z) track_w = 20.0f * z;
            ui_slider_at(ui, check_x, slider_y,
                         track_w, &layer->opacity, 0.0f, 1.0f);
        }
    }
    ui_clip_pop(ui);
    if (max_scroll > 0.0f) {
        float h = list.h * list.h / total_h;
        float y = list.y + (list.h - h) * ar->layer_scroll / max_scroll;
        ui_draw_rect(ui, r.x + r.w - 4.0f * z, y, 2.0f * z, h, tk_of(ui, QL_LINE));
    }
    ui->mouse_clicked = clicked;
    ui->mouse_down = down;
}

void game_atlas_layer_panel(UiContext *ui) {
    AtlasRenderer *ar = &g_app.atlas;
    if (ar->layer_panel_open && !g_app.esc_paused) atlas_layer_panel(ui, ar, game_atlas_panel_bounds(ui), false);
    else ar->layer_dragging = false;
}
