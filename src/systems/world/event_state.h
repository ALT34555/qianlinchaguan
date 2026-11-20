#ifndef QLCG_EVENT_STATE_H
#define QLCG_EVENT_STATE_H

#include "planet_macro.h"
#include <stdio.h>

enum { EVENT_CELL_CHANGED=1, EVENT_CELL_FLOODED=2 };

bool qlcg_trigger_scheduled_macro_event(MacroWorld *w,NaturalEventType type,float wx,float wz);
void qlcg_schedule_natural_event(MacroWorld *w,int day,bool allow);
bool qlcg_event_state_write(FILE *file,const MacroWorld *w);
bool qlcg_event_state_read(FILE *file,long limit,MacroWorld *w,int cells);

static inline void qlcg_event_mark_cell(MacroWorld *w,int cell,NaturalEventType type) {
    w->event_cells[cell]|=EVENT_CELL_CHANGED;
    if (type==NAT_EVENT_FLOOD || type==NAT_EVENT_TYPHOON) w->event_cells[cell]|=EVENT_CELL_FLOODED;
}

#endif
