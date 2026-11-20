#include "event_state.h"
#include "core/binary.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { EVENT_STATE_HEADER=11, EVENT_STATE_ACTIVE_RECORD=34, EVENT_STATE_CELL_RECORD=34 };

static void codec_event(QlCodec *io,ActiveNaturalEvent *event) {
    event->automatic=qlcg_io_bool(io,event->automatic);
    event->type=(NaturalEventType)qlcg_io_uint(io,event->type,1);
    event->wx=qlcg_io_f32(io,event->wx);
    event->wz=qlcg_io_f32(io,event->wz);
    event->vx=qlcg_io_f32(io,event->vx);
    event->vz=qlcg_io_f32(io,event->vz);
    event->radius=qlcg_io_f32(io,event->radius);
    event->intensity=qlcg_io_f32(io,event->intensity);
    event->age=qlcg_io_f32(io,event->age);
    event->duration=qlcg_io_f32(io,event->duration);
    if ((unsigned)event->type>=NAT_EVENT_COUNT || !isfinite(event->wx) || !isfinite(event->wz) ||
        fabsf(event->wx/CHUNK_SIZE)>INT_MAX/2 || fabsf(event->wz/CHUNK_SIZE)>INT_MAX/2 ||
        !isfinite(event->vx) || !isfinite(event->vz) || !isfinite(event->intensity) ||
        !isfinite(event->radius) || event->radius<=0 || !isfinite(event->duration) ||
        !isfinite(event->age) || event->age<0 || event->age>=event->duration) io->ok=false;
}

static bool has_fields(const MacroWorld *w) {
    return w->height && w->spill && w->level && w->moisture && w->temperature &&
        w->desert_factor && w->forest_bonus && w->water;
}

static void codec_cell(QlCodec *io,const MacroWorld *w,int cell,bool apply) {
    float *fields[]={w?w->height:NULL,w?w->spill:NULL,w?w->level:NULL,w?w->moisture:NULL,
        w?w->temperature:NULL,w?w->desert_factor:NULL,w?w->forest_bonus:NULL};
    for (size_t i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
        float value=qlcg_io_f32(io,io->reading?0:fields[i][cell]);
        if (!isfinite(value)) io->ok=false;
        if (apply) fields[i][cell]=value;
    }
    uint8_t water=(uint8_t)qlcg_io_uint(io,io->reading?0:w->water[cell],1);
    if (water>3) io->ok=false;
    if (apply) w->water[cell]=water;
}

bool qlcg_event_state_write(FILE *file,const MacroWorld *w) {
    unsigned active=0,changes=0;
    for (int i=0;i<MAX_ACTIVE_EVENTS;i++) active+=w->events[i].active;
    if (w->event_cells)
        for (int i=0;i<w->cell_count;i++) changes+=w->event_cells[i]!=0;
    if (w->auto_events && !w->event_day_set && !active && !changes) return true;
    if (changes && !has_fields(w)) return false;
    size_t size=EVENT_STATE_HEADER+(size_t)EVENT_STATE_ACTIVE_RECORD*active+(size_t)EVENT_STATE_CELL_RECORD*changes;
    uint8_t *buf=malloc(size);
    if (!buf) return false;
    QlCodec io=qlcg_codec_mem(buf,false,size);
    qlcg_io_bool(&io,w->auto_events);
    qlcg_io_bool(&io,w->event_day_set);
    qlcg_io_i32(&io,w->last_event_day);
    qlcg_io_uint(&io,active,1);
    for (int i=0;i<MAX_ACTIVE_EVENTS;i++) if (w->events[i].active) {
        ActiveNaturalEvent event=w->events[i];
        codec_event(&io,&event);
    }
    qlcg_io_uint(&io,changes,4);
    for (int i=0;changes && i<w->cell_count;i++) if (w->event_cells[i]) {
        qlcg_io_uint(&io,i,4);
        qlcg_io_uint(&io,w->event_cells[i],1);
        codec_cell(&io,w,i,false);
    }
    bool ok=io.ok && io.bytes==size && qlcg_section_write_buf(file,"NATE",1,buf,size);
    free(buf);
    return ok;
}

static void read_cells(QlCodec *io,MacroWorld *w,int cells,unsigned count,uint8_t *changed) {
    int64_t previous=-1;
    for (unsigned i=0;i<count && io->ok;i++) {
        uint32_t cell=(uint32_t)qlcg_io_uint(io,0,4);
        uint8_t mask=(uint8_t)qlcg_io_uint(io,0,1);
        if (cell>=(unsigned)cells || (int64_t)cell<=previous ||
            (mask!=EVENT_CELL_CHANGED && mask!=(EVENT_CELL_CHANGED|EVENT_CELL_FLOODED))) {
            io->ok=false;
            break;
        }
        if (changed) changed[cell]=mask;
        codec_cell(io,w,(int)cell,changed!=NULL);
        previous=cell;
    }
}

bool qlcg_event_state_read(FILE *file,long limit,MacroWorld *w,int cells) {
    long start=ftell(file);
    if (start==limit) return true;
    if (start<0 || limit<start || cells<=0 || (w && w->cell_count!=cells)) return false;
    size_t max_size=QLCG_SECTION_HEADER+EVENT_STATE_HEADER+EVENT_STATE_ACTIVE_RECORD*MAX_ACTIVE_EVENTS+
        (size_t)EVENT_STATE_CELL_RECORD*cells;
    if ((size_t)(limit-start)>max_size) return false;
    uint8_t *buf=NULL;
    size_t size=0;
    long end;
    if (!qlcg_section_open_alloc(file,"NATE",limit,1,NULL,&buf,&size,&end) || end!=limit) {
        free(buf);
        return false;
    }
    QlCodec io=qlcg_codec_mem(buf,true,size);
    bool enabled=qlcg_io_bool(&io,false),day_set=qlcg_io_bool(&io,false);
    int day=qlcg_io_i32(&io,0);
    unsigned active=(unsigned)qlcg_io_uint(&io,0,1);
    ActiveNaturalEvent events[MAX_ACTIVE_EVENTS]={0};
    if (active>MAX_ACTIVE_EVENTS || (day_set && (day==INT_MIN || day>INT_MAX-2440588))) io.ok=false;
    for (unsigned i=0;i<active && io.ok;i++) {
        codec_event(&io,events+i);
        if (!io.ok) break;
        events[i].active=true;
        snprintf(events[i].title,sizeof(events[i].title),"%s",qlcg_nat_event_name(events[i].type));
    }
    unsigned count=(unsigned)qlcg_io_uint(&io,0,4);
    if (count>(unsigned)cells || (count && w && !has_fields(w))) io.ok=false;
    QlCodec cells_io=io;
    read_cells(&io,NULL,cells,count,NULL);
    bool ok=io.ok && io.bytes==size;
    if (ok && w) {
        uint8_t *changed=count?calloc((size_t)cells,1):NULL;
        if (count && !changed) ok=false;
        else {
            if (count) read_cells(&cells_io,w,cells,count,changed);
            free(w->event_cells);
            w->event_cells=changed;
            w->auto_events=enabled;
            w->event_day_set=day_set;
            w->last_event_day=day;
            memcpy(w->events,events,sizeof(events));
            if (count) qlcg_world_rebuild_atlas_fields(w);
        }
    }
    free(buf);
    return ok;
}
