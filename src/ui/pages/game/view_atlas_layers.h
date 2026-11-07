#ifndef QLCG_VIEW_ATLAS_LAYERS_H
#define QLCG_VIEW_ATLAS_LAYERS_H

#include "app/app_state.h"

bool  game_atlas_over_ui(UiContext *ui);
void  atlas_layer_panel(UiContext *ui, AtlasRenderer *ar, UiRect r);
void  game_atlas_layer_panel(UiContext *ui);
/* 舆图工具条与详情栏高度，绘制与命中判定共用 */
float game_atlas_tools_height(UiContext *ui);
float game_atlas_detail_height(UiContext *ui);

#endif
