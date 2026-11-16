#ifndef QLCG_PAINT_CLOUDS_H
#define QLCG_PAINT_CLOUDS_H
#include "terrain_view.h"
typedef struct PaintClouds PaintClouds;
PaintClouds *qlcg_clouds_create(void);
void qlcg_clouds_free(PaintClouds *clouds);
void qlcg_clouds_update(PaintClouds *clouds,const MacroWorld *world,
    const WeatherSystem *weather,double unix_ms,double time,double x,double z);
void qlcg_clouds_bind(const PaintClouds *clouds,GLuint program,double x,double z,float y);
void qlcg_clouds_sample(const MacroWorld *world,const WeatherSystem *weather,
    double unix_ms,double x,double z,float *cover,float *thickness);
#endif
