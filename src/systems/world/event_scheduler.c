#include "event_state.h"
#include "climate_season.h"
#include "core/math_util.h"
#include <math.h>

enum { EVENT_DAY_ODDS=32, EVENT_SITE_PROBES=24 };

static bool suitable_site(const MacroWorld *w,int cell,NaturalEventType type,const CalendarSnapshot *date) {
    if (type==NAT_EVENT_EARTHQUAKE) return w->boundary && w->boundary[cell]!=0;
    bool ocean=w->water[cell]==1,land=w->water[cell]==0 || w->water[cell]==3;
    QlcgClimateSeason climate;
    qlcg_climate_season_at(w,cell,date,&climate);
    float temp=climate.temperature,moisture=climate.moisture;
    switch (type) {
    case NAT_EVENT_TYPHOON:
        return ocean && temp>=24.f && moisture>=.55f;
    case NAT_EVENT_DESERTIFY:
        return land && temp>=12.f && moisture<=.2f;
    case NAT_EVENT_FLOOD:
        return land && w->height[cell]<350.f && moisture>=.65f && w->river_width[cell]>0.f;
    case NAT_EVENT_COLD_WAVE:
        return land && temp<=2.f;
    case NAT_EVENT_WILDFIRE:
        return land && temp>=18.f && moisture<=.4f && w->forest_bonus[cell]>=.25f;
    default:
        return false;
    }
}

void qlcg_schedule_natural_event(MacroWorld *w,int day,bool allow) {
    if (w->event_day_set && day<=w->last_event_day) return;
    w->event_day_set=true;
    w->last_event_day=day;
    bool free_slot=false;
    for (int i=0;i<MAX_ACTIVE_EVENTS;i++) {
        ActiveNaturalEvent *event=w->events+i;
        if (event->automatic) event->active=false;
        if (!event->active) free_slot=true;
    }
    if (!allow || !w->auto_events || !free_slot || w->cell_count<=0 || !w->height ||
        !w->water || !w->temperature || !w->moisture || !w->river_width || !w->forest_bonus) return;
    uint32_t key=qlcg_hash_u32(w->opt.seed^qlcg_hash_u32((uint32_t)day)^0x8a37f4bdu);
    if (key%EVENT_DAY_ODDS!=0) return;
    CalendarSnapshot date={.jdn=day+2440588,.day_frac=.5};
    for (unsigned slot=0;slot<EVENT_SITE_PROBES;slot++) {
        uint32_t sample=qlcg_hash_u32(key^qlcg_hash_u32(slot));
        NaturalEventType type=(NaturalEventType)(sample%NAT_EVENT_COUNT);
        int cell=(int)(qlcg_hash_u32(sample)%(uint32_t)w->cell_count);
        if (!suitable_site(w,cell,type,&date)) continue;
        float x,z;
        qlcg_world_cell_center(w,cell,&x,&z);
        qlcg_trigger_scheduled_macro_event(w,type,x,z);
        return;
    }
}
