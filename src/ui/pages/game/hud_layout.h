#ifndef QLCG_HUD_LAYOUT_H
#define QLCG_HUD_LAYOUT_H

#include "app/app_state.h"

/* HUD 槽位：每帧重置，元素按调用顺序在槽内依次排布 */
enum {
    HUD_TOP_LEFT = 0,
    HUD_TOP_RIGHT,
    HUD_BOTTOM_LEFT,
    HUD_BOTTOM_RIGHT,
    HUD_BOTTOM_CENTER,
    HUD_SLOT_COUNT
};

/* 每帧开头调用，把各槽位游标复位 */
void hud_reset(UiContext *ui);

/* 申请空间：返回左上角与尺寸，顶向下底向上堆叠 */
UiRect hud_take(UiContext *ui, int slot, float w, float h);

/* 顶部通栏让位：把两个顶部槽的游标一起推下去 */
void hud_reserve_top(UiContext *ui, float h);

/* 顶栏高度（未显示时为 0），供其他 HUD 元素避让 */
float game_top_bar_height(UiContext *ui);

/* 抽屉宽度（对齐 base），聊天侧栏避让 */
float game_esc_drawer_width(UiContext *ui);

/* 顶栏菜单钮：常驻最上层，点按开关菜单 */
void game_menu_button(UiContext *ui);

#endif /* QLCG_HUD_LAYOUT_H */
