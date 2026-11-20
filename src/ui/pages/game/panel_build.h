#ifndef QLCG_PANEL_BUILD_H
#define QLCG_PANEL_BUILD_H
#include "ui/ui_core.h"
bool game_build_open(void);
bool game_build_interact(void);
void game_build_close(void);
void game_build_key(unsigned key,bool repeat);
void game_build_draw(UiContext *ui);
void game_build_prompt(UiContext *ui);
bool game_build_prompt_over(UiContext *ui);
void game_build_gesture(float zoom,float angle);
#endif
