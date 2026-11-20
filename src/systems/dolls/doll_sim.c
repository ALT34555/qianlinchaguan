#include "../world/world_lot.h"
#include "doll_sim.h"
#include "../calendar/jd_astronomy.h"
#include "../calendar/lunar_calendar.h"
#include "../world/world_settlement.h"
#include "../world/model/building_model.h"
#include "../world/world_civ_economy.h"
#include <math.h>
#include <string.h>

QlcgDollDayKind qlcg_doll_day_kind(double unix_ms,int utc_offset_minutes) {
    enum { DAY_CACHE=32 };
    static struct {int jdn;QlcgDollDayKind kind;} cache[DAY_CACHE];
    double day=floor((unix_ms+(double)utc_offset_minutes*60000)/QLCG_MS_PER_DAY);
    if(!isfinite(day))return DOLL_DAY_NORMAL;
    int weekday=qlcg_weekday_of(QLCG_UNIX_EPOCH_JDN+(int)fmod(day,7));
    QlcgDollDayKind kind=weekday==0||weekday==6?DOLL_DAY_REST:DOLL_DAY_NORMAL;
    if(day< -1000000||day>=1000000)return kind;
    int jdn=QLCG_UNIX_EPOCH_JDN+(int)day;
    unsigned slot=(unsigned)jdn%DAY_CACHE;
    if(cache[slot].jdn==jdn)return cache[slot].kind;
    QlcgChineseDate lunar;
    if(qlcg_lunar_from_jdn(jdn,&lunar)) {
        bool lunar_festival=!lunar.is_leap_month&&((lunar.month==1&&(lunar.day<=3||lunar.day==15))||
            (lunar.month==5&&lunar.day==5)||
            (lunar.month==8&&lunar.day==15)||(lunar.month==9&&lunar.day==9));
        if(lunar_festival||!strcmp(lunar.solar_term,"清明"))kind=DOLL_DAY_FESTIVAL;
    }
    cache[slot].jdn=jdn;
    cache[slot].kind=kind;
    return kind;
}

uint32_t qlcg_doll_resident_id(const TerritoryFaction *f,int slot) {
    uint32_t h=2166136261u;
    for(int i=0;i<16&&f->uid[i];i++)h=(h^(unsigned char)f->uid[i])*16777619u;
    if(h<=12) h^=0x80000000u;
    return (h^(uint32_t)(slot+1))*16777619u;
}

void qlcg_doll_prepare_camp(const MacroWorld *w,const TerritoryFaction *f,QlcgDollCamp *camp) {
    *camp=(QlcgDollCamp){.faction=f,.has_world=w!=NULL,.campfire={.8f,.8f}};
    if(!f||!f->active)return;
    camp->yaw=qlcg_settlement_yaw(f);
    camp->cosine=cosf(camp->yaw);camp->sine=sinf(camp->yaw);
    camp->part_count=qlcg_settlement_layout(f,camp->parts);
    if(w && w->lots) {
        QlcgSettlementPart homes[QLCG_SETTLEMENT_PARTS];
        int n=qlcg_lot_homes(w,f,homes,QLCG_SETTLEMENT_PARTS);
        if(n || qlcg_lot_local(w,f->cx,f->cz)) {
            camp->part_count=n;memset(camp->parts,0,sizeof(camp->parts));
            memcpy(camp->parts,homes,(size_t)n*sizeof(*homes));
        }
    }
    for(int i=0;i<camp->part_count;i++) {
        const QlcgSettlementPart *p=camp->parts+i;
        const QlcgBuildingModel *model=qlcg_lot_model(p->model);
        if(model->kind!=QLCG_BUILDING_YARD||!strstr(model->unit_id,"campfire"))continue;
        camp->campfire[0]=p->x*camp->cosine+p->z*camp->sine-QLCG_SETTLEMENT_PLAZA_X;
        camp->campfire[1]=-p->x*camp->sine+p->z*camp->cosine;
        break;
    }
    QlcgSettlementPart fire;
    if(qlcg_lot_facility(w,f,false,&fire)) {
        camp->campfire[0]=fire.x*camp->cosine+fire.z*camp->sine-QLCG_SETTLEMENT_PLAZA_X;
        camp->campfire[1]=-fire.x*camp->sine+fire.z*camp->cosine;
    }
    if(w)camp->plot_count=qlcg_civ_farms(w,f,camp->plots,QLCG_CIV_FARMS);
}

bool qlcg_doll_sample_resident(const QlcgDollCamp *camp,int slot,int role,uint32_t id,double unix_ms,
                              int utc_offset_minutes,QlcgDollSample *out) {
    const TerritoryFaction *f=camp->faction;
    if(!f||!f->active||!out||slot<0||role<0||role>=QLCG_DOLL_RESIDENTS||!isfinite(unix_ms))return false;
    memset(out,0,sizeof(*out));out->id=id;out->slot=slot;out->role=role;
    const QlcgDollType *type=qlcg_doll_type_for(role,id);if(!type)return false;
    double seconds=fmod(unix_ms/1000+(double)utc_offset_minutes*60,86400);if(seconds<0)seconds+=86400;
    QlcgDollAIContext context={0};context.resident_id=out->id;context.local_hour=seconds/3600;context.work_available=true;
    context.day_kind=qlcg_doll_day_kind(unix_ms,utc_offset_minutes);
    out->type=type;out->day_kind=context.day_kind;

    memcpy(context.home,type->home,sizeof(context.home));memcpy(context.work,type->work,sizeof(context.work));
    memcpy(context.via,type->via,sizeof(context.via));memcpy(context.facing,type->facing,sizeof(context.facing));
    memcpy(context.meeting,type->meeting,sizeof(context.meeting));
    float c=camp->cosine,s=camp->sine;
    memcpy(context.campfire,camp->campfire,sizeof(context.campfire));
    if(role==DOLL_ROLE_GATHER)context.work[0]-=QLCG_SETTLEMENT_PLAZA_X;
    if(camp->has_world&&role==DOLL_ROLE_GATHER) {
        const QlcgFarmPlot *plots=camp->plots;
        context.work_available=camp->plot_count>0;
        if(camp->plot_count) {
            float x=(float)(plots[0].cx-f->cx)*CHUNK_SIZE+plots[0].work_x;
            float z=(float)(plots[0].cz-f->cz)*CHUNK_SIZE+plots[0].work_z;
            context.work[0]=x*c+z*s-QLCG_SETTLEMENT_PLAZA_X;context.work[1]=-x*s+z*c;
        }
    }
    QlcgDollAIResult action;
    if(!qlcg_doll_ai_sample(type,&context,&action))return false;
    if(action.state==DOLL_STATE_SLEEP) {
        out->x=camp->parts[0].x;out->z=camp->parts[0].z;
        action.moving=false;out->indoor=camp->part_count>0;
    } else {
        action.x+=QLCG_SETTLEMENT_PLAZA_X;
        out->x=action.x*c-action.z*s;out->z=action.x*s+action.z*c;
    }
    out->yaw=action.yaw-camp->yaw;
    out->state=action.state;out->moving=action.moving;out->type_id=type->id;out->type_name=type->name;out->scale=type->scale;
    return true;
}
bool qlcg_doll_sample_camp(const QlcgDollCamp *camp,int slot,double unix_ms,
                          int utc_offset_minutes,QlcgDollSample *out) {
    if(!camp->faction || slot<0 || slot>=QLCG_DOLL_RESIDENTS) return false;
    return qlcg_doll_sample_resident(camp,slot,slot,qlcg_doll_resident_id(camp->faction,slot),unix_ms,
                                    utc_offset_minutes,out);
}
bool qlcg_doll_sample_world(const MacroWorld *w,const TerritoryFaction *f,int slot,double unix_ms,
                           int utc_offset_minutes,QlcgDollSample *out) {
    if(!f||!f->active||!out||slot<0||slot>=QLCG_DOLL_RESIDENTS||!isfinite(unix_ms))return false;
    QlcgDollCamp camp;
    qlcg_doll_prepare_camp(slot==DOLL_ROLE_GATHER?w:NULL,f,&camp);
    return qlcg_doll_sample_camp(&camp,slot,unix_ms,utc_offset_minutes,out);
}

bool qlcg_doll_sample(const TerritoryFaction *f,int slot,double unix_ms,
                     int utc_offset_minutes,QlcgDollSample *out) {
    return qlcg_doll_sample_world(NULL,f,slot,unix_ms,utc_offset_minutes,out);
}
void qlcg_doll_update_gait(QlcgDollGait *gait,QlcgDollSample *sample,float distance,float dt) {
    float scale=fmaxf(.5f,sample->scale);
    float speed=dt>0 ? fminf(4.5f,distance/(dt*scale)) : gait->speed;
    gait->speed+=(speed-gait->speed)*fminf(1,dt*12);
    if(gait->speed<.04f) gait->speed=0;
    sample->gait=gait->speed<1.4f ? gait->speed/1.4f : fminf(2,1+(gait->speed-1.4f)/2.45f);
    gait->phase+=distance/(qlcg_doll_gait_cycle(sample->gait)*scale);
    gait->phase-=floorf(gait->phase);
    sample->gait_phase=gait->phase;
}

QlcgDollAction qlcg_doll_render_action(const QlcgDollSample *sample) {
    switch(sample->state) {
        case DOLL_STATE_WALK:
        case DOLL_STATE_APPROACH:
        case DOLL_STATE_RETURN:
        case DOLL_STATE_PROTECT:
        case DOLL_STATE_PATROL:
        case DOLL_STATE_WATCH:
            if(sample->gait>1) return QLCG_DOLL_RUN;
            return sample->gait>0?QLCG_DOLL_WALK:QLCG_DOLL_IDLE;
        case DOLL_STATE_WORK: return QLCG_DOLL_WORK;
        case DOLL_STATE_CAMPFIRE: return QLCG_DOLL_REST;
        case DOLL_STATE_SLEEP: return QLCG_DOLL_SLEEP;
        default: return QLCG_DOLL_IDLE;
    }
}
float qlcg_doll_render_phase(const QlcgDollSample *sample,double real_seconds) {
    QlcgDollAction action=qlcg_doll_render_action(sample);
    double cycles=real_seconds/qlcg_doll_action_duration(action);
    if(action==QLCG_DOLL_WALK||action==QLCG_DOLL_RUN) return sample->gait_phase;
    cycles+=(sample->id&255)/256.0;
    return isfinite(cycles)?(float)(cycles-floor(cycles)):0;
}
void qlcg_doll_render_pose(const QlcgDollSample *sample,double real_seconds,float out[QLCG_DOLL_BONE_COUNT*16]) {
    QlcgDollAction action=qlcg_doll_render_action(sample);
    if(action==QLCG_DOLL_WALK||action==QLCG_DOLL_RUN) {
        double idle=real_seconds/qlcg_doll_action_duration(QLCG_DOLL_IDLE)+(sample->id&255)/256.0;
        qlcg_doll_gait_pose(sample->gait_phase,(float)(idle-floor(idle)),sample->gait,out);
    } else qlcg_doll_pose(action,qlcg_doll_render_phase(sample,real_seconds),out);
}
float qlcg_doll_labor_efficiency(const MacroWorld *w,const TerritoryFaction *f) {
    return qlcg_civ_labor_efficiency(w,f);
}
const char *qlcg_doll_role_name(int role) {
    static const char *names[]={"农耕","加工晾晒","巡领"};
    return role>=0&&role<QLCG_DOLL_RESIDENTS?names[role]:"村民";
}
const char *qlcg_doll_state_name(int state) {
    static const char *names[]={"行走","劳作","营火休息","屋内睡眠","守夜","守夜迎查","守夜回岗","守夜护田","守夜巡逻"};
    return state>=0&&state<(int)(sizeof(names)/sizeof(*names))?names[state]:"休息";
}
