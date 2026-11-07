/* base 河岸模板的连续断面 */
#include "surface_rivers.h"
#include <math.h>

static const double PI=3.14159265358979323846;
static double clampd(double v,double a,double b) { return fmax(a,fmin(b,v)); }
static double smooth(double t) { t=clampd(t,0,1); return t*t*(3-2*t); }

double qlcg_river_bank_field(uint32_t seed,int cell,double along,int side,double x) {
    static const double curves[8][5]={
        {.22,.18,.4,0,0},{-.25,.7,.5,-.4,0},{.38,-.42,-1.6,.5,0},{-.12,-.15,.65,.2,0},
        {.02,.35,.1,-.6,.18},{-.38,-.6,1.2,.4,0},{.08,1.1,-.25,-1.1,0},{.16,-.35,-.5,.8,-.22}
    };
    static const double bends[8]={.18,-.45,.5,-.6,.05,-.12,.22,.08};
    static const int count[4]={2,4,3,3};
    static const double features[4][4][5]={
        {{.02,-.22,.27,.58,1.5},{.02,-.25,.15,.25,-1.8}},
        {{-.2,-.2,.16,.5,1.4},{-.2,-.22,.07,.2,-1.7},{.2,-.2,.16,.5,1.4},{.2,-.22,.07,.2,-1.7}},
        {{-.22,.35,.065,.16,-1.2},{.02,.46,.08,.2,-1.4},{.25,.35,.07,.18,-1.2}},
        {{0,-.18,.36,.46,1.4},{0,-.22,.26,.17,-1.8},{.28,.08,.055,.3,.8}}
    };
    int tile=(int)floor(along/32);
    double z=clampd((along-tile*32)/32,0,1);
    uint32_t key=seed^((uint32_t)(cell+1)*374761393u)^((uint32_t)(tile+1)*668265263u)^((uint32_t)side*1274126177u);
    int variant=key%16;
    double phase=(key%628)/100.0;
    x=clampd(x,0,1);
    double env=256*pow(x*(1-x)*z*(1-z),2),v=z-.5;
    double bend;
    if (variant<8) {
        const double *c=curves[variant];
        bend=(c[0]+c[1]*v+c[2]*v*v+c[3]*v*v*v+c[4]*sin(v*2*PI+phase*.2))*smooth(hypot(x-.5,z-.5)/.18);
    } else {
        double frequency=variant==10?4:2;
        bend=bends[variant-8]*sin(x*PI*frequency+phase)*sin(z*PI);
    }
    double field=2*smooth(x)-1+env*(-.28+bend);
    if (variant<12) return field;
    double band=1-smooth((fabs(field)-.45)/.65);
    double detail=.12*sin(v*47+phase)*sin(field*13-phase);
    int group=variant-12;
    for (int i=0;i<count[group];i++) {
        const double *f=features[group][i];
        double a=(v-f[0])/f[2],b=(field-f[1])/f[3];
        double radius=.7*(fabs(a+.2*b)+fabs(b))+.3*hypot(a,b);
        if (f[4]<0 && f[1]<0)
            radius=fabs((v-f[0])/(f[2]*(.6+.4*clampd((f[1]-field)/.6,0,1))))+fmax(0,(field-f[1])/f[3]);
        detail+=f[4]*(1-smooth((radius-.25)/.75));
    }
    return field+env*band*detail;
}

int qlcg_river_bank_section(uint32_t seed,int cell,double along,double offset,
                            double radius,double depth,double level,double natural,
                            double curvature,double *height,int *wet) {
    double distance=fabs(offset),side=offset>=0?1:-1,inner=fmax(0,curvature*side);
    double v=(along-floor(along/32)*32)/32,env=4*v*(1-v),gap=natural-level;
    *wet=0; *height=natural;
    if (distance<=radius) {
        double edge=qlcg_river_bank_field(seed,cell,along,(int)side,.5);
        double shift=fmin(radius*.14,4.2)*clampd((.25-edge*.45)*(.85+inner*.3),0,1)*env;
        double effective=fmax(radius*.75,radius-shift);
        if (distance<=effective) {
            double rw=effective>0?distance/effective:1;
            double field=qlcg_river_bank_field(seed,cell,along,(int)side,rw*.85);
            double shoal=clampd(.64-field*.42-inner*.18,.12,1);
            double profile=(1-rw*rw)*(.34+.66*shoal*(1-.38*rw));
            *height=fmin(natural,fmin(level,natural-.04)-depth*profile);
            *wet=*height<level-1e-3; return 1;
        }
        double u=(distance-effective)/fmax(1e-4,radius-effective);
        double bench=fmin(fmax(0,gap),(.28+.22*inner)*env);
        *height=fmin(natural,level+.02+bench*smooth(u)); return 1;
    }
    double band=clampd((depth+fmax(0,gap))*1.15,6,48);
    if (distance>=radius+band) return 0;
    if (gap<=0) return 1;
    double field=qlcg_river_bank_field(seed,cell,along,(int)side,1);
    double shoal=clampd(.66-field*.42,.12,1),bench=fmin(gap,(.28+.22*inner)*env);
    double rise=smooth((distance-radius)/band),start=level+bench;
    *height=fmin(natural,start+(natural-start)*clampd(rise*(1.12-.24*shoal),0,1));
    return 1;
}
