#include "surface_worker.h"
#include "hydrology_archive.h"
#include "hydrology_river.h"
#include "hydrology_sources.h"
#include "surface_detail.h"
#include "surface_islands.h"
#include <stdlib.h>

struct QlcgSurfaceWorker {
    const MacroWorld *owner;
    MacroWorld world;
};

void qlcg_surface_worker_free(QlcgSurfaceWorker *worker) {
    if(!worker) return;
    MacroWorld *w=&worker->world;
    qlcg_terrain_detail_free(w);
    qlcg_river_surface_free(w);
    qlcg_source_rivers_free(w);
    qlcg_islands_free(w);
    free(worker);
}

const MacroWorld *qlcg_surface_worker_begin(QlcgSurfaceWorker **slot,const MacroWorld *source) {
    if(qlcg_world_flat(source)&&source->planes) return NULL;
    QlcgSurfaceWorker *worker=*slot;
    if(worker&&(worker->owner!=source||worker->world.surface_revision!=source->surface_revision||
        worker->world.delta_version!=source->delta_version)) {
        qlcg_surface_worker_free(worker);worker=NULL;
    }
    if(!worker) worker=calloc(1,sizeof(*worker));
    *slot=worker;
    if(!worker) return NULL;
    MacroWorld *w=&worker->world;
    struct TerrainDetailCache *detail=w->terrain_detail;
    struct RiverSurfaceCache *rivers=w->river_surface;
    struct SourceRiverCache *sources=w->source_rivers;
    struct IslandCatalog *islands=w->island_catalog;
    /* 四种惰性缓存归作业 */
    *w=*source;
    w->terrain_detail=detail;w->river_surface=rivers;w->source_rivers=sources;w->island_catalog=islands;
    worker->owner=source;
    qlcg_river_archive_readonly_begin(w);
    return w;
}

bool qlcg_surface_worker_end(QlcgSurfaceWorker **slot) {
    if(qlcg_river_archive_readonly_end()) return true;
    qlcg_surface_worker_free(*slot);
    *slot=NULL;
    return false;
}
