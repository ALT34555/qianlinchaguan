#ifndef QLCG_WORLD_FARM_SURFACE_H
#define QLCG_WORLD_FARM_SURFACE_H
#include "world_civ_economy.h"
#include "../civilization/civ_content.h"

typedef struct {
    QlcgFarmPlot plot;
    const QlcgFarmlandType *type;
    float cosine,sine;
} QlcgFarmSurface;

enum { QLCG_FARM_FOOTPRINT_POINTS=5 };

void qlcg_farm_surface_footprint(const QlcgFarmPlot *plot,float points[QLCG_FARM_FOOTPRINT_POINTS][2]);
bool qlcg_farm_surface_dry(float ground,float water);
bool qlcg_farm_surface_plot(QlcgFarmSurface *surface,const QlcgFarmPlot *plot);
bool qlcg_farm_surface_at(QlcgFarmSurface *surface,const MacroWorld *world,int cx,int cz);
bool qlcg_farm_surface_inside(const QlcgFarmSurface *surface,float x,float z,float padding);
void qlcg_farm_surface_sample(const QlcgFarmSurface *surface,float x,float z,float ground,float water,uint8_t farm[4]);
void qlcg_farm_surface_color(const uint8_t farm[4],uint8_t color[3]);
#endif
