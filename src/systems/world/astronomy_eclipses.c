#include "astronomy_eclipses.h"
#include "astronomy_infinite.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846
static float global_solar(const AstraFrame *f,int *type) {
    double d[3],p[3],len=0,along=0,dist=0;float o[3],cover;
    for(int k=0;k<3;k++){d[k]=f->moon_world[k]-f->sun_world[k];len+=d[k]*d[k];}len=sqrt(len);
    for(int k=0;k<3;k++){d[k]/=len;along-=f->moon_world[k]*d[k];}
    if(along<0){*type=0;return 0;}
    for(int k=0;k<3;k++){p[k]=f->moon_world[k]+d[k]*along;dist+=p[k]*p[k];}
    if(dist<1)for(int k=0;k<3;k++)o[k]=(float)(p[k]-d[k]*sqrt(1-dist));
    else for(int k=0;k<3;k++)o[k]=(float)(p[k]/sqrt(dist));
    *type=qlcg_astra_solar_at(f->sun_world,f->moon_world,o,&cover);return *type?(float)(4-sqrt(dist)):0;
}
bool qlcg_astra_eclipse_find(const WorldCreatorOptions *opt,const CalendarClock *clock,AstraEclipse *out) {
    if(opt->map_mode==WORLD_MAP_INFINITE) { *out=qlcg_infinite_eclipse(opt->seed,clock->unix_ms); return true; }
    memset(out,0,sizeof(*out));CalendarClock c=*clock;AstraFrame f;
    qlcg_astra_sample(opt,&c,0,0,NULL,&f);double period=clock->mode==CALENDAR_YUAN?30:29.530588;
    double phase=f.phase,base=clock->unix_ms-phase*period*86400000;
    for(int index=-1;index<27;index++) {
        double target=(index&1)?.5:0,center=base+index*.5*period*86400000;
        if(center<clock->unix_ms-86400000)continue;
        for(int j=0;j<6;j++){c.unix_ms=center;qlcg_astra_sample(opt,&c,0,0,NULL,&f);center-=remainder(f.phase-target,1)*period*86400000;}
        if(center<clock->unix_ms-21600000)continue;
        double best=0;AstraEclipse e={0};e.kind=(index&1)?2:1;
        for(int j=-120;j<=120;j++) {
            c.unix_ms=center+j*120000.0;qlcg_astra_sample(opt,&c,0,0,NULL,&f);int type;
            double strength=e.kind==1?global_solar(&f,&type):0;
            if(e.kind==2) {
                type=f.lunar_type;double ds=0,dm=0,dp=0;
                for(int k=0;k<3;k++){ds+=f.sun_world[k]*f.sun_world[k];dm+=f.moon_world[k]*f.moon_world[k];dp-=f.sun_world[k]*f.moon_world[k];}
                strength=type?1+dp/sqrt(ds*dm):0;
            }
            if(type){if(!e.type)e.start=c.unix_ms;e.end=c.unix_ms;if(strength>best){best=strength;e.peak=c.unix_ms;e.type=type;}}
        }
        if(e.type&&e.end>=clock->unix_ms){*out=e;return true;}
    }
    return false;
}
void qlcg_astra_eclipse_chart(const WorldCreatorOptions *opt,const CalendarClock *clock,
                             const AstraEclipse *e,unsigned char *rgba) {
    memset(rgba,0,ASTRA_CHART_W*ASTRA_CHART_H*4);if(!e->type)return;
    if(opt->map_mode==WORLD_MAP_INFINITE) {
        for(int i=0;i<ASTRA_CHART_W*ASTRA_CHART_H;i++) {
            rgba[i*4]=255; rgba[i*4+1]=e->type>=2?255:0;
            rgba[i*4+2]=e->type==3?255:0; rgba[i*4+3]=255;
        }
        return;
    }
    CalendarClock c=*clock;AstraFrame frames[241];int count=0;
    for(double ms=e->start;ms<=e->end+1&&count<241;ms+=120000){c.unix_ms=ms;qlcg_astra_sample(opt,&c,0,0,NULL,&frames[count++]);}
    for(int y=0;y<ASTRA_CHART_H;y++)for(int x=0;x<ASTRA_CHART_W;x++) {
        double lon=((x+.5)/ASTRA_CHART_W-.5)*2*PI,lat=(.5-(y+.5)/ASTRA_CHART_H)*PI;
        float o[3]={(float)(cos(lat)*cos(lon)),(float)sin(lat),(float)(cos(lat)*sin(lon))};
        unsigned char *p=&rgba[(y*ASTRA_CHART_W+x)*4];
        for(int j=0;j<count;j++) {
            const AstraFrame *f=&frames[j];
            if(e->kind==1) {
                float coverage;int type=qlcg_astra_solar_at(f->sun_world,f->moon_world,o,&coverage);
                if(type){if(p[0]<coverage*255)p[0]=(unsigned char)(coverage*255);if(type==2)p[1]=255;if(type==3)p[2]=255;p[3]=255;}
            }else {
                double alt=0;for(int k=0;k<3;k++)alt+=(f->moon_world[k]-o[k])*o[k];
                if(alt>0&&f->lunar_type){p[0]=255;if(f->lunar_type>=2)p[1]=255;if(f->lunar_type==3)p[2]=255;p[3]=255;}
            }
        }
    }
}
