#ifndef QLCG_UI_SCROLL_H
#define QLCG_UI_SCROLL_H

#include "ui_core.h"

#define UI_SCROLL_SLOTS 8

/* 一个滚动区：按 id 区分，可同时存在多处 */
typedef struct {
    uint32_t id;
    float    off;
    float    x, y, w, h;
    float    content_h;
} UiScrollSlot;

/* 开始滚动区：压入裁剪、处理滚轮，返回当前偏移量 */
float ui_scroll_begin(UiContext *ui, uint32_t id, float x, float y, float w, float h);

/* 结束滚动区：内容总高相对视图顶，更新限位画条 */
void ui_scroll_end(UiContext *ui, uint32_t id, float content_h);

/* 把指定滚动区归零（切标签页时用） */
void ui_scroll_reset(UiContext *ui, uint32_t id);

/* 读取滚动区当前偏移，未建立时返回 0 */
float ui_scroll_offset(UiContext *ui, uint32_t id);

#endif /* QLCG_UI_SCROLL_H */
