#include "../world/world_lot.h"
#include "doll_life.h"
#include "doll_watch.h"
#include "../world/world_settlement.h"
#include "../world/model/building_model.h"
#include "../calendar/calendar.h"
#include "core/math_util.h"
#include "core/binary.h"
#include "platform/platform.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { LIFE_DAY_MINUTES=1440, LIFE_NEAR_METERS=256, LIFE_MID_METERS=768, LIFE_NEAR_CAMPS=4,
       LIFE_MID_MINUTES=120, LIFE_FAR_MINUTES=1440, LIFE_PARK_GAP=360 };
static const double life_budget_ms=2.5;
static const double tier_budget_ms[3]={1.5,.7,.3};

struct QlcgDollLife {
    QlcgDollLifeCamp *camp[MAX_FACTIONS];
    QlcgDollLifeStats stats;
    double elapsed;
    unsigned tick,cursor[3];
};

typedef struct {
    int64_t minute;
    int utc_day,day,clock,offset;
} LifeTime;
typedef struct {
    QlcgDollCamp camp;
    QlcgDollWatchContext route;
    float hearth_x,hearth_z;
    bool no_chair;
} LifeLayout;

static bool clock_minute(double unix_ms,int64_t *minute) {
    double t=floor(unix_ms/60000.0);
    if (!isfinite(t) || t<((double)INT_MIN+1)*LIFE_DAY_MINUTES ||
        t>((double)INT_MAX-2440588)*LIFE_DAY_MINUTES) return false;
    *minute=(int64_t)t;
    return true;
}
static int minute_of_day(int64_t minute) {
    int t=(int)(minute%LIFE_DAY_MINUTES);
    return t<0?t+LIFE_DAY_MINUTES:t;
}
static LifeTime life_time(const MacroWorld *w,const TerritoryFaction *f,int64_t minute) {
    int offset=qlcg_world_flat(w)||w->circumference<=0?0:qlcg_timezone_offset_min(f->wx,w->circumference);
    int64_t local=minute+offset;
    int clock=minute_of_day(local);
    return (LifeTime){minute,(int)((minute-minute_of_day(minute))/LIFE_DAY_MINUTES),
        (int)((local-clock)/LIFE_DAY_MINUTES),clock,offset};
}
static struct QlcgDollLife *life_state(MacroWorld *w) {
    if (!w->doll_life) w->doll_life=calloc(1,sizeof(*w->doll_life));
    return w->doll_life;
}
static QlcgDollLifeCamp *camp_mutable(const MacroWorld *w,const TerritoryFaction *f) {
    if (!w || !w->doll_life || !f || !f->active || f->id<0 || f->id>=MAX_FACTIONS) return NULL;
    QlcgDollLifeCamp *camp=w->doll_life->camp[f->id];
    return camp && !memcmp(camp->faction_uid,f->uid,sizeof(f->uid))?camp:NULL;
}

const QlcgDollLifeCamp *qlcg_doll_life_camp(const MacroWorld *w,const TerritoryFaction *f) {
    return camp_mutable(w,f);
}
const QlcgDollLifeCamp *qlcg_doll_life_camp_at(const MacroWorld *w,int faction_id) {
    if (!w || faction_id<0 || faction_id>=w->faction_count || faction_id>=MAX_FACTIONS) return NULL;
    return camp_mutable(w,w->factions+faction_id);
}
static void routine(QlcgDollLifeResident *r);
static QlcgDollLifeResident *resident_at(QlcgDollLifeCamp *camp,uint32_t id);
static void sync_population(const MacroWorld *w,const TerritoryFaction *f,QlcgDollLifeCamp *camp) {
    unsigned mask=0;
    int remaining=qlcg_civ_npc_count(w,f);
    for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) {
        const QlcgDollLifeResident *r=camp->residents+i;
        if (r->name[0] || r->controlled) {mask|=1u<<i;remaining--;}
    }
    for (int i=0;i<QLCG_LIFE_RESIDENTS && remaining>0;i++) {
        if (!(mask&(1u<<i))) {mask|=1u<<i;remaining--;}
    }
    if (mask==camp->active_slots) return;
    camp->active_slots=(uint16_t)mask;
    for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) {
        QlcgDollLifeResident *r=camp->residents+i;
        if (!(mask&(1u<<i))) {
            if (r->id==camp->chair_owner) camp->chair_owner=0;
            if (r->id==camp->guard_owner) camp->guard_owner=0;
            if (r->task!=DOLL_LIFE_ROUTINE) routine(r);
        }
        if (!(mask&(1u<<i))) {r->partner_id=0;continue;}
        if (resident_at(camp,r->partner_id)) continue;
        int mate=i^1;
        r->partner_id=(mask&(1u<<mate))?camp->residents[mate].id:0;
    }
}
static QlcgDollLifeResident *resident_at(QlcgDollLifeCamp *camp,uint32_t id) {
    if (id) for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) if ((camp->active_slots&(1u<<i)) && camp->residents[i].id==id) return camp->residents+i;
    return NULL;
}
const QlcgDollLifeResident *qlcg_doll_life_find(const MacroWorld *w,const TerritoryFaction *f,uint32_t id) {
    QlcgDollLifeCamp *camp=camp_mutable(w,f);
    return camp?resident_at(camp,id):NULL;
}

static QlcgDollLifeResident *resident_by_faction(const MacroWorld *w,int faction_id,uint32_t id) {
    if (!w || faction_id<0 || faction_id>=w->faction_count || faction_id>=MAX_FACTIONS) return NULL;
    QlcgDollLifeCamp *camp=camp_mutable(w,w->factions+faction_id);
    return camp?resident_at(camp,id):NULL;
}

const Inventory *qlcg_doll_life_inventory(const MacroWorld *w,int faction_id,uint32_t id) {
    const QlcgDollLifeResident *r=resident_by_faction(w,faction_id,id);
    return r?&r->inventory:NULL;
}

bool qlcg_doll_life_set_name(MacroWorld *w,int faction_id,uint32_t id,const char *name) {
    QlcgDollLifeResident *r=resident_by_faction(w,faction_id,id);
    if (!r || !name) return false;
    while (isspace((unsigned char)*name)) name++;
    size_t length=strlen(name);
    while (length && isspace((unsigned char)name[length-1])) length--;
    if (length>=sizeof(r->name) || !qlcg_utf8_valid(name,length)) return false;
    if (!length && !r->slot && !w->factions[faction_id].is_player) {
        name=w->factions[faction_id].leader_name;
        length=strlen(name);
    }
    memmove(r->name,name,length);
    memset(r->name+length,0,sizeof(r->name)-length);
    return true;
}

bool qlcg_doll_life_swap_inventory(MacroWorld *w,int faction_id,uint32_t id,Inventory *inventory) {
    QlcgDollLifeResident *r=resident_by_faction(w,faction_id,id);
    if (!r || !inventory || !inventory_validate(inventory) || !inventory_validate(&r->inventory)) return false;
    Inventory next=r->inventory;
    r->inventory=*inventory;
    *inventory=next;
    return true;
}

bool qlcg_doll_life_give(MacroWorld *w,int faction_id,uint32_t id,Inventory *inventory,InventoryPosition position) {
    QlcgDollLifeResident *r=resident_by_faction(w,faction_id,id);
    if (!r || r->controlled || !inventory || inventory==&r->inventory) return false;
    Inventory source=*inventory;
    InventoryItem item=inventory_at(&source,&position);
    if (!item || !inventory_discard(&source,position) || !inventory_add(&r->inventory,item)) return false;
    *inventory=source;
    return true;
}

bool qlcg_doll_life_set_controlled(MacroWorld *w,int faction_id,uint32_t id,bool controlled) {
    QlcgDollLifeResident *r=resident_by_faction(w,faction_id,id);
    if (!r) return false;
    r->controlled=controlled;
    return true;
}

bool qlcg_doll_life_set_pose(MacroWorld *w,int faction_id,uint32_t id,float x,float z,float yaw) {
    QlcgDollLifeResident *r=resident_by_faction(w,faction_id,id);
    if (!r || !isfinite(x) || !isfinite(z) || !isfinite(yaw)) return false;
    r->x=x;r->z=z;r->yaw=yaw;
    r->moving=false;r->indoor=false;
    return true;
}
bool qlcg_doll_life_attach(MacroWorld *w,const QlcgDollLifeCamp *camp) {
    if (!w || !camp || camp->faction_id<0 || camp->faction_id>=w->faction_count || camp->faction_id>=MAX_FACTIONS) return false;
    const TerritoryFaction *f=w->factions+camp->faction_id;
    if (!f->active || f->id!=camp->faction_id || memcmp(camp->faction_uid,f->uid,sizeof(f->uid))) return false;
    struct QlcgDollLife *state=life_state(w);
    if (!state) return false;
    QlcgDollLifeCamp *copy=malloc(sizeof(*copy));
    if (!copy) return false;
    *copy=*camp;
    sync_population(w,f,copy);
    free(state->camp[camp->faction_id]);
    state->camp[camp->faction_id]=copy;
    return true;
}
void qlcg_doll_life_free(MacroWorld *w) {
    if (!w || !w->doll_life) return;
    for (int i=0;i<MAX_FACTIONS;i++) free(w->doll_life->camp[i]);
    free(w->doll_life);
    w->doll_life=NULL;
}
QlcgDollLifeStats qlcg_doll_life_stats(const MacroWorld *w) {
    return w && w->doll_life?w->doll_life->stats:(QlcgDollLifeStats){0};
}

static void layout_for(const MacroWorld *w,const TerritoryFaction *f,LifeLayout *layout) {
    *layout=(LifeLayout){0};
    qlcg_doll_prepare_camp(w,f,&layout->camp);
    QlcgDollCamp *camp=&layout->camp;
    float x=QLCG_SETTLEMENT_PLAZA_X+1.5f,z=2.f;
    layout->hearth_x=x*camp->cosine-z*camp->sine;
    layout->hearth_z=x*camp->sine+z*camp->cosine;
    QlcgSettlementPart fire,tea;
    if(qlcg_lot_facility(w,f,false,&fire)) {layout->hearth_x=fire.x;layout->hearth_z=fire.z;}
    layout->no_chair=qlcg_lot_local(w,f->cx,f->cz) && !qlcg_lot_facility(w,f,true,&tea);
    for (int i=0;i<camp->part_count&&layout->route.obstacle_count<QLCG_WATCH_OBSTACLES;i++) {
        const QlcgSettlementPart *part=camp->parts+i;
        const QlcgBuildingModel *model=qlcg_lot_model(part->model);
        if (!model) continue;
        if (model->kind==QLCG_BUILDING_ROOM) {
            float half=3.85f*part->scale+.35f;
            layout->route.obstacles[layout->route.obstacle_count++]=(QlcgWatchObstacle){part->x,part->z,half,half};
        } else if (model->kind==QLCG_BUILDING_YARD) {
            float center[2],half[2];
            if (qlcg_settlement_yard_footprint(part,center,half))
                layout->route.obstacles[layout->route.obstacle_count++]=(QlcgWatchObstacle){center[0],center[1],half[0]+.35f,half[1]+.35f};
        }
    }
}
static void setup_resident(QlcgDollLifeResident *r,const QlcgDollCamp *layout,LifeTime time,int slot,int rooms) {
    const TerritoryFaction *f=layout->faction;
    *r=(QlcgDollLifeResident){.id=qlcg_doll_resident_id(f,slot),.partner_id=qlcg_doll_resident_id(f,slot^1),
        .slot=(uint8_t)slot,.role=(uint8_t)(slot%QLCG_DOLL_RESIDENTS),.home=(uint8_t)(rooms?slot%rooms:0),
        .meal_day=INT_MIN,.tea_day=INT_MIN,.satiety=(uint16_t)(580+slot*21),
        .energy=(uint16_t)(slot==DOLL_ROLE_PATROL?160:900-slot*8),.social=(uint16_t)(280+slot*22)};
    QlcgDollSample sample;
    if (qlcg_doll_sample_resident(layout,slot,r->role,r->id,time.minute*60000.0,time.offset,&sample)) {
        r->x=sample.x;r->z=sample.z;r->yaw=sample.yaw;r->routine_state=(uint8_t)sample.state;r->indoor=sample.indoor;
        if (sample.indoor && rooms) {
            r->x=layout->parts[r->home].x;r->z=layout->parts[r->home].z;
        }
    }
}

static void seed_residents(const QlcgDollCamp *layout,LifeTime time,QlcgDollLifeCamp *camp) {
    int rooms=0;
    while (rooms<layout->part_count) {
        const QlcgBuildingModel *model=qlcg_lot_model(layout->parts[rooms].model);
        if (!model || model->kind!=QLCG_BUILDING_ROOM) break;
        rooms++;
    }
    for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) setup_resident(camp->residents+i,layout,time,i,rooms);
    if (!layout->faction->is_player)
        memcpy(camp->residents[0].name,layout->faction->leader_name,sizeof(camp->residents[0].name));
}

void qlcg_doll_life_seed_residents(const MacroWorld *w,const TerritoryFaction *f,QlcgDollLifeCamp *camp) {
    QlcgDollCamp layout;
    qlcg_doll_prepare_camp(w,f,&layout);
    seed_residents(&layout,life_time(w,f,camp->last_minute),camp);
}

static QlcgDollLifeCamp *ensure_camp(MacroWorld *w,const TerritoryFaction *f,LifeTime time,const LifeLayout *layout) {
    QlcgDollLifeCamp *existing=camp_mutable(w,f);
    if (existing) return existing;
    QlcgDollLifeCamp *camp=calloc(1,sizeof(*camp));
    if (!camp) return NULL;
    memcpy(camp->faction_uid,f->uid,sizeof(f->uid));
    camp->faction_id=f->id;camp->guard_day=INT_MIN;
    camp->last_minute=time.minute-1;
    float c=layout->camp.cosine,s=layout->camp.sine;
    camp->chair_x=11.55f*c+2.25f*s;camp->chair_z=11.55f*s-2.25f*c;camp->chair_yaw=-layout->camp.yaw;
    for (int p=0;p<QLCG_CIV_FARMS;p++) camp->crops[p].day=INT_MIN;
    seed_residents(&layout->camp,time,camp);
    sync_population(w,f,camp);
    free(w->doll_life->camp[f->id]);
    w->doll_life->camp[f->id]=camp;
    return camp;
}

const QlcgDollLifeResident *qlcg_doll_life_ensure_resident(MacroWorld *w,const TerritoryFaction *f,
                                                         uint32_t id,double unix_ms) {
    if (!w || !f || !f->active || f->id < 0 || f->id >= w->faction_count ||
        f->id >= MAX_FACTIONS || w->factions + f->id != f || !id) return NULL;
    QlcgDollLifeCamp *camp=camp_mutable(w,f);
    if (!camp) {
        int slot=0;
        int count=qlcg_civ_npc_count(w,f);
        for (;slot<count;slot++) if (qlcg_doll_resident_id(f,slot)==id) break;
        int64_t minute;
        if (slot==count || !clock_minute(unix_ms,&minute) || !life_state(w)) return NULL;
        LifeLayout layout;
        layout_for(w,f,&layout);
        camp=ensure_camp(w,f,life_time(w,f,minute),&layout);
    }
    return camp ? resident_at(camp,id) : NULL;
}

static void routine(QlcgDollLifeResident *r) {
    r->task=DOLL_LIFE_ROUTINE;r->resume_task=DOLL_LIFE_ROUTINE;r->reason=DOLL_LIFE_SCHEDULE;
    r->target=DOLL_LIFE_NO_TARGET;r->until_minute=0;
}
static bool deliver(MacroWorld *w,const TerritoryFaction *f,QlcgDollLifeCamp *camp,int plot) {
    QlcgDollLifeHarvest *crop=camp->crops+plot;
    if (!crop->pending || !qlcg_civ_economy_credit_harvest(w,f,crop->food,crop->wood,crop->fiber)) return false;
    crop->pending=false;
    camp->harvests++;
    for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) {
        QlcgDollLifeResident *r=camp->residents+i;
        if (!r->controlled && r->task==DOLL_LIFE_HARVEST && r->work_plot==plot) routine(r);
    }
    return true;
}
bool qlcg_doll_life_offer_harvest(MacroWorld *w,const TerritoryFaction *f,int plot,int day,
                                  int food,int wood,int fiber) {
    QlcgDollLifeCamp *camp=camp_mutable(w,f);
    if (!camp || plot<0 || plot>=QLCG_CIV_FARMS || day==INT_MIN || food<0 || wood<0 || fiber<0) return false;
    QlcgDollLifeHarvest *crop=camp->crops+plot;
    if (day<=crop->day) return true;
    if (crop->pending && !deliver(w,f,camp,plot)) return false;
    if (camp->tier==DOLL_LIFE_DORMANT) {
        if (!qlcg_civ_economy_credit_harvest(w,f,food,wood,fiber)) return false;
        *crop=(QlcgDollLifeHarvest){.day=day};
        camp->harvests++;
        camp->revision++;
        return true;
    }
    *crop=(QlcgDollLifeHarvest){day,qlcg_clampi(food,0,QLCG_CIV_RESOURCE_MAX),
        qlcg_clampi(wood,0,QLCG_CIV_RESOURCE_MAX),qlcg_clampi(fiber,0,QLCG_CIV_RESOURCE_MAX),true};
    camp->revision++;
    return true;
}
static const QlcgFarmPlot *plot_for(const QlcgDollCamp *layout,int plot) {
    for (int i=0;i<layout->plot_count;i++) if (layout->plots[i].plot==plot) return layout->plots+i;
    return NULL;
}
static bool harvest_assigned(const QlcgDollLifeCamp *camp,int plot) {
    for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) {
        const QlcgDollLifeResident *r=camp->residents+i;
        if ((camp->active_slots&(1u<<i)) && !r->controlled && r->task==DOLL_LIFE_HARVEST && r->work_plot==plot) return true;
    }
    return false;
}
static uint16_t add_need(int value,int delta) {
    return (uint16_t)qlcg_clampi(value+delta,0,QLCG_LIFE_NEED_MAX);
}
static void use_minutes(QlcgDollLifeResident *r,int elapsed,bool ration,bool dormant) {
    r->satiety=add_need(r->satiety,-elapsed);
    if (dormant && ration && r->satiety<500) r->satiety=650;
    bool sleeping=r->indoor || (!r->moving && r->routine_state==DOLL_STATE_SLEEP);
    int energy=sleeping?4*elapsed:dormant?elapsed:-elapsed;
    r->energy=add_need(r->energy,energy);
    r->social=add_need(r->social,-elapsed/3);
}
static void park_camp(MacroWorld *w,const TerritoryFaction *f,QlcgDollLifeCamp *camp,LifeTime time) {
    int64_t elapsed=time.minute-camp->last_minute;
    if (elapsed<0) return;
    bool changed=camp->tier!=DOLL_LIFE_DORMANT;
    camp->tier=DOLL_LIFE_DORMANT;
    bool ration=qlcg_civ_economy_ration(w,f,time.utc_day);
    for (int i=0;i<QLCG_CIV_FARMS;i++) if (deliver(w,f,camp,i)) changed=true;
    if (elapsed) for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) {
        if (!(camp->active_slots&(1u<<i))) continue;
        QlcgDollLifeResident *r=camp->residents+i;
        if (r->task!=DOLL_LIFE_ROUTINE) r->until_minute+=elapsed;
        if (r->controlled) continue;
        use_minutes(r,(int)fmin(1440.0,(double)elapsed),ration,true);
        changed=true;
    }
    camp->last_minute=time.minute;
    if (changed) camp->revision++;
}
static void start_task(QlcgDollLifeResident *r,QlcgDollLifeTask task,QlcgDollLifeReason reason,
                       QlcgDollLifeTarget target,int64_t until) {
    r->resume_task=r->task;
    r->task=(uint8_t)task;r->reason=(uint8_t)reason;r->target=(uint8_t)target;
    r->until_minute=until;r->moving=true;
}

static void home_door(const LifeLayout *layout,int home,float *x,float *z) {
    const QlcgSettlementPart *room=layout->camp.parts+home;
    const QlcgSettlementPart *door=NULL;
    for (int i=0;i<layout->camp.part_count;i++)
        if (layout->camp.parts[i].parent==home) {door=layout->camp.parts+i;break;}
    float dx=door?door->x-room->x:room->x;
    float dz=door?door->z-room->z:room->z;
    if (!door && fabsf(dx)+fabsf(dz)<.01f) {dx=layout->hearth_x;dz=layout->hearth_z;}
    float component=fmaxf(fabsf(dx),fabsf(dz));
    float scale=component>.01f?(4.28f*room->scale)/component:0.f;
    *x=room->x+dx*scale;*z=room->z+dz*scale;
}
static bool walk_to(const LifeLayout *layout,QlcgDollLifeResident *r,float x,float z,float yaw) {
    if (r->indoor) {
        if (r->home<layout->camp.part_count) home_door(layout,r->home,&r->x,&r->z);
        r->indoor=false;
    }
    float nx,nz;
    bool direct=qlcg_doll_detour(&layout->route,r->x,r->z,x,z,&nx,&nz);
    float dx=nx-r->x,dz=nz-r->z,distance=hypotf(dx,dz);
    if (distance>2.4f) {
        r->x+=dx*2.4f/distance;r->z+=dz*2.4f/distance;
    } else {
        r->x=nx;r->z=nz;
    }
    r->moving=!(direct && distance<=2.4f);
    r->yaw=r->moving && distance>.001f?atan2f(dx,dz):yaw;
    return !r->moving;
}
static void sleep_inside(const LifeLayout *layout,QlcgDollLifeResident *r) {
    if(!layout->camp.part_count) {r->moving=false;r->indoor=false;r->routine_state=DOLL_STATE_SLEEP;return;}
    if (!r->indoor) {
        float x,z;home_door(layout,r->home,&x,&z);
        if (!walk_to(layout,r,x,z,r->yaw)) return;
    }
    r->x=layout->camp.parts[r->home].x;r->z=layout->camp.parts[r->home].z;
    r->indoor=true;r->moving=false;r->routine_state=DOLL_STATE_SLEEP;
}
static void act_routine(const LifeLayout *layout,QlcgDollLifeResident *r,const QlcgDollSample *sample,bool guard) {
    if (sample->indoor || (sample->state==DOLL_STATE_WATCH&&!guard)) {
        sleep_inside(layout,r);
        return;
    }
    float lane=((float)(r->slot/QLCG_DOLL_RESIDENTS)-1.5f)*.85f;
    float x=sample->x-lane*layout->camp.sine,z=sample->z+lane*layout->camp.cosine;
    r->routine_state=(uint8_t)sample->state;
    walk_to(layout,r,x,z,sample->yaw);
}

static void guard_schedule(QlcgDollLifeCamp *camp,LifeTime time) {
    if (time.clock>=360 && time.clock<1260) {
        for (int i=0;i<QLCG_LIFE_RESIDENTS;i++)
            if (!camp->residents[i].controlled && camp->residents[i].task==DOLL_LIFE_SHIFT)
                routine(camp->residents+i);
        camp->guard_owner=0;
        return;
    }
    int day=time.day-(time.clock<360);
    QlcgDollLifeResident *guard=resident_at(camp,camp->guard_owner);
    if (!guard || guard->controlled || camp->guard_day!=day) {
        if (guard && !guard->controlled && guard->task==DOLL_LIFE_SHIFT) routine(guard);
        int start=camp->guard_day==INT_MIN?DOLL_ROLE_PATROL:(int)((uint32_t)day%QLCG_LIFE_RESIDENTS);
        guard=NULL;
        for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) {
            int slot=(start+i)%QLCG_LIFE_RESIDENTS;
            QlcgDollLifeResident *candidate=camp->residents+slot;
            if (!(camp->active_slots&(1u<<slot)) || candidate->controlled || candidate->task!=DOLL_LIFE_ROUTINE) continue;
            if (!guard || candidate->role==DOLL_ROLE_PATROL) guard=candidate;
            if (candidate->role==DOLL_ROLE_PATROL) break;
        }
        camp->guard_owner=guard ? guard->id : 0;camp->guard_day=day;
        if (!guard) return;
    }
    if (guard->energy>=220) return;
    QlcgDollLifeResident *cover=NULL;
    for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) {
        QlcgDollLifeResident *candidate=camp->residents+i;
        if ((camp->active_slots&(1u<<i)) && candidate!=guard && !candidate->controlled && candidate->task==DOLL_LIFE_ROUTINE && candidate->energy>=420 &&
            (!cover || candidate->energy>cover->energy)) cover=candidate;
    }
    if (!cover) return;
    if (camp->chair_owner==guard->id) camp->chair_owner=0;
    start_task(guard,DOLL_LIFE_REST,DOLL_LIFE_TIRED,DOLL_LIFE_HOME,time.minute+75);
    int shift_end=(time.clock<360?360:1800)-time.clock;
    start_task(cover,DOLL_LIFE_SHIFT,DOLL_LIFE_COVER,DOLL_LIFE_WATCH_POST,time.minute+shift_end);
    camp->guard_owner=cover->id;
    camp->shifts++;
}
static bool tea_possible(const QlcgDollLifeResident *r,LifeTime time) {
    return r && !r->controlled && r->task==DOLL_LIFE_ROUTINE && r->tea_day!=time.day && r->energy>=330 && r->satiety>=160;
}
static void choose_task(QlcgDollLifeCamp *camp,const LifeLayout *layout,QlcgDollLifeResident *r,
                        LifeTime time,bool ration,bool working) {
    if (r->task!=DOLL_LIFE_ROUTINE || r->id==camp->guard_owner) return;
    if (r->energy<220) {
        start_task(r,DOLL_LIFE_REST,DOLL_LIFE_TIRED,DOLL_LIFE_HOME,time.minute+75);
    } else if (ration && r->satiety<=625 && r->meal_day!=time.day && time.clock>=360 && time.clock<1260) {
        start_task(r,DOLL_LIFE_MEAL,DOLL_LIFE_HUNGRY,DOLL_LIFE_HEARTH,time.minute+12);
    } else if (r->role==DOLL_ROLE_GATHER && working) {
        for (int i=0;i<QLCG_CIV_FARMS;i++) if (camp->crops[i].pending && plot_for(&layout->camp,i) &&
            !harvest_assigned(camp,i)) {
            r->work_plot=(uint8_t)i;
            start_task(r,DOLL_LIFE_HARVEST,DOLL_LIFE_CROP_READY,DOLL_LIFE_FIELD,time.minute+8);
            return;
        }
    }
    if (layout->no_chair || camp->chair_owner || time.clock<780 || time.clock>=1200 || r->social>530 || !tea_possible(r,time)) return;
    QlcgDollLifeResident *partner=resident_at(camp,r->partner_id);
    if (!tea_possible(partner,time) || partner->id==camp->guard_owner) return;
    camp->chair_owner=r->id;
    start_task(r,DOLL_LIFE_TEA,DOLL_LIFE_LONELY,DOLL_LIFE_CHAIR,time.minute+24);
    start_task(partner,DOLL_LIFE_TEA,DOLL_LIFE_LONELY,DOLL_LIFE_TEA_TABLE,time.minute+24);
}
static void tea_step(QlcgDollLifeCamp *camp,const LifeLayout *layout,QlcgDollLifeResident *r,LifeTime time) {
    bool owner=r->id==camp->chair_owner;
    QlcgDollLifeResident *mate=resident_at(camp,r->partner_id);
    if (!camp->chair_owner || !mate || mate->controlled || mate->task!=DOLL_LIFE_TEA) {
        if (owner) camp->chair_owner=0;
        routine(r);
        return;
    }
    float x=camp->chair_x,z=camp->chair_z,yaw=camp->chair_yaw;
    if (!owner) {x+=QLCG_LIFE_TEA_COMPANION_M*sinf(yaw);z+=QLCG_LIFE_TEA_COMPANION_M*cosf(yaw);yaw+=QLCG_PI;}
    bool approaching=r->moving!=0;
    if (walk_to(layout,r,x,z,yaw) && approaching) r->until_minute=time.minute+24;
    if (!owner || time.minute<r->until_minute || time.minute<mate->until_minute) return;
    if (!r->moving && !mate->moving) {
        r->social=add_need(r->social,500);mate->social=add_need(mate->social,500);
        r->closeness=add_need(r->closeness,130);mate->closeness=add_need(mate->closeness,130);
        r->tea_day=mate->tea_day=time.day;
        camp->teas++;
    } else if (time.minute-r->until_minute<75) return;
    camp->chair_owner=0;
    routine(r);routine(mate);
}
static void harvest_step(MacroWorld *w,const TerritoryFaction *f,QlcgDollLifeCamp *camp,const LifeLayout *layout,
                          QlcgDollLifeResident *r,LifeTime time) {
    if (!camp->crops[r->work_plot].pending) {routine(r);return;}
    const QlcgFarmPlot *plot=plot_for(&layout->camp,r->work_plot);
    if (r->target==DOLL_LIFE_FIELD && plot) {
        float x=(float)(plot->cx-f->cx)*CHUNK_SIZE+plot->work_x;
        float z=(float)(plot->cz-f->cz)*CHUNK_SIZE+plot->work_z;
        bool approaching=r->moving!=0;
        if (!walk_to(layout,r,x,z,plot->yaw)) {
            if (time.minute-r->until_minute>=150) routine(r);
            return;
        }
        if (approaching) r->until_minute=time.minute+8;
        if (time.minute<r->until_minute) return;
        r->target=DOLL_LIFE_GRANARY;r->moving=true;
        return;
    }
    r->target=DOLL_LIFE_GRANARY;
    if (walk_to(layout,r,layout->hearth_x,layout->hearth_z,r->yaw)) {
        deliver(w,f,camp,r->work_plot);
    }
}
static void advance_resident(MacroWorld *w,const TerritoryFaction *f,QlcgDollLifeCamp *camp,const LifeLayout *layout,
                             QlcgDollLifeResident *r,LifeTime time,bool ration) {
    QlcgDollSample sample;
    int schedule_role=r->id==camp->guard_owner?DOLL_ROLE_PATROL:r->role;
    if (!qlcg_doll_sample_resident(&layout->camp,r->slot,schedule_role,r->id,time.minute*60000.0,time.offset,&sample)) return;
    bool working=sample.state==DOLL_STATE_WORK || (time.clock>=420 && time.clock<1080);
    choose_task(camp,layout,r,time,ration,working);
    switch (r->task) {
    case DOLL_LIFE_REST: {
        bool approaching=r->moving!=0;
        sleep_inside(layout,r);
        if (!r->moving && approaching) r->until_minute=time.minute+75;
        if (!r->moving && r->energy>=680 && time.minute>=r->until_minute) routine(r);
        break;
    }
    case DOLL_LIFE_MEAL: {
        float angle=r->slot*(QLCG_TAU/QLCG_LIFE_RESIDENTS);
        bool approaching=r->moving!=0;
        bool arrived=walk_to(layout,r,layout->hearth_x+cosf(angle)*1.35f,layout->hearth_z+sinf(angle)*1.35f,-angle-QLCG_PI*.5f);
        if (arrived && approaching) r->until_minute=time.minute+12;
        if (arrived && time.minute>=r->until_minute && ration) {
            r->satiety=QLCG_LIFE_NEED_MAX;r->meal_day=time.day;camp->meals++;
            routine(r);
        } else if (time.minute-r->until_minute>=120) routine(r);
        break;
    }
    case DOLL_LIFE_TEA:
        tea_step(camp,layout,r,time);
        break;
    case DOLL_LIFE_HARVEST:
        harvest_step(w,f,camp,layout,r,time);
        break;
    case DOLL_LIFE_SHIFT:
    case DOLL_LIFE_ROUTINE:
        act_routine(layout,r,&sample,r->id==camp->guard_owner);
        break;
    default:
        routine(r);
        break;
    }
}
static void step_camp(MacroWorld *w,TerritoryFaction *f,QlcgDollLifeCamp *camp,int tier,LifeTime time) {
    if (camp) sync_population(w,f,camp);
    if (tier==DOLL_LIFE_DORMANT) {
        if (camp) park_camp(w,f,camp,time);
        return;
    }
    if (camp && time.minute<camp->last_minute) return;
    LifeLayout layout;layout_for(w,f,&layout);
    if (!camp) camp=ensure_camp(w,f,time,&layout);
    if (!camp) return;
    if(w->lots || camp->lot_furnishings) {
        camp->lot_furnishings=w->lots!=NULL;
        QlcgSettlementPart chair;
        if(qlcg_lot_facility(w,f,true,&chair)) {
            camp->chair_x=chair.x;camp->chair_z=chair.z;camp->chair_yaw=-chair.yaw;
        } else {
            float c=layout.camp.cosine,s=layout.camp.sine;
            camp->chair_x=11.55f*c+2.25f*s;camp->chair_z=11.55f*s-2.25f*c;camp->chair_yaw=-layout.camp.yaw;
            if(layout.no_chair) camp->chair_owner=0;
        }
        int rooms=0;
        while(rooms<layout.camp.part_count && qlcg_lot_model(layout.camp.parts[rooms].model)->kind==QLCG_BUILDING_ROOM) rooms++;
        for(int i=0;i<QLCG_LIFE_RESIDENTS;i++) camp->residents[i].home=(uint8_t)(rooms?i%rooms:0);
    }
    if (camp->tier==DOLL_LIFE_DORMANT || time.minute-camp->last_minute>LIFE_PARK_GAP)
        park_camp(w,f,camp,time);
    camp->tier=(uint8_t)tier;
    int elapsed=(int)(time.minute-camp->last_minute);
    bool ration=qlcg_civ_economy_ration(w,f,time.utc_day);
    int social_elapsed=(int)(time.minute/3-camp->last_minute/3);
    for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) {
        QlcgDollLifeResident *r=camp->residents+i;
        if (!(camp->active_slots&(1u<<i))) continue;
        if (r->controlled) {
            if (r->task!=DOLL_LIFE_ROUTINE) r->until_minute+=elapsed;
            continue;
        }
        int prior_social=r->social;
        use_minutes(r,elapsed,ration,false);
        r->social=add_need(prior_social,-social_elapsed);
    }
    guard_schedule(camp,time);
    for (int i=0;i<QLCG_LIFE_RESIDENTS;i++)
        if ((camp->active_slots&(1u<<i)) && !camp->residents[i].controlled) advance_resident(w,f,camp,&layout,camp->residents+i,time,ration);
    camp->last_minute=time.minute;
    camp->revision++;
}

static double faction_distance_sq(const MacroWorld *w,const TerritoryFaction *f,int cx,int cz) {
    double x=(double)f->cx-cx,z=(double)f->cz-cz;
    if (!qlcg_world_flat(w) && w->equator_chunks>0) x=remainder(x,w->equator_chunks);
    return (x*x+z*z)*((double)CHUNK_SIZE*CHUNK_SIZE);
}
void qlcg_doll_life_update(MacroWorld *w,double unix_ms,double real_dt,int player_cx,int player_cz) {
    int64_t minute;
    if (!w || !w->opt.artificial_enabled || w->faction_count<=0 || !clock_minute(unix_ms,&minute)) return;
    struct QlcgDollLife *state=life_state(w);
    if (!state) return;
    if (isfinite(real_dt) && real_dt>0) state->elapsed+=fmin(1.0,real_dt);
    if (state->tick && state->elapsed<1.0) return;
    state->elapsed=fmod(state->elapsed,1.0);
    state->tick++;
    double started=qlcg_now_ms(),total_deadline=started+life_budget_ms;
    QlcgDollLifeStats stats={0};
    int tiers[MAX_FACTIONS]={0};
    int closest[LIFE_NEAR_CAMPS]={-1,-1,-1,-1};
    double distances[LIFE_NEAR_CAMPS]={INFINITY,INFINITY,INFINITY,INFINITY};
    int count=qlcg_clampi(w->faction_count,0,MAX_FACTIONS);
    for (int i=0;i<count;i++) {
        const TerritoryFaction *f=w->factions+i;
        tiers[i]=-1;
        if (!f->active || f->id!=i) continue;
        double distance=faction_distance_sq(w,f,player_cx,player_cz);
        tiers[i]=distance<=LIFE_MID_METERS*LIFE_MID_METERS?DOLL_LIFE_MID:DOLL_LIFE_DORMANT;
        if (distance>LIFE_NEAR_METERS*LIFE_NEAR_METERS) continue;
        for (int j=0;j<LIFE_NEAR_CAMPS;j++) if (distance<distances[j]) {
            for (int k=LIFE_NEAR_CAMPS-1;k>j;k--) {distances[k]=distances[k-1];closest[k]=closest[k-1];}
            distances[j]=distance;closest[j]=i;
            break;
        }
    }
    for (int i=0;i<LIFE_NEAR_CAMPS && closest[i]>=0;i++) tiers[closest[i]]=DOLL_LIFE_NEAR;
    for (int i=0;i<count;i++) if (tiers[i]>=0) stats.camps[tiers[i]]++;
    for (int tier=DOLL_LIFE_NEAR;tier<=DOLL_LIFE_DORMANT;tier++) {
        double deadline=fmin(total_deadline,qlcg_now_ms()+tier_budget_ms[tier]);
        unsigned start=state->cursor[tier]%count;
        for (int step=0;step<count;step++) {
            unsigned id=(start+(unsigned)step)%count;
            if (tiers[id]!=tier) continue;
            TerritoryFaction *f=w->factions+id;
            QlcgDollLifeCamp *camp=camp_mutable(w,f);
            if (tier==DOLL_LIFE_DORMANT && (!camp || (camp->tier==tier && minute-camp->last_minute<LIFE_FAR_MINUTES))) continue;
            if (tier==DOLL_LIFE_MID && camp && camp->tier==tier && minute-camp->last_minute<LIFE_MID_MINUTES) continue;
            if (qlcg_now_ms()>=deadline) {stats.deferred++;continue;}
            step_camp(w,f,camp,tier,life_time(w,f,minute));
            state->cursor[tier]=id+1u;
        }
    }
    stats.update_ms=qlcg_now_ms()-started;
    state->stats=stats;
}
void qlcg_doll_life_jump(MacroWorld *w,double unix_ms) {
    int64_t minute;
    if (!w || !w->doll_life || !clock_minute(unix_ms,&minute)) return;
    for (int i=0;i<MAX_FACTIONS;i++) {
        QlcgDollLifeCamp *camp=w->doll_life->camp[i];
        if (!camp || minute<=camp->last_minute) continue;
        int64_t elapsed=minute-camp->last_minute;
        for (int j=0;j<QLCG_LIFE_RESIDENTS;j++) if (camp->residents[j].task!=DOLL_LIFE_ROUTINE)
            camp->residents[j].until_minute+=elapsed;
        camp->last_minute=minute;
        camp->revision++;
    }
}

bool qlcg_doll_life_sample(const QlcgDollCamp *layout,const QlcgDollLifeResident *r,double unix_ms,
                           int offset,QlcgDollSample *out) {
    if (!layout || !layout->faction || !r || !out) return false;
    const QlcgDollType *type=qlcg_doll_type_for(r->role,r->id);
    if (!type) return false;
    int state=r->routine_state;
    if (r->moving) state=DOLL_STATE_WALK;
    else if (r->task==DOLL_LIFE_REST) state=DOLL_STATE_SLEEP;
    else if (r->task==DOLL_LIFE_MEAL || r->task==DOLL_LIFE_TEA) state=DOLL_STATE_CAMPFIRE;
    else if (r->task==DOLL_LIFE_HARVEST) state=DOLL_STATE_WORK;
    else if (r->task==DOLL_LIFE_SHIFT) state=DOLL_STATE_WATCH;
    *out=(QlcgDollSample){.id=r->id,.slot=r->slot,.role=r->role,.state=state,.x=r->x,.z=r->z,.yaw=r->yaw,
        .moving=r->moving,.indoor=r->indoor,.type_id=type->id,.type_name=type->name,.type=type,
        .day_kind=qlcg_doll_day_kind(unix_ms,offset),.scale=type->scale};
    return true;
}
