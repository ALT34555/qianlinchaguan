#ifndef QLCG_BUILD_INTERNAL_H
#define QLCG_BUILD_INTERNAL_H
#include "panel_build.h"
#include "app/app_state.h"
#include "systems/world/world_lot.h"
#include "systems/world/model/editor_buildings.h"
#include "systems/world/world_delta.h"
#include "systems/world/world_prop_edit.h"
#include "systems/world/world_claims.h"
#include "systems/world/world_settlement.h"
#include "systems/world/surface_stream.h"
enum { BUILD_MACRO,BUILD_TERRAIN,BUILD_HOUSE,BUILD_BUY };
enum { B_PLACE,B_MOVE,B_COPY,B_REMOVE,B_FLAG,B_SELECT,B_EXPAND,B_RELEASE,B_TEMPLATE };
typedef struct {
    MacroWorld *world;
    RegionData *region;
    const TerritoryFaction *faction;
    int cx,cz,flag_cx,flag_cz,mode,action,model,moving,page,filter,pack,lot_type,crop,prop,rooms;
    int season,brush_tool,material,day,view;
    uint32_t seed;
    bool grid,foundation,square,compact,manual_yaw,has_start;
    float fx,fz,yaw,pitch,distance,angle,brush,strength,start_x,start_z;
    float eye[3],forward[3],right[3],up[3],focal;
    int last_x,last_y,press_x,press_y;
    bool area_drag,styles;
    double last_time,entered_ms;
    char message[160];
} BuildEditor;
extern BuildEditor build;
const QlcgLot *build_lot(void);
void build_back(void);
void build_mode(int mode);
void build_history(bool redo);
void build_rotate(void);
bool build_line_tool(void);
void build_remove_area(float x1,float z1,float x2,float z2);
bool build_preview(float x,float z,QlcgSettlementPart *out);
void build_click(int cx,int cz,float x,float z,bool twice);
void build_toolbar(UiContext *ui,float top,float bottom,float row);
void build_camera(UiContext *ui,bool canvas);
void build_scene(UiContext *ui,bool canvas);
bool build_candidate(int cx,int cz);
#endif
