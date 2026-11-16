#ifndef QLCG_HYDROLOGY_OXBOW_H
#define QLCG_HYDROLOGY_OXBOW_H
#include "hydrology_types.h"

const OxbowPlan *qlcg_oxbow_plan(const MacroWorld *w,int cell);
void qlcg_oxbow_generate(MacroWorld *w,int cell);
void qlcg_oxbow_free(MacroWorld *w);
void qlcg_oxbow_surface(const MacroWorld *w,double wx,double wz,RiverSurfaceSample *out);
#endif
