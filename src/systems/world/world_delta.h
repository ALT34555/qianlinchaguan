#ifndef QLCG_WORLD_DELTA_H
#define QLCG_WORLD_DELTA_H
#include <stdbool.h>
#include <stdio.h>
typedef struct MacroWorld MacroWorld;
bool qlcg_chd_edit(MacroWorld *w,float x,float z,float radius,int height_cm,int substrate,int forest_pct);
bool qlcg_chd_set_prop(MacroWorld *w,float x,float z,int kind,int state);
int qlcg_chd_prop_state(const MacroWorld *w,float x,float z,int kind);
bool qlcg_chd_plant(MacroWorld *w,float x,float z,int kind);
int qlcg_chd_planted_state(const MacroWorld *w,float x,float z,int kind);
typedef void (*ChdPropVisit)(float x,float z,int kind,int state,void *context);
void qlcg_chd_visit_planted(const MacroWorld *w,int cx,int cz,ChdPropVisit visit,void *context);
bool qlcg_chd_place_workbench(MacroWorld *w,float x,float z);
unsigned qlcg_chd_workbench_count(const MacroWorld *w,int cx,int cz);
void qlcg_chd_visit_workbenches(const MacroWorld *w,int cx,int cz,ChdPropVisit visit,void *context);
int qlcg_chd_material(const MacroWorld *w,float x,float z);
void qlcg_chd_sample(const MacroWorld *w,float x,float z,float *height,float *water,float *forest);
bool qlcg_chd_write(FILE *file,const MacroWorld *w);
bool qlcg_chd_read(FILE *file,long limit,MacroWorld *w);
typedef struct ChdLakeStroke ChdLakeStroke;
ChdLakeStroke *qlcg_chd_lake(MacroWorld *w,float x,float z,float radius);
ChdLakeStroke *qlcg_chd_channel(MacroWorld *w,float ax,float az,float bx,float bz,
                               float width,float depth,float meander);
void qlcg_chd_undo_lake(MacroWorld *w,const ChdLakeStroke *stroke);
typedef struct ChdLotSnapshot ChdLotSnapshot;
ChdLotSnapshot *qlcg_chd_lot_snapshot(const MacroWorld *w,int cx,int cz);
bool qlcg_chd_lot_swap(MacroWorld *w,int cx,int cz,ChdLotSnapshot *snapshot);
void qlcg_chd_lot_reset(MacroWorld *w,int cx,int cz);
void qlcg_chd_lot_dispose(ChdLotSnapshot *snapshot);
enum { LOT_RAISE,LOT_LOWER,LOT_SMOOTH,LOT_FLAT,LOT_RECT,LOT_POND,LOT_FILL,LOT_GRASS,LOT_PATH,LOT_PAINT };
typedef struct {
    int tool,material;
    float x,z,end_x,end_z,radius,strength;
    bool square;
} ChdLotBrush;
struct RegionData;
bool qlcg_chd_lot_brush(MacroWorld *w,const struct RegionData *chunk,const ChdLotBrush *brush);
void qlcg_chd_clear(MacroWorld *w);
void qlcg_chd_free(MacroWorld *w);
#endif
