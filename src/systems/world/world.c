/* planet */
#include "core/dev_tools.h"
#include "planet_macro.h"
#include "surface_lakes.h"
#include "surface_islands.h"
#include "world_internal.h"
#include "world_infinite.h"
#include "surface_rivers.h"
#include "source_rivers.h"
#include "river_archive.h"
#include "surface_detail.h"
#include "surface_internal.h"
#include "ice_runtime.h"
#include "astronomy_events.h"
#include "precip.h"
#include "gen/gen_height.h"
#include "gen/gen_mountain.h"
#include "gen/gen_recipes.h"
#include "gen/gen_islands.h"
#include "gen/gen_valleys.h"
#include "gen/gen_biomes.h"
#include "gen/gen_validate.h"
#include "gen/gen_river.h"
#include "../../core/math_util.h"
#include "platform/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

MacroWorld *qlcg_world_generate(const WorldCreatorOptions *opt) {
    return qlcg_world_generate_with_rivers(opt,NULL);
}
MacroWorld *qlcg_world_generate_with_rivers(const WorldCreatorOptions *opt,struct RiverArchive *archive) {
    double t0 = qlcg_now_ms();
    MacroWorld *w = (MacroWorld *)calloc(1, sizeof(MacroWorld));
    if(!w) { MacroWorld empty={0}; empty.river_archive=archive; qlcg_river_archive_free(&empty); return NULL; }
    w->river_archive=archive;
    w->river_archive_preloaded=archive!=NULL;
    w->opt = *opt;
    qlcg_infinite_options(&w->opt);
    if (!isfinite(w->opt.axial_tilt_deg)) w->opt.axial_tilt_deg = 23.5f;
    w->opt.axial_tilt_deg = qlcg_clampf(w->opt.axial_tilt_deg, 0.0f, 60.0f);
    qlcg_sync_creator_dimensions(&w->opt);

    /* 规模 64..1024 列、4 的倍数，不降档 */
    int cols = w->opt.columns;
    if (cols < 64) cols = 64;
    if (cols > 1024) cols = 1024;
    if (cols % 4) cols -= cols % 4;
    w->columns        = cols;
    w->rows           = qlcg_world_flat(w) ? cols : cols / 2;
    w->cell_count     = w->columns * w->rows;
    w->equator_chunks = w->opt.equator_chunks;
    w->circumference  = (float)w->equator_chunks * (float)CHUNK_SIZE;
    w->radius         = w->circumference / QLCG_TAU;
    /* 无量纲高度倍率 */
    w->height_scale   = qlcg_height_scale_for(&w->opt);

    int N = w->cell_count;
    w->plate_id       = (uint16_t *)calloc((size_t)N, sizeof(uint16_t));
    w->ridge_push_e   = (float *)calloc((size_t)N, sizeof(float));
    w->ridge_push_n   = (float *)calloc((size_t)N, sizeof(float));
    w->hydro_friction = (float *)calloc((size_t)N, sizeof(float));
    w->biome          = (uint8_t *)calloc((size_t)N, sizeof(uint8_t));
    w->basin          = (float *)calloc((size_t)N, sizeof(float));
    w->boundary       = (uint8_t *)calloc((size_t)N, sizeof(uint8_t));
    w->points         = (float *)calloc((size_t)N * 3, sizeof(float));
    w->areas          = (float *)calloc((size_t)N, sizeof(float));
    w->neighbors      = (int32_t *)calloc((size_t)N * 8, sizeof(int32_t));
    w->lengths        = (float *)calloc((size_t)N * 8, sizeof(float));

    w->uplift         = (float *)calloc((size_t)N, sizeof(float));
    w->rift           = (float *)calloc((size_t)N, sizeof(float));
    w->shear          = (float *)calloc((size_t)N, sizeof(float));
    w->height         = (float *)calloc((size_t)N, sizeof(float));
    w->spill          = (float *)calloc((size_t)N, sizeof(float));
    w->level          = (float *)calloc((size_t)N, sizeof(float));
    w->flow           = (double *)calloc((size_t)N, sizeof(double));
    w->catchment      = (double *)calloc((size_t)N, sizeof(double));
    w->lake_id        = (int32_t *)malloc((size_t)N * sizeof(int32_t));
    w->river_width    = (float *)calloc((size_t)N, sizeof(float));
    w->main_river_width = (float *)calloc((size_t)N, sizeof(float));
    w->river_level    = (float *)calloc((size_t)N,sizeof(float));
    w->receiver       = (int32_t *)calloc((size_t)N, sizeof(int32_t));
    w->rank           = (uint32_t *)calloc((size_t)N, sizeof(uint32_t));

    w->temperature    = (float *)calloc((size_t)N, sizeof(float));
    w->rainfall       = (float *)calloc((size_t)N, sizeof(float));
    w->evaporation    = (float *)calloc((size_t)N, sizeof(float));
    w->moisture       = (float *)calloc((size_t)N, sizeof(float));
    w->desert_factor  = (float *)calloc((size_t)N, sizeof(float));
    w->forest_bonus   = (float *)calloc((size_t)N, sizeof(float));

    w->water          = (uint8_t *)calloc((size_t)N, sizeof(uint8_t));
    w->saline         = (uint8_t *)calloc((size_t)N, sizeof(uint8_t));
    w->waterfall_flag = (uint8_t *)calloc((size_t)N, sizeof(uint8_t));
    w->wetland_flag   = (uint8_t *)calloc((size_t)N, sizeof(uint8_t));
    w->landform       = (uint8_t *)calloc((size_t)N, sizeof(uint8_t));
    w->chunk_type     = (int16_t *)calloc((size_t)N, sizeof(int16_t));
    w->base_rgb       = (uint8_t *)calloc((size_t)N * 3, sizeof(uint8_t));
    w->sat_rgb        = (uint8_t *)calloc((size_t)N * 3, sizeof(uint8_t));
    w->shade          = (float *)calloc((size_t)N, sizeof(float));

    w->claim_faction  = (int16_t *)calloc((size_t)N, sizeof(int16_t));
    w->claim_type     = (uint8_t *)calloc((size_t)N, sizeof(uint8_t));

    w->river_cap      = MAX_RIVERS;
    w->rivers         = (RiverSegment *)calloc((size_t)w->river_cap, sizeof(RiverSegment));

    build_sphere_grid(w);
    build_tectonics(w);
    /* 山脊在抬升海平面之前叠加（对齐 base 顺序） */
    if(!qlcg_world_flat(w)) {
        qlcg_build_mountain_ridges(w);
        qlcg_apply_recipes(w);
    }
    calibrate_sea_level(w);
    /* 取岛与河谷在海平面后、气候前（对齐 base） */
    if(!qlcg_world_flat(w)) qlcg_raise_macro_islands(w);
    qlcg_macro_valleys(w);
    solve_climate_and_hydrology(w);
    int mild=-1,warm=-1,dry[2]={-1,-1};
    /* 水文之后同步放大高度与湖面 */
    for (int i = 0; i < N; i++) {
        float old = w->height[i];
        if(w->water[i]==0 || w->water[i]==3) w->generated_land_cells++;
        w->height[i] = old > 0 ? old * w->height_scale : fmaxf(-3000, old * w->height_scale);
        float climate_weight=qlcg_world_flat(w)?qlcg_infinite_climate_weight(i%cols,i/cols,cols,w->rows):1;
        w->temperature[i] -= fmaxf(0, w->height[i] - old) * 0.0065f*climate_weight;
        if (qlcg_world_border(w,i)) w->temperature[i]=15.0f;
        if (qlcg_world_flat(w) && !w->opt.plane_x && !w->opt.plane_z && !w->water[i]) {
            if(climate_weight>0) {
                if(dry[0]<0 || w->height[i]<w->height[dry[0]]) { dry[1]=dry[0]; dry[0]=i; }
                else if(dry[1]<0 || w->height[i]<w->height[dry[1]]) dry[1]=i;
            }
            if (w->temperature[i]>=8 && w->temperature[i]<18) mild=i;
            if (w->temperature[i]>=18 && w->temperature[i]<24) warm=i;
        }
        w->spill[i] *= w->height_scale;
        if (w->river_level && isfinite(w->river_level[i])) w->river_level[i] *= w->height_scale;
        if (w->water[i] == 2) w->level[i] *= w->height_scale;
    }
    for (int i = 0; i < w->lake_count; i++) {
        w->lakes[i].level *= w->height_scale;
        w->lakes[i].max_depth *= w->height_scale;
    }
    if (qlcg_world_flat(w) && !w->opt.plane_x && !w->opt.plane_z) {
        if(mild<0) {
            mild=dry[0]==warm?dry[1]:dry[0];
            if(mild>=0) w->temperature[mild]=12.0f;
        }
        if(warm<0) {
            warm=dry[0]==mild?dry[1]:dry[0];
            if(warm>=0) w->temperature[warm]=21.0f;
        }
    }
    qlcg_build_biomes(w);

    /* 生成自检：仅开发版校验并回报 */
    QLCG_DEV_ONLY({
        WorldAudit audit;
        qlcg_validate_world(w, &audit);
        w->audit_ok = qlcg_audit_ok(&audit) ? 1 : 0;
        if (!w->audit_ok) {
            QLCG_DEV_TRACE("[world] audit: nan=%d rank=%d uphill=%d plate=%d lake=%d balance=%d conservation=%.3g area=%.3g\n",
                    audit.nan_count, audit.bad_rank, audit.uphill, audit.bad_plate,
                    audit.bad_lake, audit.bad_balance, audit.conservation, audit.area_error);
        }
    })
    /* 水文定稿后才允许建岛岸目录：之前不介入采样 */
    w->islands_ready = true;
    build_initial_civilizations(w);
    if (w->opt.play_mode == PLAY_MODE_SURVIVAL && (!qlcg_world_flat(w) || (!w->opt.plane_x && !w->opt.plane_z))) {
        int r = QLCG_SPAWN_REVEAL_RADIUS;
        for (int dz = -r; dz <= r; ++dz) {
            int cz = w->spawn_cz + dz;
            if (cz < -w->equator_chunks / (qlcg_world_flat(w)?2:4) || cz >= w->equator_chunks / (qlcg_world_flat(w)?2:4)) continue;
            for (int dx = -r; dx <= r; ++dx) {
                if (!qlcg_world_reveal_chunk(w, w->spawn_cx + dx, cz)) {
                    qlcg_world_free(w);
                    return NULL;
                }
            }
        }
    }

    w->auto_events = true;
    w->event_cooldown = 18.0f;
    if(w->spawn_cell<0)
        snprintf(w->last_event_msg,sizeof(w->last_event_msg),tr("world.infinite.ready"),w->opt.plane_x,w->opt.plane_z);
    else snprintf(w->last_event_msg, sizeof(w->last_event_msg),
             tr("world.ready"),
             w->opt.seed, qlcg_world_preset_name(w->opt.preset), w->spawn_cx, w->spawn_cz);

    w->ice_state = qlcg_ice_world_create();
    if(w->opt.astronomy_enabled) w->astra_events=qlcg_astra_events_create(w->opt.seed);
    if(w->astra_events) w->astra_events->flat=qlcg_world_flat(w);
    w->ice_forced_season = -1;
    w->precip = qlcg_precip_create(w);
    w->gen_ms = qlcg_now_ms() - t0;
    qlcg_world_rebuild_atlas_fields(w);
    w->decor_ready=true;
    return w;
}

void qlcg_world_free(MacroWorld *w) {
    if (!w) return;
    qlcg_infinite_free_others(w);
    qlcg_precip_free(w->precip);
    w->precip = NULL;
    free(w->astra_events);
    qlcg_ice_world_free(w->ice_state);
    w->ice_state = NULL;
    qlcg_lakes_free(w);
    qlcg_islands_free(w);
    qlcg_source_rivers_free(w);
    qlcg_oxbow_free(w);
    qlcg_river_surface_free(w);
    qlcg_river_archive_free(w);
    qlcg_terrain_detail_free(w);
    free(w->plate_id); free(w->ridge_push_e); free(w->ridge_push_n);
    free(w->hydro_friction); free(w->biome); free(w->basin); free(w->boundary); free(w->points); free(w->areas);
    free(w->neighbors); free(w->lengths); free(w->uplift); free(w->rift);
    free(w->shear); free(w->height); free(w->spill); free(w->level);
    free(w->flow); free(w->catchment); free(w->lake_id); free(w->river_width);
    free(w->main_river_width); free(w->receiver);
    free(w->river_level);
    free(w->rank); free(w->temperature); free(w->rainfall); free(w->evaporation);
    free(w->moisture); free(w->season_amp); free(w->monsoon_amp);
    free(w->desert_factor); free(w->forest_bonus);
    free(w->water); free(w->saline); free(w->waterfall_flag); free(w->wetland_flag);
    free(w->landform); free(w->chunk_type); free(w->base_rgb); free(w->sat_rgb);
    free(w->shade); free(w->claim_faction); free(w->claim_type); free(w->explored_chunks);
    free(w->rivers); free(w->lakes); free(w->macro_islands);
    free(w);
}

int qlcg_world_cell_at_wxz(const MacroWorld *w, float wx, float wz) {
    return qlcg_macro_cell_at(w,wx,wz);
}

void qlcg_world_cell_center(const MacroWorld *w, int cell, float *wx, float *wz) {
    int col = cell % w->columns;
    int row = cell / w->columns;
    *wx = (((float)col + 0.5f) / (float)w->columns - 0.5f) * w->circumference;
    *wz = (((float)row + 0.5f) / (float)w->rows - 0.5f) * qlcg_world_depth(w);
}

/* 九点采样分类 */
void qlcg_world_inspect_chunk(const MacroWorld *w, int cx, int cz, ChunkInspectInfo *out) {
    memset(out, 0, sizeof(*out));
    out->cx = cx;
    out->cz = cz;
    float wx = ((float)cx + 0.5f) * (float)CHUNK_SIZE;
    float wz = ((float)cz + 0.5f) * (float)CHUNK_SIZE;
    int cell = qlcg_world_cell_at_wxz(w, wx, wz);
    out->cell_id   = cell;
    out->macro_col = cell % w->columns;
    out->macro_row = cell / w->columns;

    /* 区块内 3x3 九点：间距半个区块 */
    float hs[9], ws[9];
    SurfaceWater water={0};
    bool is_fall=false;
    const float step = (float)CHUNK_SIZE * 0.5f;
    float base_x = (float)cx * (float)CHUNK_SIZE;
    float base_z = (float)cz * (float)CHUNK_SIZE;
    for (int oz = 0; oz < 3; oz++) {
        for (int ox = 0; ox < 3; ox++) {
            float h, wl;
            SurfaceWater sample;
            qlcg_sample_surface_water(w, base_x + (float)ox * step, base_z + (float)oz * step, 0,
                                      &h, &wl, NULL, NULL, NULL, NULL, NULL, NULL, &sample, NULL);
            is_fall=is_fall || sample.kind==WATER_KIND_FALL;
            if(ox==1 && oz==1) water=sample;
            hs[oz * 3 + ox] = h;
            ws[oz * 3 + ox] = wl;
        }
    }
    float center_h = hs[4];
    float relief,slope,relative;
    qlcg_chunk_shape(hs,&relief,&slope,&relative);

    float aridity = w->rainfall[cell] / fmaxf(150.0f, w->evaporation[cell]);
    float temp = w->temperature[cell];
    /* 区块水体以点级采样为准 */
    bool is_riv = water.kind == WATER_KIND_RIVER || water.kind == WATER_KIND_FALL;
    bool is_wat = water.kind == WATER_KIND_SEA || water.kind == WATER_KIND_LAKE || water.kind == WATER_KIND_STATIC;

    out->type_id   = qlcg_classify_chunk(is_wat, is_riv, is_fall,
                                         w->wetland_flag[cell] != 0 && water.source_effects.salt != 255,
                                         water.source_effects.salt == 255 || (!water.fresh && w->saline[cell] != 0),
                                         center_h, temp, aridity, relief, slope, relative,
                                         w->claim_type[cell]);
    out->type_def  = qlcg_get_chunk_type(out->type_id);
    out->elevation = center_h;
    out->water_level = ws[4];
    out->temperature = temp;
    out->moisture    = w->moisture[cell];
    out->lon_deg = (wx / w->circumference) * 360.0f;
    out->lat_deg = -(wz / qlcg_world_depth(w)) * 180.0f;
    out->discharge   = water.discharge;
    out->river_width = water.width_m * 0.5f;
    out->river_depth = water.depth;
    out->landform    = (SpecialLandform)w->landform[cell];

    if (is_riv && hypotf(water.direction_x,water.direction_z)>.1f) {
        float ang = atan2f(-water.direction_z,water.direction_x);
        int dir8 = ((int)roundf(ang / (QLCG_PI * 0.25f)) + 8) % 8;
        out->flow_dir_name = FLOW_DIR_NAMES[dir8];
    } else {
        out->flow_dir_name = "无流动 (静水或高地)";
    }

    int fid = w->claim_faction[cell];
    if (fid >= 0 && fid < w->faction_count && w->factions[fid].active) {
        out->territory = &w->factions[fid];
    }
}

static uint64_t explored_chunk_key(const MacroWorld *w, int cx, int cz) {
    int width = w->equator_chunks;
    if (qlcg_world_flat(w)) return ((uint64_t)(uint32_t)cx<<32)|(uint32_t)cz;
    int half_height = width / 4;
    if (cz < -half_height || cz >= half_height) return UINT64_MAX;
    int col = wrap_col(cx % width + width / 2, width);
    return (uint64_t)(cz + half_height) * (uint64_t)width + (uint64_t)col;
}

static size_t explored_lower_bound(const MacroWorld *w, uint64_t key) {
    size_t lo = 0, hi = w->explored_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (w->explored_chunks[mid] < key) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

bool qlcg_world_chunk_explored(const MacroWorld *w, int cx, int cz) {
    if (w->opt.play_mode != PLAY_MODE_SURVIVAL) return true;
    uint64_t key = explored_chunk_key(w, cx, cz);
    size_t at = explored_lower_bound(w, key);
    return at < w->explored_count && w->explored_chunks[at] == key;
}

bool qlcg_world_reveal_chunk(MacroWorld *w, int cx, int cz) {
    if (w->opt.play_mode != PLAY_MODE_SURVIVAL) return true;
    uint64_t key = explored_chunk_key(w, cx, cz);
    if (!qlcg_world_flat(w) && key == UINT64_MAX) return false;
    size_t at = explored_lower_bound(w, key);
    if (at < w->explored_count && w->explored_chunks[at] == key) return true;
    if (w->explored_count == w->explored_capacity) {
        size_t cap = w->explored_capacity ? w->explored_capacity * 2 : 128;
        if (cap > SIZE_MAX / sizeof(*w->explored_chunks)) return false;
        uint64_t *grown = realloc(w->explored_chunks, cap * sizeof(*grown));
        if (!grown) return false;
        w->explored_chunks = grown;
        w->explored_capacity = cap;
    }
    memmove(w->explored_chunks + at + 1, w->explored_chunks + at,
            (w->explored_count - at) * sizeof(*w->explored_chunks));
    w->explored_chunks[at] = key;
    ++w->explored_count;
    return true;
}

/* 人工世界：按日历推进自然扩张、自然进化与 */
