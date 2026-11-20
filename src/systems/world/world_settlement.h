#ifndef QLCG_WORLD_SETTLEMENT_H
#define QLCG_WORLD_SETTLEMENT_H
#include "planet_macro.h"

#include "model/camp_layout.h"

#define QLCG_FLAG_INTERACT_REACH 6.0f
#define QLCG_SETTLEMENT_PLAZA_X 9.6f
#define QLCG_WORKBENCH_MODEL "house_0004_yard_1_fishing_bench"
#define QLCG_WORKBENCH_SCALE .4f
const TerritoryFaction *qlcg_settlement_chunk(const MacroWorld *w,int cx,int cz);
int qlcg_settlement_claim_type(const MacroWorld *w,int cx,int cz);
float qlcg_settlement_yaw(const TerritoryFaction *f);
int qlcg_settlement_layout(const TerritoryFaction *f,QlcgSettlementPart parts[QLCG_SETTLEMENT_PARTS]);
bool qlcg_settlement_cleared(const TerritoryFaction *f,float x,float z,float padding);
bool qlcg_settlement_base(const RegionData *chunk,float x,float z,float *height,float *dx,float *dz);
bool qlcg_settlement_yard_footprint(const QlcgSettlementPart *part,float center[2],float half[2]);
bool qlcg_settlement_blocked(const RegionData *reg,float x,float feet,float z,float height,float half);
float qlcg_settlement_landing(const RegionData *reg,float x,float z,float from,float to,float half);
float qlcg_settlement_ceiling(const RegionData *reg,float x,float z,float from,float to,float half);
#endif
