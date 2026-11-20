#ifndef QLCG_DOLL_SIM_H
#define QLCG_DOLL_SIM_H
#include "doll_ai.h"
#include "../world/world_civ_economy.h"
#include "../world/model/camp_layout.h"
#include "../world/model/doll_model.h"

enum { QLCG_DOLL_RESIDENTS = 3 };
enum { DOLL_ROLE_GATHER, DOLL_ROLE_PROCESS, DOLL_ROLE_PATROL };
enum { DOLL_STATE_WALK, DOLL_STATE_WORK, DOLL_STATE_CAMPFIRE, DOLL_STATE_SLEEP, DOLL_STATE_WATCH,
       DOLL_STATE_APPROACH, DOLL_STATE_RETURN, DOLL_STATE_PROTECT, DOLL_STATE_PATROL };
typedef struct {
    uint32_t id;
    int slot,role,state;
    float x,z,yaw;
    bool moving,indoor;
    const char *type_id,*type_name;
    const QlcgDollType *type;
    QlcgDollDayKind day_kind;
    float scale,gait,gait_phase;
} QlcgDollSample;

typedef struct {float speed,phase;} QlcgDollGait;
void qlcg_doll_update_gait(QlcgDollGait *gait,QlcgDollSample *sample,float distance,float dt);

QlcgDollDayKind qlcg_doll_day_kind(double unix_ms,int utc_offset_minutes);
uint32_t qlcg_doll_resident_id(const TerritoryFaction *f,int slot);
bool qlcg_doll_sample(const TerritoryFaction *f,int slot,double unix_ms,
                     int utc_offset_minutes,QlcgDollSample *out);
typedef struct {
    const TerritoryFaction *faction;
    QlcgSettlementPart parts[QLCG_SETTLEMENT_PARTS];
    QlcgFarmPlot plots[QLCG_CIV_FARMS];
    int part_count,plot_count;
    float yaw,cosine,sine,campfire[2];
    bool has_world;
} QlcgDollCamp;
void qlcg_doll_prepare_camp(const MacroWorld *world,const TerritoryFaction *f,QlcgDollCamp *camp);
bool qlcg_doll_sample_camp(const QlcgDollCamp *camp,int slot,double unix_ms,
                          int utc_offset_minutes,QlcgDollSample *out);
bool qlcg_doll_sample_resident(const QlcgDollCamp *camp,int slot,int role,uint32_t id,double unix_ms,
                              int utc_offset_minutes,QlcgDollSample *out);
bool qlcg_doll_sample_world(const MacroWorld *world,const TerritoryFaction *f,int slot,double unix_ms,
                           int utc_offset_minutes,QlcgDollSample *out);
QlcgDollAction qlcg_doll_render_action(const QlcgDollSample *sample);
float qlcg_doll_render_phase(const QlcgDollSample *sample,double real_seconds);
void qlcg_doll_render_pose(const QlcgDollSample *sample,double real_seconds,float out[QLCG_DOLL_BONE_COUNT*16]);
float qlcg_doll_labor_efficiency(const MacroWorld *w,const TerritoryFaction *f);
const char *qlcg_doll_role_name(int role);
const char *qlcg_doll_state_name(int state);
#endif
