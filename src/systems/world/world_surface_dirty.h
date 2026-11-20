#ifndef QLCG_WORLD_SURFACE_DIRTY_H
#define QLCG_WORLD_SURFACE_DIRTY_H

#include <stdbool.h>
#include <stdint.h>

struct MacroWorld;

enum { QLCG_SURFACE_DIRTY_CAP = 32 };

typedef struct {
    double x0, z0, x1, z1;
    bool wrap_x, appearance_only;
} QlcgSurfaceBounds;

typedef struct {
    QlcgSurfaceBounds bounds;
    uint32_t before_delta, before_surface, delta, surface;
} QlcgSurfaceChange;

typedef struct {
    QlcgSurfaceChange changes[QLCG_SURFACE_DIRTY_CAP];
    unsigned count, next;
} QlcgSurfaceJournal;

void qlcg_world_surface_dirty(struct MacroWorld *w, uint32_t before_delta, uint32_t before_surface,
                             double x0, double z0, double x1, double z1, bool wrap_x);
void qlcg_world_surface_appearance(struct MacroWorld *w,uint32_t before_surface);
int qlcg_world_surface_changes(const struct MacroWorld *w, uint32_t delta, uint32_t surface,
                               QlcgSurfaceBounds out[QLCG_SURFACE_DIRTY_CAP]);
bool qlcg_surface_bounds_overlap(const struct MacroWorld *w, const QlcgSurfaceBounds *bounds,
                                 double x0, double z0, double x1, double z1);

#endif
