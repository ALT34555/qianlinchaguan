/* 舆图雨云图层：降水场纹理 */
#ifndef QLCG_ATLAS_RAIN_H
#define QLCG_ATLAS_RAIN_H

#include "atlas_view.h"

/* 按当前天气档位生成降水纹理（4 小时一档，限流） */
void qlcg_atlas_rain_sync(AtlasRenderer *ar, const MacroWorld *w, const CalendarClock *clk);

#endif
