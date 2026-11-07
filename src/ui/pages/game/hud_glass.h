#ifndef QLCG_HUD_GLASS_H
#define QLCG_HUD_GLASS_H

#include "app/app_state.h"

/* HUD玻璃：顶栏与聊天抽屉同料 */
void hud_glass_strip(UiContext *ui, float x, float y, float w, float h);
/* 抽屉用：顶部与右侧留细线，不画底部分隔 */
void hud_glass_panel(UiContext *ui, float x, float y, float w, float h);
/* 人名等自定义色：纸面不够亮时自动压深 */
uint32_t hud_ink_name(UiContext *ui, uint32_t hex, uint8_t alpha);

#endif /* QLCG_HUD_GLASS_H */
