#ifndef QLCG_TERRAIN_DOLLS_H
#define QLCG_TERRAIN_DOLLS_H
#include "terrain_visual.h"
#define QLCG_DOLL_INTERACT_REACH 20.0f
#include "../systems/dolls/doll_dialogue.h"
void qlcg_terrain_dolls_draw(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *world,const TerrainVisualFrame *frame);
bool qlcg_terrain_doll_pose(const TerrainRenderer *tr,const RegionData *reg,const MacroWorld *world,
                            const TerrainDollRef *ref,float pose[5]);
bool qlcg_terrain_dolls_probe(const TerrainRenderer *tr,const RegionData *reg,const MacroWorld *world,const float direction[3],float limit,AimProbeResult *result);
uint64_t qlcg_terrain_dolls_revision(const TerrainRenderer *tr);
bool qlcg_terrain_dolls_watch_response(const TerrainRenderer *tr,const TerrainDollRef *resident,
                                        int *state,float position[3]);
bool qlcg_terrain_dolls_take_speech(TerrainRenderer *tr,QlcgDollSpeech *speech);
void qlcg_terrain_dolls_free(TerrainRenderer *tr);
#endif
