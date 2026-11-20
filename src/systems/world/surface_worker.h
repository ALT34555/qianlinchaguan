#ifndef QLCG_SURFACE_WORKER_H
#define QLCG_SURFACE_WORKER_H

#include "planet_macro.h"

typedef struct QlcgSurfaceWorker QlcgSurfaceWorker;

const MacroWorld *qlcg_surface_worker_begin(QlcgSurfaceWorker **worker,const MacroWorld *source);
bool qlcg_surface_worker_end(QlcgSurfaceWorker **worker);
void qlcg_surface_worker_free(QlcgSurfaceWorker *worker);

#endif
