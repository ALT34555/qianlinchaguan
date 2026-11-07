/* 共享球面局部坐标 */
#include "surface_frame.h"
#include <math.h>
static const double PI=3.14159265358979323846;
static double clampd(double x,double a,double b) { return fmax(a,fmin(b,x)); }
SurfaceFrame qlcg_surface_frame(const MacroWorld *w,int cell) {
    SurfaceFrame f={0};
    f.flat=qlcg_world_flat(w);
    if(f.flat) {
        f.C=w->circumference; f.radius=w->radius;
        f.anchor[0]=((cell%w->columns+.5)/w->columns-.5)*f.C;
        f.anchor[2]=((cell/w->columns+.5)/w->rows-.5)*f.C;
        return f;
    }
    f.C=w->circumference; f.radius=w->radius>0?w->radius:f.C/(2*PI);
    f.lon=((cell%w->columns+.5)/w->columns-.5)*2*PI;
    f.lat=(.25-(cell/w->columns+.5)/w->rows*.5)*2*PI;
    double cl=cos(f.lat),sl=sin(f.lat),ca=cos(f.lon),sa=sin(f.lon);
    f.anchor[0]=cl*ca; f.anchor[1]=sl; f.anchor[2]=cl*sa;
    f.east[0]=-sa; f.east[1]=0; f.east[2]=ca;
    f.south[0]=sl*ca; f.south[1]=-cl; f.south[2]=sl*sa;
    return f;
}
SurfacePoint qlcg_frame_local(const SurfaceFrame *f,double wx,double wz) {
    if(f->flat) return (SurfacePoint){wx-f->anchor[0],wz-f->anchor[2]};
    double a=wx/f->C*2*PI,b=-wz/f->C*2*PI;
    double p[3]={cos(b)*cos(a),sin(b),cos(b)*sin(a)},e=0,s=0,c=0;
    for (int k=0;k<3;k++) { e+=f->east[k]*p[k]; s+=f->south[k]*p[k]; c+=f->anchor[k]*p[k]; }
    double si=hypot(e,s),angle=atan2(si,c),scale=si?f->radius*angle/si:f->radius;
    return (SurfacePoint){e*scale,s*scale};
}
SurfacePoint qlcg_frame_coordinates(const SurfaceFrame *f,double x,double z) {
    if(f->flat) return (SurfacePoint){x+f->anchor[0],z+f->anchor[2]};
    double d=hypot(x,z),angle=d/f->radius,c=cos(angle),k=d?sin(angle)/d:0,p[3];
    for (int i=0;i<3;i++) p[i]=f->anchor[i]*c+(f->east[i]*x+f->south[i]*z)*k;
    return (SurfacePoint){atan2(p[2],p[0])/(2*PI)*f->C,-asin(clampd(p[1],-1,1))/(2*PI)*f->C};
}
