#include "app_state.h"
#include "systems/world/astronomy_events.h"
#include "systems/world/astronomy_eclipses.h"
#include "terrain3d/sky_climate.h"
static int notice_policy(int category) {
    return (g_app.world->astronomy_broadcast>>(category*2))&3;
}
static void notice_sky(double time,AstraFrame *f) {
    MacroWorld *w=g_app.world;CalendarClock c=g_app.clock;c.unix_ms=time;
    double x=(double)g_app.player_cx*CHUNK_SIZE,z=(double)g_app.player_cz*CHUNK_SIZE;
    if(g_app.region){x=g_app.region->center_x+g_app.terrain.x;z=g_app.region->center_z+g_app.terrain.z;}
    double lon=x/fmax(1,w->circumference)*6.283185307179586;
    double lat=fmax(-1.570796326794897,fmin(1.570796326794897,-z/fmax(1,qlcg_world_depth(w))*3.141592653589793));
    qlcg_astra_sample(&w->opt,&c,lon,lat,NULL,f);
}
static bool visible_star(const AstraFrame *f,const float *direction,bool daylight) {
    double altitude=0;for(int k=0;k<3;k++)altitude+=direction[k]*f->up[k];
    SkyClimate sky;qlcg_sky_climate_astra(&g_app.clock,0,0,NULL,false,0,0,f,&sky);
    return altitude>0&&(daylight||sky.star>.001f);
}
static void event_time(double ms,char *out,size_t cap) {
    CalendarClock c=g_app.clock;c.unix_ms=ms;CalendarSnapshot snap;
    int zone=qlcg_world_flat(g_app.world)?0:qlcg_timezone_offset_min((float)g_app.player_cx*CHUNK_SIZE,g_app.world->circumference);
    qlcg_calendar_snapshot(&c,zone,&snap);
    int y=snap.gregorian.year,m=snap.gregorian.month,d=snap.gregorian.day;
    if(c.mode==CALENDAR_YUAN){y=snap.yuan.year;m=snap.yuan.month;d=snap.yuan.day;}
    if(c.mode==CALENDAR_LUNAR){y=snap.chinese.year;m=snap.chinese.month;d=snap.chinese.day;}
    const char *leap=c.mode==CALENDAR_LUNAR&&snap.chinese.is_leap?tr("astra.leap"):"";
    snprintf(out,cap,"%s %d-%02d-%02d%s %02d:%02d:%02d (UTC%+d:%02d)",qlcg_calendar_mode_name(c.mode),y,m,d,leap,snap.gregorian.hour,snap.gregorian.minute,snap.gregorian.second,zone/60,abs(zone)%60);
}
static void meteor_notice(const AstraMeteor *m,void *context) {
    int policy=notice_policy(0);
    if(policy==1||policy==2) {
        AstraFrame f;notice_sky(m->start,&f);
        bool visible=!m->skipped&&visible_star(&f,m->origin,m->kind==2);
        if(visible!=(policy==2))return;
    }
    (void)context;char when[240],text[512];event_time(m->start,when,sizeof(when));
    snprintf(text,sizeof(text),tr(m->kind==2?"astra.notice.fireball":"astra.notice.meteor"),when);app_push_chat(text);
}
static void event_notice(const AstraStart *e,void *context) {
    int policy=notice_policy(e->kind+1);
    if(policy==0)return;
    if(policy==1||policy==2) {
        AstraFrame f;notice_sky(e->time,&f);AstraEventState *s=g_app.world->astra_events;
        bool visible;
        if(e->kind==ASTRA_START_SHOWER) {
            visible=visible_star(&f,s->showers[e->index].radiant,false);
        } else if(e->kind==ASTRA_START_COMET) {
            const AstraComet *c=&s->comets[e->index];float p[3];
            for(int k=0;k<3;k++)p[k]=(float)(c->a[k]*cos(-.45)+c->b[k]*sin(-.45));
            visible=visible_star(&f,p,false);
        } else if(e->eclipse_kind==1) {
            visible=f.sun[1]>0&&f.solar_type;
        } else visible=f.moon[1]>0&&f.lunar_type;
        if(visible!=(policy==2))return;
    }
    (void)context;char when[240],text[512];event_time(e->time,when,sizeof(when));
    if(e->kind==ASTRA_START_SHOWER)snprintf(text,sizeof(text),tr("astra.notice.shower"),when,e->index+1);
    else if(e->kind==ASTRA_START_COMET)snprintf(text,sizeof(text),tr("astra.notice.comet"),when,e->index+1);
    else {
        const char *solar[]={"","astra.solar.partial","astra.solar.total","astra.solar.annular"};
        const char *lunar[]={"","astra.lunar.penumbra","astra.lunar.partial","astra.lunar.total"};
        snprintf(text,sizeof(text),tr("astra.notice.eclipse"),when,tr(e->eclipse_kind==1?solar[e->eclipse_type]:lunar[e->eclipse_type]));
    }
    app_push_chat(text);
}
void app_astra_advance(double before) {
    MacroWorld *w=g_app.world;if(!w||!w->opt.astronomy_enabled)return;
    if(w->astronomy_broadcast&0xfcu)qlcg_astra_starts_notify(w->astra_events,&w->opt,&g_app.clock,before,event_notice,NULL);
    else if(w->astra_events)w->astra_events->broadcast_ready=false;
    int selected=notice_policy(0);
    int policy=selected?ASTRA_NOTICE_ALL:0;
    qlcg_astra_events_advance(w->astra_events,before,g_app.clock.unix_ms,g_app.clock.time_speed,
                            policy,meteor_notice,NULL);
}
void app_astra_report(void) {
    MacroWorld *w=g_app.world;
    if(!w||g_app.screen!=SCREEN_IN_GAME){app_push_chat(tr("chat.world.noWorld"));return;}
    if(!w->opt.astronomy_enabled){app_push_chat(tr("chat.astra.disabled"));return;}
    AstraEventState *s=w->astra_events;char text[512];
    if(qlcg_world_flat(w)) app_push_chat(tr("astra.infinite.catalog"));
    else if(s){snprintf(text,sizeof(text),tr("astra.catalog"),s->shower_count,s->comet_count);app_push_chat(text);}
    AstraEclipse e;if(!qlcg_astra_eclipse_find(&w->opt,&g_app.clock,&e)){app_push_chat(tr("astra.noEclipse"));return;}
    char when[240];event_time(e.peak,when,sizeof(when));
    const char *solar[]={"","astra.solar.partial","astra.solar.total","astra.solar.annular"};
    const char *lunar[]={"","astra.lunar.penumbra","astra.lunar.partial","astra.lunar.total"};
    snprintf(text,sizeof(text),tr("astra.forecast"),tr(e.kind==1?solar[e.type]:lunar[e.type]),when);app_push_chat(text);
    app_push_chat(tr(qlcg_world_flat(w)?"astra.infinite.map":e.kind==1?"astra.map.solar":"astra.map.lunar"));
}
