#include "astronomy_events.h"
#include "../calendar/jd_astronomy.h"
#include <math.h>
#define DAY_MS 86400000.0
#define DEGREES 57.29577951308232
static double next_shower(const AstraEventState *s,int index,double after) {
    if(s->flat) {
        int64_t slot=(int64_t)floor(after/(8*DAY_MS));double half;
        double peak=qlcg_astra_flat_shower(s,index,slot,&half),start=(peak-half)*3600000;
        if(start<=after){peak=qlcg_astra_flat_shower(s,index,slot+1,&half);start=(peak-half)*3600000;}
        return start;
    }
    const AstraShower *shower=&s->showers[index];
    double jd=after/DAY_MS+2440587.5,target=(shower->peak-shower->width*.5)*DEGREES;
    double lon=qlcg_sun_apparent_longitude_deg(jd);
    double guess=jd+qlcg_norm360(target-lon)/.98564736;
    double start=qlcg_solar_term_jde(target,guess);
    if((start-2440587.5)*DAY_MS<=after)start=qlcg_solar_term_jde(target,start+365.2422);
    return (start-2440587.5)*DAY_MS;
}
static double next_comet(const AstraEventState *s,int index,double after) {
    const AstraComet *comet=&s->comets[index];
    if(s->flat) {
        int64_t slot=(int64_t)floor(after/(128*DAY_MS));
        double start=(qlcg_astra_flat_comet(s,index,slot)-comet->window*.5)*DAY_MS;
        if(start<=after)start=(qlcg_astra_flat_comet(s,index,slot+1)-comet->window*.5)*DAY_MS;
        return start;
    }
    double base=comet->epoch-comet->window*.5;
    return (base+(floor((after/DAY_MS-base)/comet->period)+1)*comet->period)*DAY_MS;
}
static void next_eclipse(AstraEventState *s,const WorldCreatorOptions *opt,const CalendarClock *clock,double after) {
    CalendarClock c=*clock;c.unix_ms=after;
    bool found=qlcg_astra_eclipse_find(opt,&c,&s->broadcast_eclipse);
    s->eclipse_refresh=found?s->broadcast_eclipse.end+1:after+30*DAY_MS;
}
void qlcg_astra_starts_notify(AstraEventState *s,const WorldCreatorOptions *opt,
                              const CalendarClock *clock,double from,AstraStartNotice notice,void *context) {
    if(!s||!opt||!clock||!notice||!opt->astronomy_enabled)return;
    double to=clock->unix_ms;
    if(!isfinite(from)||!isfinite(to)||to<=from||clock->paused||clock->time_speed<=0)return;
    if(to-from>ASTRA_MAX_STEP_MS){s->broadcast_ready=false;return;}
    bool reset=!s->broadcast_ready||fabs(s->broadcast_cursor-from)>.5||s->broadcast_mode!=clock->mode;
    reset=reset||s->broadcast_tilt!=opt->axial_tilt_deg||s->broadcast_prograde!=opt->prograde||s->broadcast_flat!=s->flat;
    if(reset) {
        for(int i=0;i<s->shower_count;i++)s->shower_start[i]=next_shower(s,i,from);
        for(int i=0;i<s->comet_count;i++)s->comet_start[i]=next_comet(s,i,from);
        next_eclipse(s,opt,clock,from);
        s->broadcast_ready=true;s->broadcast_mode=clock->mode;
        s->broadcast_tilt=opt->axial_tilt_deg;s->broadcast_prograde=opt->prograde;s->broadcast_flat=s->flat;
    }
    s->broadcast_cursor=to;double after=fmax(from,s->high_water);
    for(int i=0;i<s->shower_count;i++)if(s->shower_start[i]<=to) {
        AstraStart e={ASTRA_START_SHOWER,s->shower_start[i],i,0,0};
        if(e.time>after)notice(&e,context);s->shower_start[i]=next_shower(s,i,to);
    }
    for(int i=0;i<s->comet_count;i++)if(s->comet_start[i]<=to) {
        AstraStart e={ASTRA_START_COMET,s->comet_start[i],i,0,0};
        if(e.time>after)notice(&e,context);s->comet_start[i]=next_comet(s,i,to);
    }
    AstraEclipse *e=&s->broadcast_eclipse;
    if(e->type&&e->start>after&&e->start<=to) {
        AstraStart start={ASTRA_START_ECLIPSE,e->start,0,e->kind,e->type};notice(&start,context);
    }
    if(to>=s->eclipse_refresh)next_eclipse(s,opt,clock,to);
}
