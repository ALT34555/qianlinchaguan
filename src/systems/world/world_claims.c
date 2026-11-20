#include "world_claims.h"
#include "world_civ_economy.h"
#include "surface_internal.h"
#include "core/math_util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

const int qlcg_claim_directions[4][2]={{0,-1},{1,0},{0,1},{-1,0}};

struct WorldClaims {
    QlcgChunkClaim *items;
    uint16_t *index;
    size_t count,capacity,index_capacity;
    uint32_t surface_revision,delta_version;
    uint16_t first[MAX_FACTIONS],last[MAX_FACTIONS],totals[MAX_FACTIONS];
};

bool qlcg_claims_canonical(const MacroWorld *w,int *cx,int cz) {
    int width=w->equator_chunks>0?w->equator_chunks:(int)lroundf(w->circumference/CHUNK_SIZE);
    int half=width/2;
    if(width<=0) return false;
    if(qlcg_world_flat(w)) return *cx>=-half&&*cx<half&&cz>=-half&&cz<half;
    if(cz < -width/4 || cz>=width/4) return false;
    *cx%=width;
    if(*cx< -half) *cx+=width;
    if(*cx>=half) *cx-=width;
    return true;
}

static size_t index_slot(const struct WorldClaims *claims,int cx,int cz) {
    const size_t mask=claims->index_capacity-1;
    size_t at=qlcg_hash_u32((unsigned)cx^qlcg_hash_u32((unsigned)cz))&mask;
    for(;claims->index[at];at=(at+1)&mask) {
        const QlcgChunkClaim *entry=claims->items+claims->index[at]-1;
        if(entry->cx==cx&&entry->cz==cz) break;
    }
    return at;
}

const QlcgChunkClaim *qlcg_claims_list(const MacroWorld *w,size_t *count) {
    *count=w->chunk_claims?w->chunk_claims->count:0;
    return *count?w->chunk_claims->items:NULL;
}

const QlcgChunkClaim *qlcg_claims_at(const MacroWorld *w,int cx,int cz) {
    const struct WorldClaims *claims=w->chunk_claims;
    if(!claims||!claims->count||!qlcg_claims_canonical(w,&cx,cz)) return NULL;
    uint16_t entry=claims->index[index_slot(claims,cx,cz)];
    return entry?claims->items+entry-1:NULL;
}

bool qlcg_claims_reserve(MacroWorld *w,size_t count) {
    if(count>QLCG_MAX_CHUNK_CLAIMS) return false;
    if(!w->chunk_claims) {
        w->chunk_claims=calloc(1,sizeof(*w->chunk_claims));
        if(!w->chunk_claims) return false;
    }
    struct WorldClaims *claims=w->chunk_claims;
    if(count<=claims->capacity) return true;
    size_t capacity=claims->capacity?claims->capacity:16;
    while(capacity<count) capacity*=2;
    uint16_t *index=calloc(capacity*2,sizeof(*index));
    if(!index) return false;
    QlcgChunkClaim *items=realloc(claims->items,capacity*sizeof(*items));
    if(!items) {free(index);return false;}
    free(claims->index);
    claims->items=items;claims->index=index;
    claims->capacity=capacity;claims->index_capacity=capacity*2;
    for(size_t i=0;i<claims->count;i++) index[index_slot(claims,items[i].cx,items[i].cz)]=(uint16_t)(i+1);
    return true;
}

void qlcg_claims_clear(MacroWorld *w) {
    struct WorldClaims *claims=w->chunk_claims;
    if(!claims) return;
    claims->count=0;
    if(claims->index) memset(claims->index,0,claims->index_capacity*sizeof(*claims->index));
    memset(claims->first,0,sizeof(claims->first));
    memset(claims->last,0,sizeof(claims->last));
    memset(claims->totals,0,sizeof(claims->totals));
}

void qlcg_claims_free(MacroWorld *w) {
    if(!w->chunk_claims) return;
    free(w->chunk_claims->items);
    free(w->chunk_claims->index);
    free(w->chunk_claims);
    w->chunk_claims=NULL;
}

bool qlcg_claims_add(MacroWorld *w,int cx,int cz,int faction,int type) {
    if(faction<0||faction>=MAX_FACTIONS||(type!=QLCG_FLAG_CHUNK&&type!=QLCG_TERRITORY_CHUNK)||
       !qlcg_claims_canonical(w,&cx,cz)||qlcg_claims_at(w,cx,cz)) return false;
    size_t count=w->chunk_claims?w->chunk_claims->count:0;
    if(!qlcg_claims_reserve(w,count+1)) return false;
    struct WorldClaims *claims=w->chunk_claims;
    uint16_t entry=(uint16_t)(count+1);
    QlcgChunkClaim *claim=claims->items+count;
    *claim=(QlcgChunkClaim){.cx=cx,.cz=cz,.faction=(uint16_t)faction,.type=(uint8_t)type,.edges=15};
    claims->index[index_slot(claims,cx,cz)]=entry;
    claims->count++;
    if(claims->last[faction]) claims->items[claims->last[faction]-1].next=entry;
    else claims->first[faction]=entry;
    claims->last[faction]=entry;
    claims->totals[faction]++;
    for(int dir=0;dir<4;dir++) {
        int nx=cx+qlcg_claim_directions[dir][0],nz=cz+qlcg_claim_directions[dir][1];
        if(!qlcg_claims_canonical(w,&nx,nz)) continue;
        uint16_t neighbor=claims->index[index_slot(claims,nx,nz)];
        if(neighbor&&claims->items[neighbor-1].faction==faction) {
            claim->edges&=(uint8_t)~(1u<<dir);
            claims->items[neighbor-1].edges&=(uint8_t)~(1u<<((dir+2)%4));
        }
    }
    return true;
}

bool qlcg_claims_land(const MacroWorld *w,int cx,int cz) {
    if(!qlcg_claims_canonical(w,&cx,cz)) return false;
    float x=(cx+.5f)*CHUNK_SIZE,z=(cz+.5f)*CHUNK_SIZE;
    if(!w->height) return w->water&&!w->water[qlcg_world_cell_at_wxz(w,x,z)];
    float ground,water;
    if(w->decor_ready) qlcg_sample_surface_point(w,x,z,&ground,&water,NULL,NULL,NULL,NULL,NULL,NULL);
    else qlcg_sample_surface_map(w,x,z,&ground,&water,NULL,NULL);
    return isfinite(ground)&&ground>water+.02f;
}

static void seed_neighbors(MacroWorld *w,const TerritoryFaction *f) {
    for(int dir=0;dir<4;dir++) {
        int x=f->cx+qlcg_claim_directions[dir][0],z=f->cz+qlcg_claim_directions[dir][1];
        if(qlcg_claims_land(w,x,z)) qlcg_claims_add(w,x,z,f->id,QLCG_TERRITORY_CHUNK);
    }
}

bool qlcg_claims_seed(MacroWorld *w,const TerritoryFaction *f) {
    size_t count;
    qlcg_claims_list(w,&count);
    if(!qlcg_claims_reserve(w,count+5)||!qlcg_claims_add(w,f->cx,f->cz,f->id,QLCG_FLAG_CHUNK)) return false;
    seed_neighbors(w,f);
    return true;
}

bool qlcg_claims_candidate(MacroWorld *w,int faction,int *cx,int *cz) {
    struct WorldClaims *claims=w->chunk_claims;
    if(!claims||faction<0||faction>=MAX_FACTIONS) return false;
    if(claims->surface_revision!=w->surface_revision||claims->delta_version!=w->delta_version) {
        for(size_t i=0;i<claims->count;i++) claims->items[i].blocked_edges=0;
        claims->surface_revision=w->surface_revision;
        claims->delta_version=w->delta_version;
    }
    for(uint16_t at=claims->first[faction];at;at=claims->items[at-1].next) {
        QlcgChunkClaim *claim=claims->items+at-1;
        for(int dir=0;dir<4;dir++) {
            uint8_t edge=(uint8_t)(1u<<dir);
            if(!(claim->edges&edge)||(claim->blocked_edges&edge)) continue;
            int x=claim->cx+qlcg_claim_directions[dir][0],z=claim->cz+qlcg_claim_directions[dir][1];
            if(qlcg_claims_at(w,x,z)) continue;
            if(qlcg_claims_land(w,x,z)) {
                *cx=x;*cz=z;return true;
            }
            claim->blocked_edges|=edge;
        }
    }
    return false;
}

int qlcg_claims_count(const MacroWorld *w,int faction) {
    return w->chunk_claims&&faction>=0&&faction<MAX_FACTIONS?w->chunk_claims->totals[faction]:0;
}

bool qlcg_claims_migrate(MacroWorld *w) {
    if(!qlcg_claims_reserve(w,(size_t)w->faction_count*5)) return false;
    qlcg_claims_clear(w);
    for(int i=0;i<w->faction_count;i++) {
        TerritoryFaction *f=w->factions+i;
        if(!f->active) continue;
        if(!qlcg_claims_canonical(w,&f->cx,f->cz)) {
            float x,z;
            qlcg_world_cell_center(w,f->flag_cell,&x,&z);
            f->cx=(int)floorf(x/CHUNK_SIZE);f->cz=(int)floorf(z/CHUNK_SIZE);
        }
        f->wx=(f->cx+.5f)*CHUNK_SIZE;f->wz=(f->cz+.5f)*CHUNK_SIZE;
        if(!qlcg_claims_add(w,f->cx,f->cz,i,QLCG_FLAG_CHUNK)) return false;
    }
    for(int i=0;i<w->faction_count;i++) if(w->factions[i].active) seed_neighbors(w,w->factions+i);
    for(int i=0;i<w->faction_count;i++) {
        const TerritoryFaction *f=w->factions+i;
        if(!f->active) continue;
        int previous=qlcg_civ_expansion_budget_used(w,f),restored=0;
        if(previous>QLCG_CIV_POP_MAX*4+40) previous=QLCG_CIV_POP_MAX*4+40;
        for(;restored<previous;restored++) {
            int x,z;
            if(!qlcg_claims_candidate(w,i,&x,&z)) break;
            if(!qlcg_claims_add(w,x,z,i,QLCG_TERRITORY_CHUNK)) return false;
        }
        qlcg_civ_restore_expansion_budget(w,f,restored);
    }
    w->territory_revision++;
    return true;
}

bool qlcg_claims_release(MacroWorld *w,int cx,int cz,int faction) {
    const QlcgChunkClaim *removed=qlcg_claims_at(w,cx,cz);
    if(!removed || removed->type==QLCG_FLAG_CHUNK || removed->faction!=faction) return false;
    /* 发布编辑前重建哈希、阵营链与边界掩码 */
    MacroWorld *copy=calloc(1,sizeof(*copy));
    if(!copy) return false;
    copy->opt=w->opt;copy->circumference=w->circumference;copy->equator_chunks=w->equator_chunks;
    size_t count;
    const QlcgChunkClaim *claims=qlcg_claims_list(w,&count);
    bool ok=qlcg_claims_reserve(copy,count);
    for(size_t i=0;i<count && ok;i++) if(claims+i!=removed)
        ok=qlcg_claims_add(copy,claims[i].cx,claims[i].cz,claims[i].faction,claims[i].type);
    if(ok) {
        struct WorldClaims *old=w->chunk_claims;w->chunk_claims=copy->chunk_claims;copy->chunk_claims=old;
    }
    qlcg_claims_free(copy);free(copy);return ok;
}
