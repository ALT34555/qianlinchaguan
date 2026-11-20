#include "app_state.h"
#include "app_doll_control.h"
#include "systems/world/world_infinite.h"
#include "terrain3d/terrain_dolls.h"

typedef struct {
    MacroWorld *owner;
    TerrainDollRef ref;
    int faction;
} DollTarget;

static struct {
    DollTarget host;
    MacroWorld *origin;
    Inventory origin_bag, plane_bag;
    float x,y,z,yaw,pitch,vx,vy,vz,crouch,step;
    bool flying,on_ground,saving;
} control;

static bool same_doll(TerrainDollRef a,TerrainDollRef b) {
    return a.id && a.id==b.id && a.plane_x==b.plane_x && a.plane_z==b.plane_z &&
           !memcmp(a.camp_uid,b.camp_uid,sizeof(a.camp_uid));
}

static bool find_doll(TerrainDollRef ref,DollTarget *out) {
    if (!g_app.world || !ref.id || !ref.camp_uid[0]) return false;
    MacroWorld *owner=g_app.world;
    if (owner->opt.plane_x!=ref.plane_x || owner->opt.plane_z!=ref.plane_z)
        owner=qlcg_infinite_find(owner,ref.plane_x,ref.plane_z);
    if (!owner) return false;
    for (int i=0;i<owner->faction_count;i++) {
        const TerritoryFaction *f=owner->factions+i;
        if (f->active && !memcmp(f->uid,ref.camp_uid,sizeof(ref.camp_uid))) {
            *out=(DollTarget){owner,ref,i};
            return true;
        }
    }
    return false;
}

static const QlcgDollLifeResident *interactive_doll(TerrainDollRef ref,DollTarget *target,float pose[5]) {
    if (g_app.screen!=SCREEN_IN_GAME || g_app.view_mode!=VIEW_MODE_TERRAIN || !g_app.region ||
        !find_doll(ref,target)) return NULL;
    const TerritoryFaction *f=target->owner->factions+target->faction;
    if (same_doll(ref,control.host.ref) && !pose)
        return qlcg_doll_life_find(target->owner,f,ref.id);
    float visible[5];
    const TerrainRenderer *tr=&g_app.terrain;
    if (!qlcg_terrain_doll_pose(tr,g_app.region,g_app.world,&ref,visible)) return NULL;
    float dx=visible[0]-tr->x, dz=visible[2]-tr->z;
    float dy=tr->y-qlcg_clampf(tr->y,visible[1],visible[1]+visible[4]);
    if (!(dx*dx+dy*dy+dz*dz <= QLCG_DOLL_INTERACT_REACH*QLCG_DOLL_INTERACT_REACH)) return NULL;
    if (pose) memcpy(pose,visible,sizeof(visible));
    return qlcg_doll_life_ensure_resident(target->owner,f,ref.id,g_app.clock.unix_ms);
}

const QlcgDollLifeResident *app_doll_interact(TerrainDollRef ref) {
    DollTarget target;
    return interactive_doll(ref,&target,NULL);
}

bool app_doll_controlled(TerrainDollRef *out) {
    if (!control.host.ref.id) return false;
    if (out) *out=control.host.ref;
    return true;
}

const QlcgDollLifeResident *app_doll_controlled_resident(void) {
    DollTarget host=control.host;
    return host.owner ? qlcg_doll_life_find(host.owner,host.owner->factions+host.faction,host.ref.id) : NULL;
}

const char *app_doll_controlled_name(void) {
    const QlcgDollLifeResident *resident=app_doll_controlled_resident();
    if (!resident) return NULL;
    return resident->name[0] ? resident->name : qlcg_i18n_content_text(qlcg_doll_role_name(resident->role));
}

bool app_doll_give(TerrainDollRef ref,InventoryPosition item) {
    DollTarget target;
    return !control.saving && interactive_doll(ref,&target,NULL) &&
           qlcg_doll_life_give(target.owner,target.faction,ref.id,&g_app.world->inventory,item);
}

bool app_doll_rename(TerrainDollRef ref,const char *name) {
    DollTarget target;
    return interactive_doll(ref,&target,NULL) &&
           qlcg_doll_life_set_name(target.owner,target.faction,ref.id,name);
}

void app_doll_sync_pose(void) {
    DollTarget host=control.host;
    if (!host.owner || host.owner!=g_app.world || !g_app.region ||
        g_app.view_mode!=VIEW_MODE_TERRAIN) return;
    const TerritoryFaction *f=host.owner->factions+host.faction;
    float x=g_app.region->center_x+g_app.terrain.x-(f->cx+.5f)*CHUNK_SIZE;
    float z=g_app.region->center_z+g_app.terrain.z-(f->cz+.5f)*CHUNK_SIZE;
    if (!qlcg_world_flat(host.owner) && host.owner->circumference>0)
        x=remainderf(x,host.owner->circumference);
    qlcg_doll_life_set_pose(host.owner,host.faction,host.ref.id,x,z,g_app.terrain.yaw);
}

static bool exchange_bag(DollTarget target,Inventory *bag) {
    return qlcg_doll_life_swap_inventory(target.owner,target.faction,target.ref.id,bag);
}

static void reset_control_input(void) {
    app_key_track_clear();
    ui_touch_reset(&g_app.touch);
    for (int i=0;i<ACT_COUNT;i++) app_action_set_virtual(i,false);
    TerrainRenderer *tr=&g_app.terrain;
    tr->last_jump_tap_ms=-1.0e30;
    tr->blocked_forward=false;
    tr->plant_contacts=0;
    memset(&tr->block_feedback,0,sizeof(tr->block_feedback));
    memset(tr->tool_swing_ms,0,sizeof(tr->tool_swing_ms));
    memset(tr->tool_swing_item,0,sizeof(tr->tool_swing_item));
    tr->probe_cache.valid=false;
    tr->aim=(AimProbeResult){0};
}

bool app_doll_possess(TerrainDollRef ref) {
    if (!g_app.world || g_app.world->opt.play_mode!=PLAY_MODE_LEISURE || control.saving) return false;
    if (same_doll(ref,control.host.ref)) return true;
    DollTarget target;
    float pose[5];
    if (!interactive_doll(ref,&target,pose) || !isfinite(pose[4]) || pose[4]<=.1f) return false;
    Inventory incoming={0};
    if (!exchange_bag(target,&incoming)) return false;
    TerrainRenderer *tr=&g_app.terrain;
    DollTarget old=control.host;
    if (old.owner) {
        app_doll_sync_pose();
        if (!exchange_bag(old,&g_app.world->inventory)) {
            exchange_bag(target,&incoming);
            return false;
        }
        qlcg_doll_life_set_controlled(old.owner,old.faction,old.ref.id,false);
    } else {
        control.origin=g_app.world;
        control.origin_bag=g_app.world->inventory;
        control.x=g_app.region->center_x+tr->x;
        control.y=tr->y;
        control.z=g_app.region->center_z+tr->z;
        control.yaw=tr->yaw;control.pitch=tr->pitch;
        control.vx=tr->vx;control.vy=tr->vy;control.vz=tr->vz;
        control.step=tr->step_offset;control.crouch=tr->crouch_offset;
        control.flying=tr->flying;control.on_ground=tr->on_ground;
    }
    control.host=target;
    qlcg_doll_life_set_controlled(target.owner,target.faction,ref.id,true);
    g_app.world->inventory=incoming;
    tr->controlled_doll=ref;
    tr->controlled_doll_height=pose[4];
    tr->x=pose[0];tr->y=pose[1]+pose[4]*.94f;tr->z=pose[2];
    tr->yaw=pose[3];tr->pitch=0;
    tr->vx=tr->vy=tr->vz=tr->step_offset=tr->crouch_offset=0;
    tr->flying=false;tr->on_ground=true;
    reset_control_input();
    return true;
}

void app_doll_control_reset(void) {
    DollTarget host=control.host;
    if (host.owner) qlcg_doll_life_set_controlled(host.owner,host.faction,host.ref.id,false);
    memset(&control,0,sizeof(control));
    g_app.terrain.controlled_doll=(TerrainDollRef){0};
    g_app.terrain.controlled_doll_height=0;
}

bool app_doll_release(void) {
    DollTarget host=control.host;
    if (!host.owner || !g_app.world || control.saving) return false;
    app_doll_sync_pose();
    bool resident_exists=app_doll_controlled_resident()!=NULL;
    if (resident_exists && !exchange_bag(host,&g_app.world->inventory)) return false;
    if (control.origin!=g_app.world || !g_app.region ||
        !qlcg_region_chunk_at(g_app.region,control.x-g_app.region->center_x,
                             control.z-g_app.region->center_z,NULL,NULL)) {
        int cx=(int)floorf(control.x/CHUNK_SIZE),cz=(int)floorf(control.z/CHUNK_SIZE);
        if (!app_doll_restore_chunk(control.origin,cx,cz)) {
            if (resident_exists) exchange_bag(host,&g_app.world->inventory);
            return false;
        }
    }
    TerrainRenderer *tr=&g_app.terrain;
    tr->x=control.x-g_app.region->center_x;
    tr->y=control.y;
    tr->z=control.z-g_app.region->center_z;
    tr->yaw=control.yaw;tr->pitch=control.pitch;
    tr->vx=control.vx;tr->vy=control.vy;tr->vz=control.vz;
    tr->step_offset=control.step;tr->crouch_offset=control.crouch;
    tr->flying=control.flying;tr->on_ground=control.on_ground;
    g_app.player_cx=g_app.region->center_cx;g_app.player_cz=g_app.region->center_cz;
    g_app.world->inventory=control.origin_bag;
    app_doll_control_reset();
    reset_control_input();
    return true;
}

bool app_doll_save_begin(float *x,float *y,float *z,float *yaw,float *pitch,bool *flying) {
    DollTarget host=control.host;
    if (!host.owner || !g_app.world || control.saving) return false;
    app_doll_sync_pose();
    if (!exchange_bag(host,&g_app.world->inventory)) return false;
    control.saving=true;
    if (control.origin!=g_app.world) control.plane_bag=control.origin->inventory;
    control.origin->inventory=control.origin_bag;
    *x=control.x;*y=control.y;*z=control.z;
    *yaw=control.yaw;*pitch=control.pitch;*flying=control.flying;
    return true;
}

MacroWorld *app_doll_save_world(void) {
    return control.saving ? control.origin : g_app.world;
}

void app_doll_save_end(void) {
    if (!control.saving) return;
    if (control.origin!=g_app.world) control.origin->inventory=control.plane_bag;
    g_app.world->inventory=(Inventory){0};
    exchange_bag(control.host,&g_app.world->inventory);
    control.saving=false;
}
