#ifndef QLCG_WORLD_CIV_ECONOMY_H
#define QLCG_WORLD_CIV_ECONOMY_H
#include "planet_macro.h"
#include <stdio.h>

enum { QLCG_CIV_INITIAL_POPULATION=3, QLCG_CIV_FARMS=2, QLCG_CIV_POP_MAX=12, QLCG_CIV_RESOURCE_MAX=1000000 };
enum { CIV_BLOCK_NONE,CIV_BLOCK_FLAG,CIV_BLOCK_FOOD,CIV_BLOCK_WOOD,CIV_BLOCK_FIBER,
       CIV_BLOCK_POPULATION,CIV_BLOCK_LAND_LIMIT,CIV_BLOCK_LEVEL_LIMIT };
typedef struct {
    int population,capacity,food,wood,fiber,farm_count;
    int expansion_used,expansion_limit,land_chunks,level,starvation_days;
    int expand_block,upgrade_block;
    int food_reserve;
    uint32_t revision;
} QlcgCivEconomySnapshot;
typedef struct {
    int plot,id,cx,cz;
    float x,z,width,depth,work_x,work_z,yaw;
    int stage,season;
    const char *crop_id;
    const char *farm_type_id;
    const char *plant_id;
} QlcgFarmPlot;
typedef struct { int food,wood,fiber,population; } QlcgCivCost;
typedef struct {
    const MacroWorld *owner;
    const TerritoryFaction *faction;
    int cx,cz,cell,type;
} QlcgCivTerritory;

bool qlcg_civ_economy_update(MacroWorld *w,int previous_day,int current_day,bool simulate);
void qlcg_civ_economy_free(MacroWorld *w);
bool qlcg_civ_economy_snapshot(const MacroWorld *w,const TerritoryFaction *f,QlcgCivEconomySnapshot *out);
bool qlcg_civ_territory_at(const MacroWorld *w,int cx,int cz,QlcgCivTerritory *out);
bool qlcg_civ_economy_at(const MacroWorld *w,int cx,int cz,const MacroWorld **owner,
                       const TerritoryFaction **f,QlcgCivEconomySnapshot *out);
int qlcg_civ_farms(const MacroWorld *w,const TerritoryFaction *f,QlcgFarmPlot *out,int capacity);
bool qlcg_civ_territory_farm(const QlcgCivTerritory *territory,QlcgFarmPlot *out);
bool qlcg_civ_farm_at(const MacroWorld *w,int cx,int cz,QlcgFarmPlot *out);
int qlcg_civ_farm_chunk_type(const MacroWorld *w,int cx,int cz);
/* farm_type小于0时查询农田 */
int qlcg_civ_chunk_type(const MacroWorld *w,int cx,int cz,int natural_type,int farm_type);
uint32_t qlcg_civ_farms_revision(const MacroWorld *w);
void qlcg_civ_economy_invalidate_farms(MacroWorld *w,bool land);
QlcgCivCost qlcg_civ_expand_cost(void);
QlcgCivCost qlcg_civ_upgrade_cost(int level);
int qlcg_civ_economy_expand_block(const MacroWorld *w,const TerritoryFaction *f);
int qlcg_civ_economy_upgrade_block(const MacroWorld *w,const TerritoryFaction *f);
bool qlcg_civ_economy_pay_expand(MacroWorld *w,const TerritoryFaction *f);
bool qlcg_civ_economy_pay_upgrade(MacroWorld *w,const TerritoryFaction *f);
bool qlcg_civ_economy_ration(const MacroWorld *w,const TerritoryFaction *f,int utc_day);
bool qlcg_civ_economy_credit_harvest(MacroWorld *w,const TerritoryFaction *f,int food,int wood,int fiber);
int qlcg_civ_expansion_budget_used(const MacroWorld *w,const TerritoryFaction *f);
void qlcg_civ_restore_expansion_budget(MacroWorld *w,const TerritoryFaction *f,int used);
int qlcg_civ_npc_count(const MacroWorld *w,const TerritoryFaction *f);
float qlcg_civ_economy_labor_scale(const MacroWorld *w,const TerritoryFaction *f);
float qlcg_civ_labor_efficiency(const MacroWorld *w,const TerritoryFaction *f);
const char *qlcg_civ_economy_block_name(int block);
bool qlcg_civ_economy_write(FILE *file,const MacroWorld *w);
bool qlcg_civ_economy_read_mem(const void *data,size_t size,MacroWorld *w);
bool qlcg_civ_economy_read_mem_ver(const void *data,size_t size,MacroWorld *w,uint32_t version);
#endif
