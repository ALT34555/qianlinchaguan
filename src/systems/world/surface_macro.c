/* 连续宏观插值与极点反射 */
#include "surface_macro.h"
#include "surface_detail.h"
#include <math.h>

int qlcg_macro_index(const MacroWorld *w,int x,int y) {
    int rows=w->rows,cols=w->columns;
    if (qlcg_world_flat(w)) return (y<0?0:y>=rows?rows-1:y)*cols+(x<0?0:x>=cols?cols-1:x);
    y=(y%(rows*2)+rows*2)%(rows*2);
    if (y>=rows) { y=rows*2-y-1; x+=cols/2; }
    return y*cols+(x%cols+cols)%cols;
}
int qlcg_macro_cell_at(const MacroWorld *w,double wx,double wz) {
    int x=(int)floor((wx/w->circumference+.5)*w->columns);
    int y=(int)floor((wz/w->circumference+qlcg_world_z_offset(w))*w->columns);
    y=y<0?0:y>=w->rows?w->rows-1:y;
    return qlcg_world_flat(w) ? qlcg_macro_index(w,x,y) : y*w->columns+(x%w->columns+w->columns)%w->columns;
}
SurfaceMacroContext qlcg_macro_context(const MacroWorld *w,double wx,double wz) {
    double x=(wx/w->circumference+.5)*w->columns-.5;
    double z=(wz/w->circumference+qlcg_world_z_offset(w))*w->columns-.5;
    int ix=(int)floor(x),iz=(int)floor(z);
    double distance=fmin(fabs(wz+w->circumference/4),fabs(wz-w->circumference/4));
    double span=w->circumference/(double)w->columns,pole=-1;
    if (!qlcg_world_flat(w) && distance<span) { double t=fmax(0,fmin(1,distance/span)); pole=t*t*(3-2*t); }
    return (SurfaceMacroContext){qlcg_macro_index(w,ix,iz),qlcg_macro_index(w,ix+1,iz),
        qlcg_macro_index(w,ix,iz+1),qlcg_macro_index(w,ix+1,iz+1),wz<0?0:1,x-ix,z-iz,pole};
}
double qlcg_read_macro(const MacroWorld *w,const SurfaceMacroContext *c,const float *v) {
    double a=v[c->a]+((double)v[c->b]-v[c->a])*c->tx;
    double b=v[c->c]+((double)v[c->d]-v[c->c])*c->tx;
    double value=a+(b-a)*c->tz;
    if (c->pole>=0) {
        double pole=qlcg_detail_pole(w,v,c->pole_id,value);
        value=pole+(value-pole)*c->pole;
    }
    return value;
}
