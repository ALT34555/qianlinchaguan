#ifndef QLCG_EDITOR_BUILDINGS_H
#define QLCG_EDITOR_BUILDINGS_H
#include "../world_lot.h"



enum { LOT_EXT_BASE=1000 };
int qlcg_lot_style_count(void);
const char *qlcg_lot_style_id(int style);
const char *qlcg_lot_style_name(int style);
int qlcg_lot_style_unit(int style,int ordinal);
const QlcgBuildingModel *qlcg_extra_model(int id);
bool qlcg_extra_load(int id);
void qlcg_extra_free(void);
bool qlcg_extra_preset(QlcgLot *lot,int style,uint32_t seed,int turn);
#endif
