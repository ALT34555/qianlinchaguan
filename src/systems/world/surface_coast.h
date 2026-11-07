#ifndef QLCG_SURFACE_COAST_H
#define QLCG_SURFACE_COAST_H
#include "surface_macro.h"

typedef struct {
    double height;
    float mud, sand, rock, weight;
} CoastSample;

CoastSample qlcg_coast_surface(const MacroWorld *w,const SurfaceMacroContext *c,
                              double wx,double wz,double base,double natural);

double qlcg_natural_detail_coast(const MacroWorld *w,double wx,double wz,CoastSample *out);

#endif
