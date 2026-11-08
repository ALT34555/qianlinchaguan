#ifndef QLCG_WORLD_INFINITE_H
#define QLCG_WORLD_INFINITE_H
#include "planet_macro.h"
#include <stdio.h>
typedef struct InfiniteWorld {
    MacroWorld **items;
    size_t count, capacity;
    int32_t min_x,max_x,min_z,max_z;
} InfiniteWorld;
uint32_t qlcg_infinite_seed(uint32_t seed,int32_t x,int32_t z);
void qlcg_infinite_options(WorldCreatorOptions *opt);
float qlcg_infinite_climate_weight(int x,int y,int cols,int rows);
float qlcg_infinite_climate_base(const WorldCreatorOptions *opt,int x,int y,int cols,int rows);
MacroWorld *qlcg_infinite_find(const MacroWorld *w,int32_t x,int32_t z);
MacroWorld *qlcg_infinite_resolve(const MacroWorld *w,double wx,double wz,double *local_x,double *local_z);
void qlcg_infinite_bounds(const MacroWorld *w,double bounds[4]);
bool qlcg_infinite_attach(MacroWorld *owner,MacroWorld *tile);
MacroWorld *qlcg_infinite_explore(MacroWorld *w,float *wx,float *wz);
void qlcg_infinite_free_others(MacroWorld *w);
bool qlcg_infinite_read(FILE *fp,MacroWorld *w,long limit);
#endif
