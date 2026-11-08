/* 图层面板与输入边界：贴视口右上角（对齐 base） */
#include "view_atlas_layers.h"
#include "hud_layout.h"
#include <stdio.h>

static UiRect panel_rect(UiContext *ui) {
    float z   = ui->met.size;
    float sx  = ui->safe_x_px;
    float top = game_top_bar_height(ui) + game_atlas_tools_height(ui);
    float det = game_atlas_detail_height(ui);
    /* base：宽 min(260z,42vw)，贴右上 0 */
    float w = qlcg_minf(260.0f * z, (float)ui->win_w * 0.42f);
    float h = (float)ui->win_h - top - ui->safe_edges[3] - det;
    if (h < 80.0f * z) h = 80.0f * z;
    UiRect r = { (float)ui->win_w - sx - w, top, w, h };
    return r;
}

bool game_atlas_over_ui(UiContext *ui) {
    UiRect r = panel_rect(ui);
    float  top = game_top_bar_height(ui) + game_atlas_tools_height(ui);
    float  det = game_atlas_detail_height(ui);
    return game_scene_over_ui(ui) || app_river_craft_over_ui(ui) || ui_toast_over_ui(ui, &g_app.toasts) ||
           g_app.atlas.layer_dragging || ui->drag_slider_id >= 0 ||
           (g_app.atlas.layer_panel_open && ui_hit(ui, r.x, r.y, r.w, r.h)) ||
           ui_hit(ui, 0, 0, (float)ui->win_w, top) ||
           ui_hit(ui, 0, (float)ui->win_h - det, (float)ui->win_w, det);
}

void atlas_layer_panel(UiContext *ui, AtlasRenderer *ar, UiRect r) {
    float z    = ui->met.size;
    float sc   = ui->met.scale;
    float font = 11.0f * sc;

    /* base：面板 pad 10/12，行内 gap 4/6 */
    float pad_x = 12.0f * z;
    float pad_y = 10.0f * z;
    float chk_h = ui_checkbox_height(ui);
    float line1 = qlcg_maxf(16.0f * z, chk_h);
    float line2 = 14.0f * z;
    float row_pad = 10.0f * z;

    /* grid 空区高 0，只有带滑杆的行占第二行 */
    float row_top[LAYER_ID_COUNT + 1];
    row_top[0] = 0.0f;
    for (int i = 0; i < LAYER_ID_COUNT; i++) {
        bool hop = qlcg_atlas_layer_has_opacity(ar->display.layers[i].id);
        row_top[i + 1] = row_top[i] + row_pad + line1 + (hop ? 4.0f * z + line2 : 0.0f);
    }
    float total_h = row_top[LAYER_ID_COUNT];

    UiRect list = { r.x + pad_x, r.y + pad_y, r.w - pad_x * 2.0f, r.h - pad_y * 2.0f };
    float max_scroll = qlcg_maxf(0.0f, total_h - list.h);
    bool in_list = ui_hit(ui, list.x, list.y, list.w, list.h);
    if (in_list && ui->wheel_delta && !ar->layer_dragging)
        ar->layer_scroll -= (float)ui->wheel_delta / 120.0f * (line1 + row_pad) * 2.0f;
    if (ar->layer_dragging && ui->mouse_down && in_list) {
        float step = line1 + row_pad;
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
            qlcg_atlas_layer_move(&ar->display, ar->layer_drag_from, ar->layer_drag_to);
            ar->layer_dragging = false;
        }
    }

    /* 面板：surface 底 + line 边，顶边不描 */
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

        float rx      = list.x + 2.0f * z;
        float drag_w  = 16.0f * z;
        float check_x = rx + drag_w + 6.0f * z;
        float top_y   = y + 5.0f * z;
        float name_w  = list.x + list.w - 2.0f * z - check_x - chk_h - 6.0f * z;

        if (visible) {
            /* 行底虚线分隔 */
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
            /* 关掉的层整行压淡（base .is-off .55） */
            if (!layer->on) ui_draw_rect(ui, list.x, y, list.w, rh - 1.0f,
                                         with_alpha(tk_of(ui, QL_SURFACE), 0.45f));
        }

        /* 勾选 + 名字：第一行第二列 */
        float check_y = top_y + (line1 - chk_h) * 0.5f;
        ui_checkbox(ui, check_x, check_y, "", &layer->on);
        float name_x = check_x + chk_h + 6.0f * z;
        if (name_w > 4.0f && ui->mouse_clicked && ui_hit(ui, name_x, top_y, name_w, line1))
            layer->on = !layer->on;
        ui_clip_push(ui, name_x, top_y, name_w, line1);
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, font, false, name_x,
                             top_y + (line1 - font * 1.35f) * 0.5f, name_w, 0,
                             qlcg_atlas_layer_name(layer->id),
                             layer->on ? tk_of(ui, QL_INK) : tk_of(ui, QL_INK_3));
        ui_clip_pop(ui);

        /* 滑杆：第二行，从勾选列起占满，不带数值 */
        if (qlcg_atlas_layer_has_opacity(layer->id)) {
            float slider_y = top_y + line1 + 4.0f * z;
            if (ui->drag_slider_id >= 0 && ui->drag_slider_id != ui->slider_seq)
                ui->mouse_down = false;
            float track_w = name_w;
            if (track_w < 20.0f * z) track_w = 20.0f * z;
            ui_slider_at(ui, check_x, slider_y + line2 * 0.5f - 3.0f * z,
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
    if (ar->layer_panel_open && !g_app.esc_paused) atlas_layer_panel(ui, ar, panel_rect(ui));
    else ar->layer_dragging = false;
}
