#ifndef QLCG_APP_RIVER_CRAFT_H
#define QLCG_APP_RIVER_CRAFT_H
#include "systems/world/river_edit.h"

typedef struct {
    bool active,dirty,drawing;
    int tool,selected;
    double spacing;
    RiverEdit draft;
} RiverCraft;
void app_river_craft_command(const char *arg);
void app_river_craft_click(double wx,double wz,double radius);
void app_river_craft_stroke(double wx,double wz,double radius,bool begin,bool down);
void app_river_craft_pointer(bool begin,bool down);
bool app_river_craft_over_ui(UiContext *ui);
void app_river_craft_draw(UiContext *ui,float x,float y,float width,float height);
#endif
