#include "ui_touch_gesture.h"
#include <math.h>

void ui_gesture_down(UiTouchGesture *g,float x,float y,double now) {
    g->down=true;g->dragged=g->cancelled=g->double_tap=false;
    g->start_x=x;g->start_y=y;g->started=now;
}
bool ui_gesture_move(UiTouchGesture *g,float x,float y,float density) {
    float slop=12*fmaxf(1,density);
    if(g->down && hypotf(x-g->start_x,y-g->start_y)>slop)g->dragged=true;
    return g->dragged;
}
bool ui_gesture_up(UiTouchGesture *g,float x,float y,double now,float density) {
    ui_gesture_move(g,x,y,density);
    bool tap=g->down && !g->dragged && !g->cancelled && now-g->started<=500;
    g->down=false;g->double_tap=false;
    if(tap) {
        g->double_tap=g->last_tap>0 && now-g->last_tap<=450 &&
            hypotf(x-g->last_x,y-g->last_y)<=32*fmaxf(1,density);
        g->last_tap=g->double_tap?0:now;g->last_x=x;g->last_y=y;
    } else g->last_tap=0;
    return tap;
}
void ui_gesture_cancel(UiTouchGesture *g) {
    g->cancelled=true;g->last_tap=0;g->double_tap=false;
}
