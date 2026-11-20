#ifndef QLCG_WORLD_CLAIMS_H
#define QLCG_WORLD_CLAIMS_H

#include "planet_macro.h"

enum { QLCG_FLAG_CHUNK = 2, QLCG_TERRITORY_CHUNK = 1, QLCG_MAX_CHUNK_CLAIMS = MAX_FACTIONS * 128 };
typedef struct {
    int cx,cz;
    uint16_t faction,next;
    uint8_t type,edges,blocked_edges;
} QlcgChunkClaim;

extern const int qlcg_claim_directions[4][2];
const QlcgChunkClaim *qlcg_claims_list(const MacroWorld *w,size_t *count);
const QlcgChunkClaim *qlcg_claims_at(const MacroWorld *w,int cx,int cz);
bool qlcg_claims_canonical(const MacroWorld *w,int *cx,int cz);
bool qlcg_claims_reserve(MacroWorld *w,size_t count);
void qlcg_claims_clear(MacroWorld *w);
void qlcg_claims_free(MacroWorld *w);
bool qlcg_claims_add(MacroWorld *w,int cx,int cz,int faction,int type);
bool qlcg_claims_seed(MacroWorld *w,const TerritoryFaction *f);
bool qlcg_claims_release(MacroWorld *w,int cx,int cz,int faction);
bool qlcg_claims_candidate(MacroWorld *w,int faction,int *cx,int *cz);
bool qlcg_claims_land(const MacroWorld *w,int cx,int cz);
int qlcg_claims_count(const MacroWorld *w,int faction);
bool qlcg_claims_migrate(MacroWorld *w);
#endif
