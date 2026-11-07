#include "app_state.h"
#include "systems/world/world_infinite.h"
#include "systems/world/climate_season.h"
#include "systems/world/precip.h"

void app_infinite_step(void) {
    MacroWorld *old=g_app.world;
    if(!old || !qlcg_world_flat(old)) return;
    bool terrain=g_app.view_mode==VIEW_MODE_TERRAIN && g_app.region;
    float wx=terrain?g_app.region->center_x+g_app.terrain.x:(g_app.player_cx+.5f)*CHUNK_SIZE;
    float wz=terrain?g_app.region->center_z+g_app.terrain.z:(g_app.player_cz+.5f)*CHUNK_SIZE;
    MacroWorld *next=qlcg_infinite_explore(old,&wx,&wz);
    if(next==old) return;
    if(!next) {
        float limit=old->circumference*.5f-CHUNK_SIZE;
        wx=qlcg_clampf(wx,-limit,limit); wz=qlcg_clampf(wz,-limit,limit);
        g_app.player_cx=(int)floorf(wx/CHUNK_SIZE); g_app.player_cz=(int)floorf(wz/CHUNK_SIZE);
        if(terrain) { g_app.terrain.x=wx-g_app.region->center_x; g_app.terrain.z=wz-g_app.region->center_z; }
        app_push_toast_k(UI_TOAST_WARNING,NULL,tr("world.infinite.failed")); return;
    }
    int cx=(int)floorf(wx/CHUNK_SIZE),cz=(int)floorf(wz/CHUNK_SIZE);
    int cell=qlcg_world_cell_at_wxz(next,wx,wz);
    int season=qlcg_climate_season_index(next,cell,&g_app.snap);
    qlcg_precip_sync(next,g_app.clock.unix_ms);
    RegionData *region=qlcg_region_sample(next,cx,cz,season,&g_app.settings);
    if(!region) {
        app_push_toast_k(UI_TOAST_WARNING,NULL,tr("world.infinite.failed")); return;
    }
    qlcg_ice_visual_reset(&g_app.terrain);
    qlcg_terrain_clear_chunks(&g_app.terrain);
    qlcg_region_free(g_app.region); g_app.region=region; g_app.world=next;
    g_app.player_cx=cx; g_app.player_cz=cz;
    g_app.terrain.x=wx-region->center_x; g_app.terrain.z=wz-region->center_z;
    qlcg_world_reveal_chunk(next,cx,cz);
    qlcg_terrain_set_season(&g_app.terrain,season);
    qlcg_terrain_upload_region(&g_app.terrain,region,&g_app.settings);
    qlcg_atlas_release_world(&g_app.atlas);
    qlcg_atlas_upload_world(&g_app.atlas,next);
    qlcg_atlas_center_on(&g_app.atlas,next,wx,wz,true);
    g_app.atlas.eclipse_ready=false; g_app.river_craft=(RiverCraft){0};
    qlcg_astra_upload(&g_app.terrain.astronomy,&next->opt);
    char msg[160]; snprintf(msg,sizeof(msg),tr("world.infinite.enter"),next->opt.plane_x,next->opt.plane_z);
    app_push_toast(msg);
}
