#ifndef QLCG_UI_TOUCH_H
#define QLCG_UI_TOUCH_H

#include "ui_core.h"

/* 虚拟动作键数量：冲刺 / 跳跃 / 下蹲 */
#define UI_TOUCH_BTN_COUNT 3

/* 鼠标与触点使用独立编号 */
#define UI_TOUCH_MOUSE_POINTER (-2)

/* 可调项：顺序同 base TOUCH_ITEMS */
typedef enum {
    UI_TOUCH_ITEM_MOVE = 0,
    UI_TOUCH_ITEM_SPRINT,
    UI_TOUCH_ITEM_JUMP,
    UI_TOUCH_ITEM_CROUCH,
    UI_TOUCH_ITEM_COUNT
} UiTouchItem;

typedef struct {
    bool  enabled;
    float opacity;

    /* 摇杆 */
    float stick_cx, stick_cy, stick_r;
    int   stick_pointer;      /* -1 = 未按下 */
    float knob_dx, knob_dy;   /* 视觉偏移，限制在半径内 */
    int   axis_x, axis_y;     /* 摇杆输出方向：-1 / 0 / 1 */
    int   style;
    bool  sprint_locked;
    float forward_hold;

    /* 动作键 */
    float btn_cx[UI_TOUCH_BTN_COUNT];
    float btn_cy[UI_TOUCH_BTN_COUNT];
    float btn_r;
    int   btn_pointer[UI_TOUCH_BTN_COUNT];
    bool  btn_down[UI_TOUCH_BTN_COUNT];

    /* 视角拖动：增量由 app 层每帧取走后清零 */
    int   look_pointer;
    float look_dx, look_dy;
    float look_last_x, look_last_y;   /* 上一指针位置，避免用函数级 static */
} UiTouch;

void ui_touch_reset(UiTouch *t);
/* 每帧重算位置可见性；lay 见 settings */
void ui_touch_begin(UiTouch *t, UiContext *ui, bool enabled, float opacity,
                    const float lay[4][3]);
/* 按下：返回 true 表示控件接住，app 别再处理 */
bool ui_touch_down(UiTouch *t, UiContext *ui, int pointer, float x, float y);
void ui_touch_move(UiTouch *t, UiContext *ui, int pointer, float x, float y);
void ui_touch_up(UiTouch *t, UiContext *ui, int pointer);
void ui_touch_draw(UiTouch *t, UiContext *ui);

#endif
