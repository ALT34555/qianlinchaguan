#include "astronomy.h"
#include "astronomy_infinite.h"
#include "../../core/math_util.h"
#include "../calendar/jd_astronomy.h"
#include <math.h>
#include <string.h>
#define AP 3.14159265358979323846
#define AT (2.0 * AP)
static float rnd(uint32_t *s) { *s = qlcg_hash_u32(*s + 0x9e3779b9u); return (*s >> 8) / 16777216.0f; }
static double cycle(double t) { return t - floor(t); }
static float dot(const float *a,const float *b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static void norm(float *p) { float d=sqrtf(dot(p,p)); if(d>0) for(int i=0;i<3;i++) p[i]/=d; }
static void direction(uint32_t *s,float *p) {
    float z=2*rnd(s)-1, a=(float)AT*rnd(s), r=sqrtf(1-z*z);
    p[0]=r*cosf(a); p[1]=r*sinf(a); p[2]=z;
}
static void basis(const float *n,float *a,float *b) {
    if(fabsf(n[2])<.9f) { a[0]=-n[1]; a[1]=n[0]; a[2]=0; }
    else { a[0]=0; a[1]=-n[2]; a[2]=n[1]; }
    norm(a);
    b[0]=n[1]*a[2]-n[2]*a[1]; b[1]=n[2]*a[0]-n[0]*a[2]; b[2]=n[0]*a[1]-n[1]*a[0];
}
static void offset(const float *n,const float *a,const float *b,float x,float y,float *p) {
    for(int k=0;k<3;k++) p[k]=n[k]+a[k]*x+b[k]*y;
    norm(p);
}
static void line(AstraCatalog *c,const float *a,const float *b,float r,float g,float blue) {
    if(c->line_count+2>ASTRA_LINES) return;
    for(int i=0;i<2;i++) {
        AstraVertex *v=&c->lines[c->line_count++]; memcpy(v->p,i?b:a,12);
        v->color[0]=r; v->color[1]=g; v->color[2]=blue; v->size=1;
    }
}
static void orbit_point(const AstraOrbit *o,double angle,float *p) {
    for(int k=0;k<3;k++) p[k]=(float)(o->a[k]*cos(angle)+o->b[k]*sin(angle));
}
void qlcg_astra_generate(AstraCatalog *c,uint32_t seed,uint32_t version) {
    memset(c,0,sizeof(*c)); c->seed=seed; c->version=version;
    uint32_t s=qlcg_hash_u32(seed ^ version ^ 0x3a91f73u);
    float hubs[3][3]; for(int i=0;i<3;i++) direction(&s,hubs[i]);
    c->nebula_count=24+(int)(rnd(&s)*8);
    for(int j=0;j<c->nebula_count;j++) {
        float *n=c->nebula[j],a[3],b[3];
        if(rnd(&s)<.72f) {
            const float *h=hubs[(int)(rnd(&s)*3)]; basis(h,a,b);
            offset(h,a,b,(rnd(&s)-.5f)*.9f,(rnd(&s)-.5f)*.9f,n);
        } else direction(&s,n);
        n[3]=.025f+rnd(&s)*.05f;
        n[4]=.3f+rnd(&s)*.5f; n[5]=.35f+rnd(&s)*.4f; n[6]=.55f+rnd(&s)*.45f;
        n[7]=rnd(&s)*(float)AT; basis(n,a,b);
        int count=150+(int)(rnd(&s)*200);
        for(int i=0;i<count;i++) {
            AstraVertex *v=&c->stars[c->star_count++];
            float r=sqrtf(-2*logf(fmaxf(rnd(&s),.00001f)))*n[3],t=rnd(&s)*(float)AT;
            float x=r*cosf(t),y=r*sinf(t)*(.35f+.15f*(j%4));
            offset(n,a,b,x*cosf(n[7])-y*sinf(n[7]),x*sinf(n[7])+y*cosf(n[7]),v->p);
            float bright=i<4?.9f:.12f+rnd(&s)*.3f;
            for(int k=0;k<3;k++) v->color[k]=n[4+k]*bright;
            v->size=i<4?3.0f:1.1f+rnd(&s)*1.1f;
        }
    }
    s=qlcg_hash_u32(seed ^ version ^ 0xa83472u);
    c->constellation_count=16+(int)(rnd(&s)*7);
    for(int j=0;j<c->constellation_count;j++) {
        float n[3],a[3],b[3],first[3],prev[3],x=0,y=0;
        direction(&s,n); basis(n,a,b); int count=4+(int)(rnd(&s)*5);
        for(int i=0;i<count;i++) {
            AstraVertex *v=&c->stars[c->star_count++];
            x=qlcg_clampf(x+(rnd(&s)-.5f)*.08f,-.13f,.13f);
            y=qlcg_clampf(y+(rnd(&s)-.5f)*.08f,-.13f,.13f);
            offset(n,a,b,x,y,v->p); v->size=2.5f+rnd(&s);
            v->color[0]=.65f; v->color[1]=.8f; v->color[2]=1;
            if(i) line(c,prev,v->p,.15f,.28f,.4f); else memcpy(first,v->p,12);
            memcpy(prev,v->p,12);
        }
        if(count>=5 && rnd(&s)<.45f) line(c,prev,first,.15f,.28f,.4f);
    }
    s=qlcg_hash_u32(seed ^ version ^ 0x817ab2u);
    for(int i=0;i<ASTRA_BODIES;i++) {
        AstraOrbit *o=&c->bodies[i]; float n[3]; direction(&s,n); basis(n,o->a,o->b);
        o->phase=rnd(&s)*AT; o->period=(20+380*rnd(&s))*(i<9?1:-1); o->size=3.5f+rnd(&s);
        o->color[0]=.65f+.35f*rnd(&s); o->color[1]=.65f+.35f*rnd(&s); o->color[2]=.65f+.35f*rnd(&s);
        for(int j=0;j<128;j++) { float a[3],b[3]; orbit_point(o,j*AT/128,a); orbit_point(o,(j+1)*AT/128,b); line(c,a,b,.09f,.16f,.22f); }
    }
    for(int j=0;j<17;j++) for(int i=0;i<128;i++) {
        float a[3],b[3];
        for(int k=0;k<2;k++) {
            double t=(i+k)*AT/128, lon=j<12?j*AT/12:t,lat=j<12?t:(j-14)*AP/6;
            float *p=k?b:a; p[0]=(float)(cos(lat)*cos(lon)); p[1]=(float)(cos(lat)*sin(lon)); p[2]=(float)sin(lat);
        }
        line(c,a,b,.045f,.08f,.12f);
    }
}
static void local(const AstraFrame *f,const float *p,float *v) { v[0]=dot(f->east,p); v[1]=dot(f->up,p); v[2]=dot(f->south,p); }
static void ecliptic(double lon,double tilt,float *p) { p[0]=(float)cos(lon); p[1]=(float)(sin(lon)*cos(tilt)); p[2]=(float)(sin(lon)*sin(tilt)); }
double qlcg_astra_overlap(double r,double m,double d) {
    if(r<=0||m<=0||d>=r+m) return 0;
    if(d<=fabs(r-m)) return m>=r?1:m*m/(r*r);
    double a=(d*d+r*r-m*m)/(2*d*r), b=(d*d+m*m-r*r)/(2*d*m);
    a=fmax(-1,fmin(1,a)); b=fmax(-1,fmin(1,b));
    double h=fmax(0,(-d+r+m)*(d+r-m)*(d-r+m)*(d+r+m));
    return (r*r*acos(a)+m*m*acos(b)-.5*sqrt(h))/(AP*r*r);
}
int qlcg_astra_solar_at(const float *s,const float *m,const float *o,float *cover) {
    double ds=0,dm=0,dp=0,alt=0;
    for(int k=0;k<3;k++) { double a=s[k]-o[k],b=m[k]-o[k]; ds+=a*a;dm+=b*b;dp+=a*b;alt+=a*o[k]; }
    *cover=0; if(alt<=0) return 0;
    ds=sqrt(ds);dm=sqrt(dm);double rs=asin(46.5/ds),rm=asin(.2724/dm);
    double d=acos(fmax(-1,fmin(1,dp/(ds*dm))));
    *cover=(float)qlcg_astra_overlap(rs,rm,d);
    if(d>=rs+rm) return 0;
    if(d<=fabs(rs-rm)) return rm>=rs?2:3;
    return 1;
}
static void world_vector(const float *p,double theta,float *v) {
    v[0]=(float)(p[0]*cos(theta)+p[1]*sin(theta));v[1]=p[2];
    v[2]=(float)(-p[0]*sin(theta)+p[1]*cos(theta));
}
void qlcg_astra_sample(const WorldCreatorOptions *opt,const CalendarClock *clk,
                       double lon,double lat,const AstraCatalog *cat,AstraFrame *f) {
    memset(f,0,sizeof(*f));
    if(opt->map_mode==WORLD_MAP_INFINITE) { lon=0; lat=3.141592653589793/6; }
    double jd=(clk?clk->unix_ms:0)/86400000.0+2440587.5, jde=qlcg_jd_ut_to_jde(jd);
    double sl=qlcg_sun_apparent_longitude_deg(jd)*AP/180, tilt=opt->axial_tilt_deg*AP/180;
    float sun[3],moon[3]; ecliptic(sl,tilt,sun);
    double ra=atan2(sun[1],sun[0]), spin=(opt->prograde?1:-1)*(cycle(jd+.5)-.5)*AT;
    double theta=ra+spin+lon, cp=cos(lat),sp=sin(lat),ct=cos(theta),st=sin(theta);
    f->east[0]=(float)-st; f->east[1]=(float)ct;
    f->up[0]=(float)(cp*ct); f->up[1]=(float)(cp*st); f->up[2]=(float)sp;
    f->south[0]=(float)(sp*ct); f->south[1]=(float)(sp*st); f->south[2]=(float)-cp;
    f->sun_lon=(float)-spin; f->sun_lat=asinf(sun[2]);
    CalendarMode mode=clk?clk->mode:opt->calendar_mode;
    double phase;
    if(mode==CALENDAR_YUAN) phase=cycle((jd-(qlcg_yuan_to_jdn(1,1,1)-.5))/30.0);
    else if(mode==CALENDAR_GREGORIAN) phase=cycle((jd-QLCG_J2000)/29.530588+(qlcg_hash_u32(opt->seed^0xabe31u)>>8)/16777216.0);
    else phase=cycle(qlcg_lunar_elongation_deg(jde)/360.0);
    f->phase=(float)phase;
    double ml=sl+phase*AT, days=jd-QLCG_J2000;
    double node=(qlcg_hash_u32(opt->seed^0x51eec1u)>>8)/16777216.0*AT-days*AT/6798.0;
    double beta=.0898*sin(ml-node), md=60.3*(1-.075*cos(days*AT/27.55+node));
    if(opt->map_mode==WORLD_MAP_INFINITE) beta=.1;
    moon[0]=(float)(cos(beta)*cos(ml));
    moon[1]=(float)(cos(beta)*sin(ml)*cos(tilt)-sin(beta)*sin(tilt));
    moon[2]=(float)(cos(beta)*sin(ml)*sin(tilt)+sin(beta)*cos(tilt));
    float spv[3],mpv[3],sv[3],mv[3],shadow[3]; double along=0;
    for(int k=0;k<3;k++) {spv[k]=sun[k]*10000;mpv[k]=(float)(moon[k]*md);along-=mpv[k]*sun[k];}
    world_vector(spv,ra+spin,f->sun_world);world_vector(mpv,ra+spin,f->moon_world);
    float obs[3]={(float)(cp*cos(lon)),(float)sp,(float)(cp*sin(lon))};
    f->solar_type=qlcg_astra_solar_at(f->sun_world,f->moon_world,obs,&f->solar_cover);
    double ds=0,dm=0;
    for(int k=0;k<3;k++) {sv[k]=spv[k]-f->up[k];mv[k]=mpv[k]-f->up[k];ds+=sv[k]*sv[k];dm+=mv[k]*mv[k];}
    f->sun_radius=(float)asin(46.5/sqrt(ds));f->moon_radius=(float)asin(.2724/sqrt(dm));
    norm(sv);norm(mv);local(f,sv,f->sun);local(f,mv,f->moon);
    f->moon_light=(float)((1-dot(moon,sun))*.5);
    if(along>0) {
        double d2=0,ur=1-along*45.5/10000,pr=1+along*47.5/10000;
        for(int k=0;k<3;k++) {shadow[k]=(float)(-sun[k]*along-f->up[k]);double t=mpv[k]+sun[k]*along;d2+=t*t;}
        double d=sqrt(d2);f->lunar_cover=(float)qlcg_astra_overlap(.2724,ur,d);
        f->lunar_penumbra=(float)qlcg_astra_overlap(.2724,pr,d);
        f->lunar_type=d+.2724<=ur?3:d<ur+.2724?2:d<pr+.2724?1:0;
        f->shadow_radius=(float)atan2(fmax(0,ur),sqrt(dm));f->penumbra_radius=(float)atan2(pr,sqrt(dm));
        norm(shadow);local(f,shadow,f->shadow_dir);
    } else { f->shadow_dir[1]=-1; }

    if(opt->map_mode==WORLD_MAP_INFINITE) qlcg_infinite_astra(opt,clk,f);
    if(cat) for(int i=0;i<ASTRA_BODIES;i++) {
        const AstraOrbit *o=&cat->bodies[i]; AstraVertex *v=&f->bodies[i];
        orbit_point(o,o->phase+cycle((jd-QLCG_J2000)/o->period)*AT,v->p);
        memcpy(v->color,o->color,12); v->size=o->size;
    }
}
void qlcg_astra_background(const AstraCatalog *c,unsigned char *rgb,int width,int height) {
    uint32_t seed=qlcg_hash_u32(c->seed^c->version^0x19b8a7u);
    for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
        double lon=((x+.5)/width)*AT,lat=((y+.5)/height-.5)*AP;
        float p[3]={(float)(cos(lat)*cos(lon)),(float)(cos(lat)*sin(lon)),(float)sin(lat)};
        float noise=qlcg_noise3(p[0]*5,p[1]*5,p[2]*5,seed),col[3]={.003f,.005f,.012f};
        for(int k=0;k<3;k++) col[k]*=(.5f+.5f*noise);
        for(int j=0;j<c->nebula_count;j++) {
            const float *n=c->nebula[j]; float d=1-dot(p,n);
            if(d>n[3]*n[3]*18) continue;
            float glow=expf(-d/(n[3]*n[3]*2))*(.012f+.025f*noise);
            for(int k=0;k<3;k++) col[k]+=n[k+4]*glow;
        }
        if(qlcg_hash2(x,y,seed)>.998f) for(int k=0;k<3;k++) col[k]+=.035f;
        for(int k=0;k<3;k++) rgb[(y*width+x)*3+k]=(unsigned char)(qlcg_clampf(col[k],0,1)*255);
    }
}
