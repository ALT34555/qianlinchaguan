#ifndef QLCG_ATLAS_VEGETATION_H
#define QLCG_ATLAS_VEGETATION_H
#include "atlas_view.h"
#include "../systems/world/model/plant_guild.h"

int qlcg_atlas_guild_species(const QlcgGuildDef *guild);
void qlcg_atlas_vegetation_release(AtlasRenderer *ar);
void qlcg_atlas_draw_vegetation(AtlasRenderer *ar,const MacroWorld *w,
                               const CalendarClock *clock,UiContext *ui,float opacity,
                               float x,float y,float width,float height);
#endif
