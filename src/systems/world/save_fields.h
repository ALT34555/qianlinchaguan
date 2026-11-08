#ifndef QLCG_SAVE_FIELDS_H
#define QLCG_SAVE_FIELDS_H
#include "core/binary.h"
#include "systems/world/planet_macro.h"
void qlcg_fields_profile(QlCodec *io, PlayerProfile *s);
void qlcg_fields_bind(QlCodec *io, QlBind *s);
void qlcg_fields_settings(QlCodec *io, GameSettings *s);
void qlcg_fields_options(QlCodec *io, WorldCreatorOptions *s);
void qlcg_fields_delta(QlCodec *io, SparseDeltaRecord *s);
void qlcg_fields_faction(QlCodec *io, TerritoryFaction *s);
void qlcg_fields_layer(QlCodec *io, AtlasLayerSetting *s);
void qlcg_fields_atlas(QlCodec *io, AtlasDisplayState *s);
void qlcg_fields_slot(QlCodec *io, SaveSlotInfo *s);
#endif
