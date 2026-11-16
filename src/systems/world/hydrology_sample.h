#ifndef QLCG_HYDROLOGY_SAMPLE_H
#define QLCG_HYDROLOGY_SAMPLE_H
#include "hydrology_types.h"
#include "hydrology_lakes.h"

void qlcg_hydrology_sample(const MacroWorld *w,const int corners[4],float wx,float wz,
                           float natural,const LakeShoreField *lake,HydrologySample *out,int source_mode);
#endif
