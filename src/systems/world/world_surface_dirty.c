#include "planet_macro.h"
#include <math.h>

static void publish_change(MacroWorld *w,uint32_t before_delta,uint32_t before_surface,QlcgSurfaceBounds bounds) {
    if (before_delta == w->delta_version && before_surface == w->surface_revision) return;
    QlcgSurfaceJournal *journal = &w->surface_changes;
    journal->changes[journal->next] = (QlcgSurfaceChange){
        bounds, before_delta, before_surface, w->delta_version, w->surface_revision
    };
    journal->next = (journal->next + 1) % QLCG_SURFACE_DIRTY_CAP;
    if (journal->count < QLCG_SURFACE_DIRTY_CAP) journal->count++;
}

void qlcg_world_surface_dirty(MacroWorld *w,uint32_t before_delta,uint32_t before_surface,
                             double x0,double z0,double x1,double z1,bool wrap_x) {
    if(!isfinite(x0)||!isfinite(x1)||!isfinite(z0)||!isfinite(z1)||x0>x1||z0>z1) return;
    publish_change(w,before_delta,before_surface,(QlcgSurfaceBounds){x0,z0,x1,z1,wrap_x,false});
}

void qlcg_world_surface_appearance(MacroWorld *w,uint32_t before_surface) {
    publish_change(w,w->delta_version,before_surface,(QlcgSurfaceBounds){.appearance_only=true});
}

int qlcg_world_surface_changes(const MacroWorld *w, uint32_t delta, uint32_t surface,
                               QlcgSurfaceBounds out[QLCG_SURFACE_DIRTY_CAP]) {
    if (delta == w->delta_version && surface == w->surface_revision) return 0;
    const QlcgSurfaceJournal *journal = &w->surface_changes;
    uint32_t next_delta = w->delta_version, next_surface = w->surface_revision;
    unsigned index = journal->next;
    for (unsigned i = 0; i < journal->count; i++) {
        index = (index + QLCG_SURFACE_DIRTY_CAP - 1) % QLCG_SURFACE_DIRTY_CAP;
        const QlcgSurfaceChange *change = &journal->changes[index];
        if (change->delta != next_delta || change->surface != next_surface) break;
        out[i] = change->bounds;
        next_delta = change->before_delta;
        next_surface = change->before_surface;
        if (next_delta == delta && next_surface == surface) return (int)i + 1;
    }
    return -1;
}

bool qlcg_surface_bounds_overlap(const MacroWorld *w, const QlcgSurfaceBounds *b,
                                 double x0, double z0, double x1, double z1) {
    if (b->appearance_only || z1 < b->z0 || z0 > b->z1) return false;
    if (!b->wrap_x || qlcg_world_flat(w) || w->circumference <= 0) return x1 >= b->x0 && x0 <= b->x1;
    double period = w->circumference;
    return ceil((x0 - b->x1) / period) <= floor((x1 - b->x0) / period);
}
