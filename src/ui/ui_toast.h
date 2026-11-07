#ifndef QLCG_UI_TOAST_H
#define QLCG_UI_TOAST_H

#include "ui_core.h"

/* 通知语义：决定图标、竖条与进度条配色 */
typedef enum {
    UI_TOAST_INFO = 0,
    UI_TOAST_SUCCESS,
    UI_TOAST_WARNING,
    UI_TOAST_ERROR,
    UI_TOAST_KIND_COUNT
} UiToastKind;

#define UI_TOAST_SLOTS 4

typedef struct {
    bool  used;
    int   kind;
    char  title[48];
    char  msg[160];
    float life;      /* 总时长，秒 */
    float t;         /* 已过时间，秒 */
    bool  hold;      /* 悬停中，暂停计时 */
    bool  dragging;  /* 正在被拖动 */
    float drag_x0;   /* 按下时指针 x */
    float drag;      /* 当前右移量 */
    float hit_x, hit_y, hit_w, hit_h;
} UiToast;

typedef struct {
    UiToast item[UI_TOAST_SLOTS];
    uint32_t layout_frame;
} UiToastQueue;

/* 推入一条；同 kind 同文案则重置计时，不新增 */
void ui_toast_push(UiToastQueue *q, int kind, const char *title, const char *msg, float sec);
/* 推进计时；悬停与拖动中的条目不推进，到期移除 */
void ui_toast_update(UiToastQueue *q, float dt);
bool ui_toast_over_ui(UiContext *ui, UiToastQueue *q);
/* 右上堆叠绘制，同时处理悬停判定与右滑划走 */
void ui_toast_render(UiContext *ui, UiToastQueue *q);

#endif
