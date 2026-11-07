#ifndef QLCG_RIVER_JUNCTION_H
#define QLCG_RIVER_JUNCTION_H

#include <stdint.h>

typedef struct {
    int first,second,mirror;
    uint32_t seed;
    double east,south,radius,level,depth,width,blend;
} RiverJunction;

typedef struct { double bar,shelf,scour,branch; } JunctionRelief;

void qlcg_junction_configure(RiverJunction *plan,uint32_t seed,double x,double z);
void qlcg_junction_relief(const RiverJunction *plan,double x,double z,JunctionRelief *out);
double qlcg_junction_weight(const RiverJunction *plan,double distance);

#endif
