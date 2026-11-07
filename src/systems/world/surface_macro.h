#ifndef QLCG_SURFACE_MACRO_H
#define QLCG_SURFACE_MACRO_H
#include "planet_macro.h"

typedef struct { int a,b,c,d,pole_id; double tx,tz,pole; } SurfaceMacroContext;
SurfaceMacroContext qlcg_macro_context(const MacroWorld *w,double wx,double wz);
double qlcg_read_macro(const MacroWorld *w,const SurfaceMacroContext *c,const float *values);
int qlcg_macro_index(const MacroWorld *w,int x,int y);
int qlcg_macro_cell_at(const MacroWorld *w,double wx,double wz);

#endif
