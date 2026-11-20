#include "world_civ_growth.h"
#include "world_civ_economy.h"
#include "world_claims.h"
#include <math.h>
#include <stdlib.h>

enum { CIV_ATTEMPTS = 64, CIV_CLAIMS = 8 };
typedef struct {
    int *cells;
    int head, count, capacity, flag;
} CivFrontier;
struct CivGrowthCache {
    unsigned char *seen;
    int cells;
    uint32_t revision;
    CivFrontier frontier[MAX_FACTIONS];
};

void qlcg_civ_growth_free(MacroWorld *w) {
    if (!w || !w->civ_growth) return;
    for (int f=0; f<MAX_FACTIONS; f++) free(w->civ_growth->frontier[f].cells);
    free(w->civ_growth->seen);
    free(w->civ_growth);
    w->civ_growth=NULL;
}

static bool enqueue(CivFrontier *q, int cell) {
    if (q->count==q->capacity) {
        int capacity=q->capacity?q->capacity*2:32;
        int *cells=malloc((size_t)capacity*sizeof(*cells));
        if (!cells) return false;
        for (int i=0; i<q->count; i++) cells[i]=q->cells[(q->head+i)%q->capacity];
        free(q->cells);
        q->cells=cells;
        q->capacity=capacity;
        q->head=0;
    }
    q->cells[(q->head+q->count++)%q->capacity]=cell;
    return true;
}

static int periods(int before, int now, float rate, int days, int cap) {
    if (!isfinite(rate) || rate<=0) return 0;
    double delta=floor((double)now*rate/days)-floor((double)before*rate/days);
    return delta>=cap?cap:delta>0?(int)delta:0;
}

static struct CivGrowthCache *cache_for(MacroWorld *w) {
    if (w->civ_growth && w->civ_growth->cells!=w->cell_count) qlcg_civ_growth_free(w);
    if (w->civ_growth && w->civ_growth->revision!=w->territory_revision) qlcg_civ_growth_free(w);
    for (int f=0; w->civ_growth && f<w->faction_count && f<MAX_FACTIONS; f++) {
        int flag=w->civ_growth->frontier[f].flag;
        if (flag>=0 && flag!=w->factions[f].flag_cell) qlcg_civ_growth_free(w);
    }
    if (!w->civ_growth) {
        struct CivGrowthCache *cache=calloc(1,sizeof(*cache));
        if (!cache) return NULL;
        cache->seen=calloc((size_t)w->cell_count,1);
        if (!cache->seen) { free(cache); return NULL; }
        cache->cells=w->cell_count;
        cache->revision=w->territory_revision;
        for (int f=0; f<MAX_FACTIONS; f++) cache->frontier[f].flag=-1;
        w->civ_growth=cache;
    }
    return w->civ_growth;
}

bool qlcg_civ_growth_step(MacroWorld *w, int previous_day, int current_day) {
    if (!w->civ_economy && !qlcg_civ_economy_update(w,previous_day,previous_day,false)) return false;
    bool changed=false;
    struct CivGrowthCache *cache=NULL;
    for (int f=0; f<w->faction_count && f<MAX_FACTIONS; f++) {
        TerritoryFaction *tf=&w->factions[f];
        if (!tf->active) continue;
        float efficiency=qlcg_civ_labor_efficiency(w,tf)*qlcg_civ_economy_labor_scale(w,tf);
        int claims=periods(previous_day,current_day,w->opt.natural_expansion*efficiency,12,CIV_CLAIMS);
        int levels=periods(previous_day,current_day,w->opt.natural_evolution*efficiency,60,4);
        for (int step=0;step<levels && tf->level<5;step++) {
            if (!qlcg_civ_economy_pay_upgrade(w,tf)) break;
            tf->level++;changed=true;
        }
        if(w->chunk_claims) {
            for(int step=0;step<claims;step++) {
                int x,z;
                if(qlcg_civ_economy_expand_block(w,tf)!=CIV_BLOCK_NONE||
                   !qlcg_claims_candidate(w,f,&x,&z)) break;
                size_t count;
                qlcg_claims_list(w,&count);
                if(!qlcg_claims_reserve(w,count+1)||!qlcg_civ_economy_pay_expand(w,tf)) break;
                if(qlcg_claims_add(w,x,z,f,QLCG_TERRITORY_CHUNK)) changed=true;
            }
            continue;
        }
        if (claims && !cache) cache=cache_for(w);
        if (!claims || !cache) continue;
        CivFrontier *q=&cache->frontier[f];
        int flag=tf->flag_cell;
        if (flag<0 || flag>=w->cell_count || w->claim_faction[flag]!=f) continue;
        if (q->flag!=flag) {
            q->head=q->count=0;
            q->flag=flag;
            if (!enqueue(q,flag)) continue;
            cache->seen[flag]=(unsigned char)(f+1);
        }
        int remaining=claims;
        bool expansion_blocked=false;
        for (int attempt=0; attempt<CIV_ATTEMPTS && q->count && remaining>0; attempt++) {
            int cell=q->cells[q->head];
            q->head=(q->head+1)%q->capacity;
            q->count--;
            if (w->claim_faction[cell]!=f) continue;
            bool retry=false;
            for (int k=0; k<8; k++) {
                int nb=w->neighbors[cell*8+k];
                if (nb<0 || nb>=w->cell_count) continue;
                int owner=w->claim_faction[nb];
                if (owner==f && cache->seen[nb]!=(unsigned char)(f+1)) {
                    if (enqueue(q,nb)) cache->seen[nb]=(unsigned char)(f+1);
                } else if (owner<0 && !w->water[nb]) {
                    if (!remaining || expansion_blocked) { retry=true; continue; }
                    if (qlcg_civ_economy_expand_block(w,tf)!=CIV_BLOCK_NONE) {
                        expansion_blocked=true;retry=true;continue;
                    }
                    if (!enqueue(q,nb)) { retry=true; continue; }
                    if (!qlcg_civ_economy_pay_expand(w,tf)) continue;
                    w->claim_faction[nb]=(int16_t)f;
                    w->claim_type[nb]=1;
                    w->chunk_type[nb]=9000;
                    cache->seen[nb]=(unsigned char)(f+1);
                    remaining--;
                    changed=true;
                }
            }
            if (retry) enqueue(q,cell);
        }
    }
    if (w->civ_growth && w->civ_growth->revision==w->territory_revision)
        w->civ_growth->revision=w->territory_revision+(changed?1u:0u);
    return changed;
}
