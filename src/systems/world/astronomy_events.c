#include "astronomy_events.h"
#include "../../core/math_util.h"
#include "../calendar/jd_astronomy.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define TAU 6.283185307179586
static float random01(uint32_t *s) { *s=qlcg_hash_u32(*s+0x9e3779b9u); return (*s>>8)/16777216.f; }
static float dot3(const float *a,const float *b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static void normalize(float *p) { float d=sqrtf(dot3(p,p)); if(d>0) for(int k=0;k<3;k++) p[k]/=d; }
static void direction(uint32_t *s,float *p) {
    float z=2*random01(s)-1,a=(float)TAU*random01(s),r=sqrtf(1-z*z);
    p[0]=r*cosf(a);p[1]=r*sinf(a);p[2]=z;
}
static void tangent(uint32_t *s,const float *p,float *t) {
    direction(s,t);float d=dot3(p,t);for(int k=0;k<3;k++)t[k]-=d*p[k];normalize(t);
}
AstraEventState *qlcg_astra_events_create(uint32_t seed) {
    AstraEventState *s=calloc(1,sizeof(*s)); if(!s)return NULL;
    s->seed=seed;s->version=ASTRA_EVENT_VERSION;s->hours[0].hour=s->hours[1].hour=INT64_MIN;
    s->high_water=-1e100;s->rendered_to=-1e100;
    uint32_t r=qlcg_hash_u32(seed^0xea57a11u);
    s->shower_count=3+(int)(random01(&r)*4);s->comet_count=2+(int)(random01(&r)*3);
    for(int i=0;i<s->shower_count;i++) {
        AstraShower *v=&s->showers[i];v->peak=random01(&r)*TAU;
        v->width=(2+3*random01(&r))*TAU/365.2422;v->rate=24+36*random01(&r);direction(&r,v->radiant);
    }
    for(int i=0;i<s->comet_count;i++) {
        AstraComet *v=&s->comets[i];v->period=120+600*random01(&r);v->epoch=random01(&r)*v->period;
        v->window=8+16*random01(&r);direction(&r,v->a);tangent(&r,v->a,v->b);
    }
    return s;
}
double qlcg_astra_flat_shower(const AstraEventState *s,int index,int64_t slot,double *half) {
    uint32_t h=qlcg_hash_u32(s->seed^(uint32_t)slot^((uint32_t)index*0x9e3779b9u));
    *half=6+(h%19);return slot*192.0+24+(h>>8)/16777216.0*144;
}
double qlcg_astra_flat_comet(const AstraEventState *s,int index,int64_t slot) {
    uint32_t h=qlcg_hash_u32(s->seed^(uint32_t)slot^((uint32_t)index*0x1f123bb5u)^0xc04e7u);
    return slot*128.0+16+(h>>8)/16777216.0*96;
}
static AstraMeteorHour *hour_at(AstraEventState *s,int64_t hour) {
    AstraMeteorHour *h=&s->hours[(unsigned long long)hour&1];if(h->hour==hour)return h;
    memset(h,0,sizeof(*h));h->hour=hour;
    uint32_t r=qlcg_hash_u32(s->seed^(uint32_t)hour^qlcg_hash_u32((uint32_t)(hour>>32))^0x4e37ab1u);
    double lon=qlcg_sun_apparent_longitude_deg(hour/24.0+2440587.5)*TAU/360,weights[6],rate=6;
    for(int i=0;i<s->shower_count;i++) {
        double d=fabs(remainder(lon-s->showers[i].peak,TAU)),t=fmax(0,1-d/(s->showers[i].width*.5));
        if(s->flat) {
            int64_t slot=(int64_t)floor(hour/(24.0*8));
            double half,peak=qlcg_astra_flat_shower(s,i,slot,&half);
            t=fmax(0,1-fabs(hour-peak)/half);
        }
        weights[i]=s->showers[i].rate*t*t;rate+=weights[i];
    }
    h->count=(int)fmin(ASTRA_METEORS,floor(rate+random01(&r)));
    for(int i=0;i<h->count;i++) {
        AstraMeteor *m=&h->meteors[i];m->id=((uint64_t)hour<<10)|(unsigned)i;
        m->start=hour*3600000.0+(i+random01(&r))*3600000.0/h->count;
        float kind=random01(&r);m->kind=kind<.94f?0:kind<.99f?1:2;
        m->speed=11+61*random01(&r);
        m->flight=m->kind==0?.1f+1.9f*random01(&r):m->kind==1?2+3*random01(&r):1+9*random01(&r);
        m->train=0;if(m->kind==2||random01(&r)<.12f) m->train=1+7*random01(&r);
        if(m->kind==2&&random01(&r)<.2f)m->train=30+150*random01(&r);
        m->path=fminf(.9f,m->speed*m->flight/250.f);
        direction(&r,m->origin);tangent(&r,m->origin,m->tangent);
        double pick=random01(&r)*rate-6;
        for(int j=0;j<s->shower_count&&pick>=0;j++) {
            pick-=weights[j];if(pick<0) {
                m->shower=(unsigned char)(j+1);float d=dot3(m->origin,s->showers[j].radiant);
                for(int k=0;k<3;k++)m->tangent[k]=d*m->origin[k]-s->showers[j].radiant[k];normalize(m->tangent);
            }
        }
        m->color[0]=.7f+.3f*random01(&r);m->color[1]=.8f+.2f*random01(&r);m->color[2]=.65f+.35f*random01(&r);
    }
    return h;
}
bool qlcg_astra_meteor_skip(double life,double speed) { return speed>0&&life<speed*.01; }
void qlcg_astra_events_advance(AstraEventState *s,double from,double to,double speed,
                              int policy,AstraNotice notice,void *context) {
    if(!s||!isfinite(from)||!isfinite(to)||!isfinite(speed)||speed<=0||to<=from)return;
    bool contiguous=!s->initialized||fabs(s->cursor-from)<.5;
    s->initialized=true;s->cursor=to;s->frame_from=from;s->frame_to=to;
    if(!contiguous)s->high_water=fmax(s->high_water,from);
    if(to-from>ASTRA_MAX_STEP_MS) {s->frame_from=to;s->high_water=fmax(s->high_water,to);return;}
    int64_t first=(int64_t)floor(from/3600000),last=(int64_t)floor(to/3600000);
    for(int64_t hour=first;hour<=last;hour++) {
        AstraMeteorHour *h=hour_at(s,hour);
        for(int i=0;i<h->count;i++) {
            AstraMeteor *m=&h->meteors[i];
            if(m->start<=from||m->start>to||m->decided)continue;
            m->decided=1;m->skipped=(unsigned char)qlcg_astra_meteor_skip((double)m->flight+m->train,speed);
            bool announce=(policy&ASTRA_NOTICE_ALL)||(m->skipped&&(policy&ASTRA_NOTICE_SKIPPED));
            if(announce&&notice&&m->start>s->high_water)notice(m,context);
        }
    }
    s->high_water=fmax(s->high_water,to);
}
static void vertex(AstraVertex *v,const float *p,const float *c,float scale) {
    memcpy(v->p,p,12);for(int k=0;k<3;k++)v->color[k]=c[k]*scale;v->size=1;
}
static void ribbon(AstraVertex *out,int *n,int cap,const float *a,const float *b,
                   float wa,float wb,const float *color,float sa,float sb) {
    if(*n+6>cap)return;
    float side[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};normalize(side);
    float p[4][3];for(int k=0;k<3;k++){p[0][k]=a[k]-side[k]*wa;p[1][k]=a[k]+side[k]*wa;p[2][k]=b[k]-side[k]*wb;p[3][k]=b[k]+side[k]*wb;}
    const int idx[6]={0,1,2,2,1,3};for(int j=0;j<6;j++){AstraVertex *v=&out[(*n)++];vertex(v,p[idx[j]],color,idx[j]<2?sa:sb);v->size=(idx[j]&1)?1:-1;}
}
int qlcg_astra_event_vertices(AstraEventState *s,const CalendarClock *clk,const AstraFrame *f,
                              AstraVertex *out,int cap,int *comet_end) {
    *comet_end=0;if(!s||!clk||!out)return 0;int n=0;
    double days=clk->unix_ms/86400000.0,now=clk->unix_ms;
    float sun[3];for(int k=0;k<3;k++)sun[k]=f->sun[0]*f->east[k]+f->sun[1]*f->up[k]+f->sun[2]*f->south[k];
    for(int i=0;i<s->comet_count;i++) {
        const AstraComet *c=&s->comets[i];double dt=remainder(days-c->epoch,c->period);
        if(s->flat) {
            int64_t slot=(int64_t)floor(days/128);
            dt=days-qlcg_astra_flat_comet(s,i,slot);
        }
        if(fabs(dt)>c->window*.5)continue;
        float a[3],tail[3],prev[3];double angle=dt/c->window*.9;
        for(int k=0;k<3;k++)a[k]=(float)(c->a[k]*cos(angle)+c->b[k]*sin(angle));
        float d=dot3(sun,a);for(int k=0;k<3;k++)tail[k]=a[k]*d-sun[k];normalize(tail);memcpy(prev,a,12);
        float bright=(float)pow(cos(dt/c->window*3.14159265359),2),color[3]={.32f,.55f,.7f};
        for(int j=1;j<=24;j++) {
            float t=j/24.f,p[3];for(int k=0;k<3;k++)p[k]=a[k]+tail[k]*t*.22f;normalize(p);
            float last=(j-1)/24.f;
            ribbon(out,&n,cap,prev,p,.0015f+last*.012f,.0015f+t*.012f,color,bright*(1-last)*(1-last),bright*(1-t)*(1-t));memcpy(prev,p,12);
        }
        float p[3];for(int k=0;k<3;k++)p[k]=a[k]+tail[k]*.002f;
        ribbon(out,&n,cap,a,p,.002f,.0015f,color,bright*2,bright);
    }
    *comet_end=n;
    double from=now;
    if(s->initialized&&fabs(s->frame_to-now)<.5&&s->rendered_to!=now)from=s->frame_from;
    s->rendered_to=now;
    int64_t hour=(int64_t)floor(now/3600000);
    for(int j=0;j<2;j++) {
        AstraMeteorHour *h=hour_at(s,hour-1+j);
        for(int i=0;i<h->count;i++) {
            AstraMeteor *m=&h->meteors[i];double age=(now-m->start)*.001,life=(double)m->flight+m->train;
            if(age<0||m->start+life*1000<from)continue;
            if(!m->decided) {m->decided=1;m->skipped=(unsigned char)qlcg_astra_meteor_skip(life,clk->time_speed);}
            if(m->skipped)continue;
            bool swept=age>life;if(swept&&from==now)continue;
            if(swept)age=m->flight*.65;
            float head=(float)fmin(1,age/m->flight),tail=fmaxf(0,head-.25f),bright=1;
            if(age>m->flight){tail=0;bright=(float)pow(fmax(0,1-(age-m->flight)/fmax(.001,m->train)),2)*.25f;}
            else bright=(float)sin(fmin(1,age/m->flight)*3.14159265359);
            if(m->kind==2)bright*=4;
            float side[3]={m->origin[1]*m->tangent[2]-m->origin[2]*m->tangent[1],m->origin[2]*m->tangent[0]-m->origin[0]*m->tangent[2],m->origin[0]*m->tangent[1]-m->origin[1]*m->tangent[0]};
            float drift=age>m->flight?(float)((age-m->flight)/fmax(1,m->train))*.008f:0;
            float prev[3];for(int k=0;k<3;k++)prev[k]=m->origin[k]*cosf(tail*m->path)+m->tangent[k]*sinf(tail*m->path)+side[k]*drift*sinf((float)age*.03f);
            for(int q=1;q<=12;q++) {
                float t=q/12.f,p[3],a=(tail+(head-tail)*t)*m->path;
                for(int k=0;k<3;k++)p[k]=m->origin[k]*cosf(a)+m->tangent[k]*sinf(a)+side[k]*drift*sinf(q*.7f+(float)age*.03f);
                float flicker=m->kind==2?1+.3f*sinf(t*37+(float)age*20):1;
                float width=(m->kind==2?.002f:.0014f)+drift*.25f;
                ribbon(out,&n,cap,prev,p,width,width,m->color,bright*(t-1/12.f)*flicker,bright*t*flicker);memcpy(prev,p,12);
            }
        }
    }
    return n;
}
typedef struct { uint32_t version,seed;double high_water;int64_t hour[2];unsigned char flags[2][ASTRA_METEORS]; } EventDisk;
bool qlcg_astra_events_write(FILE *fp,const MacroWorld *w) {
    EventDisk d={0};d.version=ASTRA_EVENT_VERSION;d.seed=w->opt.seed;d.high_water=-1e100;
    const AstraEventState *s=w->astra_events;
    for(int j=0;j<2;j++){d.hour[j]=s?s->hours[j].hour:INT64_MIN;if(s)for(int i=0;i<s->hours[j].count;i++)d.flags[j][i]=s->hours[j].meteors[i].decided|(s->hours[j].meteors[i].skipped<<1);}
    if(s)d.high_water=s->high_water;uint32_t size=sizeof(d)+8,enabled=0,policy=w->astronomy_broadcast;
    return policy<=255&&fwrite("ASTE1",1,5,fp)==5&&fwrite(&size,4,1,fp)==1&&fwrite(&d,sizeof(d),1,fp)==1&&fwrite(&enabled,4,1,fp)==1&&fwrite(&policy,4,1,fp)==1;
}
bool qlcg_astra_events_read(FILE *fp,MacroWorld *w,long limit) {
    long at=ftell(fp);if(at<0)return false;char magic[5];
    if(limit>0&&limit-at<5)return fseek(fp,at,SEEK_SET)==0;
    if(fread(magic,1,5,fp)!=5||memcmp(magic,"ASTE1",5))return fseek(fp,at,SEEK_SET)==0;
    uint32_t size,enabled=0,policy=0;EventDisk d;
    if(fread(&size,4,1,fp)!=1||(size!=sizeof(d)&&size!=sizeof(d)+4&&size!=sizeof(d)+8))return false;
    if((limit>0&&limit-at<9+(long)size)||fread(&d,sizeof(d),1,fp)!=1)return false;
    if(size>sizeof(d)&&(fread(&enabled,4,1,fp)!=1||enabled>1))return false;
    if(size==sizeof(d)+8&&(fread(&policy,4,1,fp)!=1||policy>255))return false;
    if(d.version!=ASTRA_EVENT_VERSION||!isfinite(d.high_water))return false;
    for(int j=0;j<2;j++) {
        if(d.hour[j]!=INT64_MIN&&(d.hour[j]<-1000000000LL||d.hour[j]>1000000000LL))return false;
        for(int i=0;i<ASTRA_METEORS;i++)if(d.flags[j][i]>3||d.flags[j][i]==2)return false;
    }
    if(!w)return true;if(d.seed!=w->opt.seed)return false;
    AstraEventState *s=qlcg_astra_events_create(w->opt.seed);if(!s)return false;s->high_water=d.high_water;s->flat=qlcg_world_flat(w);
    for(int j=0;j<2;j++)if(d.hour[j]!=INT64_MIN) {
        AstraMeteorHour *h=hour_at(s,d.hour[j]);for(int i=0;i<h->count;i++){h->meteors[i].decided=d.flags[j][i]&1;h->meteors[i].skipped=(d.flags[j][i]>>1)&1;}
    }
    free(w->astra_events);w->astra_events=s;w->astronomy_broadcast=policy;return true;
}
