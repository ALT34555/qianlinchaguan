#ifndef QLCG_DOLL_LIFE_H
#define QLCG_DOLL_LIFE_H

#include "doll_sim.h"

enum { QLCG_LIFE_RESIDENTS=QLCG_CIV_POP_MAX, QLCG_LIFE_NEED_MAX=1000, QLCG_DOLL_NAME_BYTES=96 };
#define QLCG_LIFE_TEA_TABLE_M .72f
#define QLCG_LIFE_TEA_COMPANION_M 1.55f
typedef enum { DOLL_LIFE_NEAR,DOLL_LIFE_MID,DOLL_LIFE_DORMANT } QlcgDollLifeTier;
typedef enum {
    DOLL_LIFE_ROUTINE,DOLL_LIFE_REST,DOLL_LIFE_MEAL,DOLL_LIFE_TEA,DOLL_LIFE_HARVEST,DOLL_LIFE_SHIFT,
    DOLL_LIFE_TASK_COUNT
} QlcgDollLifeTask;
typedef enum {
    DOLL_LIFE_SCHEDULE,DOLL_LIFE_TIRED,DOLL_LIFE_HUNGRY,DOLL_LIFE_LONELY,DOLL_LIFE_CROP_READY,DOLL_LIFE_COVER,
    DOLL_LIFE_REASON_COUNT
} QlcgDollLifeReason;
typedef enum {
    DOLL_LIFE_NO_TARGET,DOLL_LIFE_HOME,DOLL_LIFE_HEARTH,DOLL_LIFE_CHAIR,DOLL_LIFE_FIELD,DOLL_LIFE_WATCH_POST,
    DOLL_LIFE_TEA_TABLE,DOLL_LIFE_GRANARY,
    DOLL_LIFE_TARGET_COUNT
} QlcgDollLifeTarget;

typedef struct {
    uint32_t id,partner_id;
    float x,z,yaw;
    int64_t until_minute;
    int32_t meal_day,tea_day;
    uint16_t satiety,energy,social,closeness;
    uint8_t slot,role,home,task,resume_task,reason,target;
    uint8_t routine_state,moving,indoor,work_plot;
    char name[QLCG_DOLL_NAME_BYTES];
    Inventory inventory;
    bool controlled;
} QlcgDollLifeResident;

typedef struct {
    int32_t day,food,wood,fiber;
    bool pending;
} QlcgDollLifeHarvest;

typedef struct {
    char faction_uid[17];
    int32_t faction_id,guard_day;
    int64_t last_minute;
    bool lot_furnishings; /* 最后编辑地块撤销时刷新默认值 */
    uint32_t chair_owner,guard_owner,revision,meals,teas,shifts,harvests;
    uint16_t active_slots;
    uint8_t tier;
    float chair_x,chair_z,chair_yaw;
    QlcgDollLifeHarvest crops[QLCG_CIV_FARMS];
    QlcgDollLifeResident residents[QLCG_LIFE_RESIDENTS];
} QlcgDollLifeCamp;

typedef struct {
    double update_ms;
    int camps[3],deferred;
} QlcgDollLifeStats;

void qlcg_doll_life_update(MacroWorld *world,double unix_ms,double real_dt,int player_cx,int player_cz);
void qlcg_doll_life_jump(MacroWorld *world,double unix_ms);
void qlcg_doll_life_free(MacroWorld *world);
const QlcgDollLifeCamp *qlcg_doll_life_camp(const MacroWorld *world,const TerritoryFaction *faction);
const QlcgDollLifeCamp *qlcg_doll_life_camp_at(const MacroWorld *world,int faction_id);
const QlcgDollLifeResident *qlcg_doll_life_find(const MacroWorld *world,const TerritoryFaction *faction,uint32_t id);
const QlcgDollLifeResident *qlcg_doll_life_ensure_resident(MacroWorld *world,const TerritoryFaction *faction,
                                                         uint32_t id,double unix_ms);
const Inventory *qlcg_doll_life_inventory(const MacroWorld *world,int faction_id,uint32_t resident_id);
bool qlcg_doll_life_set_name(MacroWorld *world,int faction_id,uint32_t resident_id,const char *name);
bool qlcg_doll_life_swap_inventory(MacroWorld *world,int faction_id,uint32_t resident_id,Inventory *inventory);
bool qlcg_doll_life_give(MacroWorld *world,int faction_id,uint32_t resident_id,Inventory *inventory,InventoryPosition position);
bool qlcg_doll_life_set_controlled(MacroWorld *world,int faction_id,uint32_t resident_id,bool controlled);
bool qlcg_doll_life_set_pose(MacroWorld *world,int faction_id,uint32_t resident_id,float x,float z,float yaw);
void qlcg_doll_life_seed_residents(const MacroWorld *world,const TerritoryFaction *faction,QlcgDollLifeCamp *camp);
bool qlcg_doll_life_attach(MacroWorld *world,const QlcgDollLifeCamp *camp);
QlcgDollLifeStats qlcg_doll_life_stats(const MacroWorld *world);
bool qlcg_doll_life_sample(const QlcgDollCamp *layout,const QlcgDollLifeResident *resident,double unix_ms,
                           int utc_offset,QlcgDollSample *out);
bool qlcg_doll_life_offer_harvest(MacroWorld *world,const TerritoryFaction *faction,int plot,int day,
                                  int food,int wood,int fiber);

#endif
