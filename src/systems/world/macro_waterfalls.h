#ifndef QLCG_MACRO_WATERFALLS_H
#define QLCG_MACRO_WATERFALLS_H
#include <stdint.h>
typedef struct {
    int state,style;
    double begin,end,top,bottom,width;
} MacroWaterfall;
double qlcg_macro_fall_level(const MacroWaterfall *p,double arc,double lateral,double base);
double qlcg_macro_fall_width(const MacroWaterfall *p,double arc);
double qlcg_macro_fall_island(const MacroWaterfall *p,double arc,double lateral);
double qlcg_macro_fall_recess(uint32_t seed,int cell,double along,double offset,double radius);
#endif
