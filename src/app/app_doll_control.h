#ifndef QLCG_APP_DOLL_CONTROL_H
#define QLCG_APP_DOLL_CONTROL_H

#include "terrain3d/terrain_view.h"
#include "systems/dolls/doll_life.h"

bool app_doll_possess(TerrainDollRef ref);
bool app_doll_release(void);
bool app_doll_give(TerrainDollRef ref, InventoryPosition item);
bool app_doll_rename(TerrainDollRef ref, const char *name);
bool app_doll_controlled(TerrainDollRef *out);
const QlcgDollLifeResident *app_doll_interact(TerrainDollRef ref);
const QlcgDollLifeResident *app_doll_controlled_resident(void);
const char *app_doll_controlled_name(void);
void app_doll_sync_pose(void);
void app_doll_control_reset(void);
bool app_doll_restore_chunk(MacroWorld *world,int cx,int cz);
bool app_doll_save_begin(float *x, float *y, float *z, float *yaw, float *pitch, bool *flying);
MacroWorld *app_doll_save_world(void);
void app_doll_save_end(void);

#endif
