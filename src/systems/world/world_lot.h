#ifndef QLCG_WORLD_LOT_H
#define QLCG_WORLD_LOT_H
#include "world_civ_economy.h"
#include "model/building_model.h"
#include "model/camp_layout.h"
#include "../civilization/civ_content.h"

enum { LOT_LAND=9000, LOT_CUSTOM=9002, LOT_PRESET=9003 };
enum { LOT_TEA=10000, LOT_FENCE, LOT_STONE_WALL, LOT_STEPS, LOT_DECK, LOT_LAMP, LOT_TIMBER, LOT_PLASTER, LOT_GATE, LOT_WIDE_GATE,
       LOT_WOOD_CHAIR, LOT_STUMP, LOT_STONE_TEA, LOT_TEA_SHELTER, LOT_POT, LOT_MODEL_END };
typedef struct {
    int type,pack,turn,count;
    uint32_t seed,revision;
    bool flag_moved;
    float flag_x,flag_z;
    char crop[QLCG_CIV_TYPE_ID];
    QlcgSettlementPart *parts;
} QlcgLot;
typedef struct QlcgLotEntry {
    int cx,cz,faction;
    QlcgLot lot;
    struct QlcgLotEntry *next;
} QlcgLotEntry;

const QlcgLot *qlcg_lot_local(const MacroWorld *owner,int cx,int cz);
const QlcgLot *qlcg_lot_at(const MacroWorld *world,int cx,int cz);
bool qlcg_lot_copy(QlcgLot *out,const QlcgLot *source);
bool qlcg_lot_commit(MacroWorld *world,int cx,int cz,const QlcgLot *replacement);
bool qlcg_lot_preset(QlcgLot *lot,int pack,uint32_t seed,int turn);
const QlcgBuildingModel *qlcg_lot_model(int id);
bool qlcg_lot_is_seat(int model);
bool qlcg_lot_is_wall(int model);
void qlcg_lot_models_init(void);
void qlcg_lot_models_free(void);
bool qlcg_lot_placeable(const QlcgLot *lot,const QlcgSettlementPart *part,int ignore);
int qlcg_lot_pick(const QlcgLot *lot,float x,float z,float pad);
bool qlcg_lot_socket(const QlcgSettlementPart *room,QlcgSettlementPart *door);
bool qlcg_lot_door(const QlcgLot *lot,QlcgSettlementPart *door);
bool qlcg_lot_put(QlcgLot *lot,QlcgSettlementPart part,int index);
void qlcg_lot_remove(QlcgLot *lot,int index);
/* 以阵营为中心返回设施（含跨缝地块） */
bool qlcg_lot_facility(const MacroWorld *w,const TerritoryFaction *f,bool tea,QlcgSettlementPart *out);
int qlcg_lot_homes(const MacroWorld *w,const TerritoryFaction *f,QlcgSettlementPart *out,int cap);
bool qlcg_lot_farm(const QlcgLot *lot,const QlcgCivTerritory *territory,QlcgFarmPlot *out);
void qlcg_lot_free(MacroWorld *world);
bool qlcg_lot_write(FILE *file,const MacroWorld *world);
bool qlcg_lot_read(FILE *file,long limit,MacroWorld *world);
#endif
