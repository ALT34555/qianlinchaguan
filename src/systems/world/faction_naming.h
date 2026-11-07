#ifndef QLCG_FACTION_NAMING_H
#define QLCG_FACTION_NAMING_H

#include "planet_macro.h"

void qlcg_faction_init_identity(TerritoryFaction *faction, uint32_t seed,
                                const char *salt, const char *name);
void qlcg_faction_set_name(TerritoryFaction *faction, const char *name);
void qlcg_faction_generate_name(TerritoryFaction *faction, int natural_type, const WorldCreatorOptions *opt);
bool qlcg_culture_checked(const WorldCreatorOptions *opt, const char *code);
void qlcg_world_set_culture(MacroWorld *world, const WorldCreatorOptions *opt);
const char *qlcg_faction_display_name(const TerritoryFaction *faction);

#endif
