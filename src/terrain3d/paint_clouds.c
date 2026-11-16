#include "paint_clouds.h"
#include "core/math_util.h"
#include "systems/world/precip.h"
#include "systems/world/gen/gen_climate.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CLOUD_SIDE 33
#define CLOUD_WIND 129
#define CLOUD_NOISE 256
typedef struct { int x,z;bool ready;float cover,thickness; } CloudCell;
typedef struct {
    CloudCell cells[CLOUD_SIDE*CLOUD_SIDE];
    GLuint texture;
    int x,z;
} CloudGrid;
struct PaintClouds {
    CloudGrid grid[2];
    GLuint noise;
    const MacroWorld *world;
    const PrecipField *precip;
    uint32_t seed;
    int monsoon;
    bool wind_prograde,wind_ready;
    double time,tick,wall_tick,wind[CLOUD_WIND][2],velocity[CLOUD_WIND][2],target_vel[CLOUD_WIND][2];
    bool ready;
};
static int wrap(int x,int n) { int a=x%n;return a<0?a+n:a; }
static double wrapped_x(const MacroWorld *w,double x) {
    if(qlcg_world_flat(w))return fmax(-w->circumference*.5,fmin(w->circumference*.5,x));
    double c=w->circumference;return x-floor((x+c*.5)/c)*c;
}
void qlcg_clouds_sample(const MacroWorld *w,const WeatherSystem *ws,double ms,
    double x,double z,float *cover,float *thickness) {
    double circ=fmax(w->circumference,1),depth=qlcg_world_depth(w);
    x=wrapped_x(w,x);z=fmax(-depth*.5,fmin(depth*.5,z));
    float c=0,p=0,water=0;
    if(ws->forced_id>=0) {
        const WeatherProfile *wp=qlcg_get_weather_profile(ws->forced_id);
        c=qlcg_clampf(wp->clouds/2.5f,0,1);
        p=qlcg_clampf(wp->rain+wp->snow+wp->hail,0,1);water=c*PRECIP_THRESHOLD;
    } else {
        int cell=qlcg_world_cell_at_wxz(w,(float)x,(float)z);
        float temp=qlcg_precip_season_temp(w,cell,w->plant_calendar);
        PrecipSample sample;
        if(qlcg_precip_sample(w->precip,ms,(float)(x/circ*2*QLCG_PI),
            (float)(-z/depth*QLCG_PI),0,temp,&sample)) {
            c=sample.cloud_cover;p=sample.intensity;water=sample.cloud_mm;
        } else {
            int cx=(int)floor(x/CHUNK_SIZE),cz=(int)floor(z/CHUNK_SIZE);
            int eq=qlcg_world_flat(w)?0:(int)(circ/CHUNK_SIZE);
            ChunkWeather cw=qlcg_sample_chunk_weather(ws,ms,cx,cz,temp,eq);
            c=cw.cloud_cover;p=cw.intensity;water=c*PRECIP_THRESHOLD;
        }
    }
    float rain_cover=qlcg_clampf(p/.15f,0,1);rain_cover=rain_cover*rain_cover*(3-2*rain_cover);
    *cover=qlcg_clampf(fmaxf(c,(.78f+p*.20f)*rain_cover),0,1);
    *thickness=qlcg_clampf(.25f+c*1.35f+water/PRECIP_THRESHOLD*.45f+p*2.2f,.25f,4.5f);
}
static GLuint texture(int size,GLint format,GLenum type,const void *data,bool repeat) {
    GLuint id=0;glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);
    glTexImage2D(GL_TEXTURE_2D,0,format,size,size,0,GL_RGBA,type,data);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,(format==GL_RGBA32F&&!qlcg_gl_caps.float_linear)?GL_NEAREST:GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,(format==GL_RGBA32F&&!qlcg_gl_caps.float_linear)?GL_NEAREST:GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,repeat?GL_REPEAT:GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,repeat?GL_REPEAT:GL_CLAMP_TO_EDGE);
    return id;
}
static float lattice(int x,int z,int period,uint32_t seed) {
    return qlcg_hash2(wrap(x,period),wrap(z,period),seed);
}
static bool noise_init(PaintClouds *c) {
    unsigned char *pixels=malloc(CLOUD_NOISE*CLOUD_NOISE*4);
    if(!pixels)return false;
    for(int z=0;z<CLOUD_NOISE;z++)for(int x=0;x<CLOUD_NOISE;x++) {
        for(int channel=0;channel<2;channel++) {
            float value=0,amp=.5f,total=0;
            for(int octave=0,period=4;octave<5;octave++,period*=2,amp*=.5f) {
                float px=(float)x*period/CLOUD_NOISE,pz=(float)z*period/CLOUD_NOISE;
                int ix=(int)px,iz=(int)pz;float fx=px-ix,fz=pz-iz;
                fx=fx*fx*fx*(fx*(fx*6-15)+10);fz=fz*fz*fz*(fz*(fz*6-15)+10);
                uint32_t seed=c->seed+channel*619u+octave*1439u;
                value+=amp*qlcg_lerpf(qlcg_lerpf(lattice(ix,iz,period,seed),lattice(ix+1,iz,period,seed),fx),
                    qlcg_lerpf(lattice(ix,iz+1,period,seed),lattice(ix+1,iz+1,period,seed),fx),fz);total+=amp;
            }
            pixels[(z*CLOUD_NOISE+x)*4+channel]=(unsigned char)qlcg_clampi((int)(value/total*255),0,255);
        }
        pixels[(z*CLOUD_NOISE+x)*4+2]=0;pixels[(z*CLOUD_NOISE+x)*4+3]=255;
    }
    c->noise=texture(CLOUD_NOISE,GL_RGBA8,GL_UNSIGNED_BYTE,pixels,true);free(pixels);
    return c->noise!=0;
}
PaintClouds *qlcg_clouds_create(void) { return calloc(1,sizeof(PaintClouds)); }
void qlcg_clouds_free(PaintClouds *c) {
    if(!c)return;
    GLuint ids[]={c->noise,c->grid[0].texture,c->grid[1].texture};
    qlcg_gl_delete_textures(3,ids);free(c);
}
static void wind_at(const PaintClouds *c,double z,float *east,float *south) {
    double f=qlcg_clampd((z/qlcg_world_depth(c->world)+.5)*(CLOUD_WIND-1),0,CLOUD_WIND-1);
    int i=(int)f,j=i<CLOUD_WIND-1?i+1:i;float a=(float)(f-i);
    *east=(float)(c->wind[i][0]*(1-a)+c->wind[j][0]*a);
    *south=(float)(c->wind[i][1]*(1-a)+c->wind[j][1]*a);
}
void qlcg_clouds_update(PaintClouds *c,const MacroWorld *w,const WeatherSystem *ws,
    double ms,double time,double x,double z) {
    if(!c||!w||!ws)return;
    bool reset=c->world!=w||c->seed!=w->opt.seed||c->precip!=w->precip||time<c->time;
    if(reset) {
        for(int k=0;k<2;k++)memset(c->grid[k].cells,0,sizeof(c->grid[k].cells));
        if(c->noise)qlcg_gl_delete_textures(1,&c->noise);c->noise=0;
        memset(c->wind,0,sizeof(c->wind));memset(c->velocity,0,sizeof(c->velocity));
        c->ready=false;c->wind_ready=false;c->world=w;c->seed=w->opt.seed;c->precip=w->precip;c->time=time;
    }
    if(!c->noise&&!noise_init(c))return;
    double dt=fmax(0,fmin(.25,time-c->time));c->time=time;
    if(!c->wind_ready||c->monsoon!=(int)w->opt.monsoon_direction||c->wind_prograde!=w->opt.prograde) {
        for(int i=0;i<CLOUD_WIND;i++) {
            double east,south,lat=(.5-(double)i/(CLOUD_WIND-1))*QLCG_PI;
            qlcg_gen_climate_wind(&w->opt,lat,&east,&south);
            double speed=hypot(east,south);if(speed>1){east/=speed;south/=speed;}
            c->target_vel[i][0]=east*7;
            c->target_vel[i][1]=south*7;
        }
        c->monsoon=(int)w->opt.monsoon_direction;
        c->wind_prograde=w->opt.prograde;
        c->wind_ready=true;
    }
    double a=c->ready?1-exp(-dt/2):1;
    for(int i=0;i<CLOUD_WIND;i++) {
        c->velocity[i][0]+=(c->target_vel[i][0]-c->velocity[i][0])*a;
        c->velocity[i][1]+=(c->target_vel[i][1]-c->velocity[i][1])*a;
        c->wind[i][0]+=c->velocity[i][0]*dt;c->wind[i][1]+=c->velocity[i][1]*dt;
    }
    bool move=false;
    for(int k=0;k<2;k++) {
        double step=k?512:128;
        int gx=(int)floor(x/step)-CLOUD_SIDE/2,gz=(int)floor(z/step)-CLOUD_SIDE/2;
        move|=gx!=c->grid[k].x||gz!=c->grid[k].z;
    }
    double wall=qlcg_time_ms()*.001,elapsed=fmax(time-c->tick,wall-c->wall_tick);
    if(c->ready&&!move&&elapsed<.1)return;
    bool refresh=!c->ready||elapsed>=.1;
    float blend=c->ready?(float)(1-exp(-fmin(1,elapsed))):1;
    if(refresh){c->tick=time;c->wall_tick=wall;}
    float data[CLOUD_SIDE*CLOUD_SIDE*4];
    glActiveTexture(GL_TEXTURE0+3);
    for(int k=0;k<2;k++) {
        CloudGrid *g=c->grid+k;double step=k?512:128;
        g->x=(int)floor(x/step)-CLOUD_SIDE/2;g->z=(int)floor(z/step)-CLOUD_SIDE/2;
        for(int j=0;j<CLOUD_SIDE;j++) {
            int gz=g->z+j;
            float we,ws_wind;wind_at(c,gz*step,&we,&ws_wind);
            for(int i=0;i<CLOUD_SIDE;i++) {
                int gx=g->x+i,index=(j*CLOUD_SIDE+i)*4;
                CloudCell *cell=g->cells+wrap(gz,CLOUD_SIDE)*CLOUD_SIDE+wrap(gx,CLOUD_SIDE);
                bool fresh=!cell->ready||cell->x!=gx||cell->z!=gz;
                if(fresh||refresh) {
                    float cover,thickness;qlcg_clouds_sample(w,ws,ms,gx*step,gz*step,&cover,&thickness);
                    float blend_a=fresh?1:blend;
                    cell->cover+=(cover-cell->cover)*blend_a;cell->thickness+=(thickness-cell->thickness)*blend_a;
                }
                cell->x=gx;cell->z=gz;cell->ready=true;
                data[index]=cell->cover;data[index+1]=cell->thickness;
                data[index+2]=we;data[index+3]=ws_wind;
            }
        }
        if(!g->texture)g->texture=texture(CLOUD_SIDE,GL_RGBA32F,GL_FLOAT,data,false);
        else { glBindTexture(GL_TEXTURE_2D,g->texture);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,CLOUD_SIDE,CLOUD_SIDE,GL_RGBA,GL_FLOAT,data); }
    }
    c->ready=true;glActiveTexture(GL_TEXTURE0);
}
void qlcg_clouds_bind(const PaintClouds *c,GLuint p,double x,double z,float y) {
    if(!c||!c->ready){glUniform1f(qlcg_gl_uniform(p,"uCloudAmount"),0);return;}
    GLuint ids[]={c->noise,c->grid[0].texture,c->grid[1].texture};
    const char *names[]={"uCloudNoise","uCloudNear","uCloudFar"};
    for(int i=0;i<3;i++){glActiveTexture(GL_TEXTURE0+3+i);glBindTexture(GL_TEXTURE_2D,ids[i]);glUniform1i(qlcg_gl_uniform(p,names[i]),3+i);}
    for(int k=0;k<2;k++) {
        double step=k?512:128;
        glUniform4f(qlcg_gl_uniform(p,k?"uCloudFarMap":"uCloudNearMap"),
            (float)((x-c->grid[k].x*step)/step+.5),(float)((z-c->grid[k].z*step)/step+.5),
            (float)(1/step),1.0f/CLOUD_SIDE);
    }
    double circ=fmax(1,c->world->circumference),cycles=fmax(1,round(circ/4096));
    double scale=cycles/circ,sz=1.0/4096;
    glUniform4f(qlcg_gl_uniform(p,"uCloudNoiseMap"),(float)(x*scale-floor(x*scale)),
        (float)(z*sz-floor(z*sz)),(float)scale,(float)sz);
    glUniform1i(qlcg_gl_uniform(p,"uCloudLinear"),qlcg_gl_caps.float_linear?1:0);
    glUniform1f(qlcg_gl_uniform(p,"uCloudHeight"),2400-y);
    glUniform1f(qlcg_gl_uniform(p,"uCloudPulse"),(float)fmod(c->time*.045,2*QLCG_PI));
    glActiveTexture(GL_TEXTURE0);
}
