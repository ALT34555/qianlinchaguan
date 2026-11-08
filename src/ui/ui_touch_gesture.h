#ifndef QLCG_UI_TOUCH_GESTURE_H
#define QLCG_UI_TOUCH_GESTURE_H
#include <stdbool.h>

typedef struct {
    bool down,dragged,cancelled,double_tap;
    float start_x,start_y,last_x,last_y;
    double started,last_tap;
} UiTouchGesture;
void ui_gesture_down(UiTouchGesture *g,float x,float y,double now);
bool ui_gesture_move(UiTouchGesture *g,float x,float y,float density);
bool ui_gesture_up(UiTouchGesture *g,float x,float y,double now,float density);
void ui_gesture_cancel(UiTouchGesture *g);
#endif
