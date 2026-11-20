#ifndef QLCG_VIEW_ATLAS_LAYERS_H
#define QLCG_VIEW_ATLAS_LAYERS_H

#include "app/app_state.h"

typedef struct {
    UiRect list;
    float row_top[LAYER_ID_COUNT + 1];
    float line_h, check_h, row_step;
    float handle_x, handle_w, check_x, name_w;
} AtlasPanelLayout;

UiRect game_atlas_panel_bounds(UiContext *ui);
AtlasPanelLayout atlas_panel_layout(UiContext *ui, const AtlasDisplayState *display, UiRect bounds);
bool  game_atlas_over_ui(UiContext *ui);
void  atlas_layer_panel(UiContext *ui, AtlasRenderer *ar, UiRect r, bool creator);
void  game_atlas_layer_panel(UiContext *ui);
/* 舆图工具条与详情栏高度，绘制与命中判定共用 */
float game_atlas_tools_height(UiContext *ui);
float game_atlas_detail_height(UiContext *ui);

#endif
