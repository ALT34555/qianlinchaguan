/* 主河缓存、版本与编辑预览 */
#include "hydrology_river_internal.h"
#include <stdlib.h>

#define CURVE_LIMIT 32
#define RIVER_CELL_CACHE 128

struct RiverSurfaceCache {
    uint32_t revision;
    unsigned long long serial;
    RiverCurve *items[CURVE_LIMIT];
    RiverCurve *detail_preview;
    RiverCellCache cells[RIVER_CELL_CACHE];
    JunctionSlot junctions[RIVER_CELL_CACHE];
};

static struct RiverSurfaceCache *surface_cache_at(const MacroWorld *w) {
    MacroWorld *mutable=(MacroWorld *)w;
    if (w->river_surface && w->river_surface->revision!=w->surface_revision) qlcg_river_surface_free(mutable);
    if (!w->river_surface) {
        mutable->river_surface=calloc(1,sizeof(*w->river_surface));
        if (!w->river_surface) return NULL;
        mutable->river_surface->revision=w->surface_revision;
    }
    return w->river_surface;
}

RiverCurve *qlcg_river_curve_at(const MacroWorld *w,int cell) {
    struct RiverSurfaceCache *cache=surface_cache_at(w);
    if (!cache) return NULL;
    int slot=-1;
    for (int i=0;i<CURVE_LIMIT;i++) {
        RiverCurve *c=cache->items[i];
        if (c && c->cell==cell) { c->used=++cache->serial; return c; }
        if (!c) slot=i;
        else if (!c->profile_building &&
                 (slot<0 || (cache->items[slot] && c->used<cache->items[slot]->used))) slot=i;
    }
    if (slot<0) return NULL;
    RiverCurve *c=qlcg_river_curve_build(w,cell); if (!c) return NULL;
    qlcg_river_curve_free(cache->items[slot]); cache->items[slot]=c; c->used=++cache->serial; return c;
}

void qlcg_river_surface_free(MacroWorld *w) {
    if (!w->river_surface) return;
    for (int i=0;i<CURVE_LIMIT;i++) qlcg_river_curve_free(w->river_surface->items[i]);
    free(w->river_surface); w->river_surface=NULL;
}

RiverCellCache *qlcg_river_candidate_slot(const MacroWorld *w,int cell) {
    struct RiverSurfaceCache *cache=surface_cache_at(w);
    return cache?&cache->cells[(cell^(cell>>8))&(RIVER_CELL_CACHE-1)]:NULL;
}
JunctionSlot *qlcg_river_junction_slot(const MacroWorld *w,int cell) {
    struct RiverSurfaceCache *cache=surface_cache_at(w);
    return cache?&cache->junctions[(cell^(cell>>8))&(RIVER_CELL_CACHE-1)]:NULL;
}
RiverCurve *qlcg_river_preview(const MacroWorld *w) {
    return w->river_surface?w->river_surface->detail_preview:NULL;
}
RiverCurve *qlcg_river_preview_swap(const MacroWorld *w,RiverCurve *preview) {
    if(!w->river_surface) return NULL;
    RiverCurve *previous=w->river_surface->detail_preview;
    w->river_surface->detail_preview=preview;
    return previous;
}
