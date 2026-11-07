#ifndef QLCG_PLANT_PHENOLOGY_H
#define QLCG_PLANT_PHENOLOGY_H

#include "../climate_season.h"
#include <stdint.h>

/* 按本地气候季边界与稳定身份错峰换季 */
int qlcg_plant_visual_season(const QlcgClimateSeason *cs,
                             float annual_temperature, uint32_t identity);

#endif
