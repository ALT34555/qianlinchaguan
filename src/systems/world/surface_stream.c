/* 连续世界：圆形视距与区块生命周期 */
#include "surface_stream.h"
#include "../../core/math_util.h"
#include <stdlib.h>
#include <math.h>
#include <float.h>

int qlcg_stream_slot(int cx, int cz) {
    int x = (cx % QLCG_STREAM_SIDE + QLCG_STREAM_SIDE) % QLCG_STREAM_SIDE;
    int z = (cz % QLCG_STREAM_SIDE + QLCG_STREAM_SIDE) % QLCG_STREAM_SIDE;
    return z * QLCG_STREAM_SIDE + x;
}

static int offset_cmp(const void *a, const void *b) {
    const int *p = a, *q = b;
    int d = p[0]*p[0] + p[1]*p[1] - q[0]*q[0] - q[1]*q[1];
    if (d) return d;
    return p[1] != q[1] ? p[1] - q[1] : p[0] - q[0];
}

static void release_slot(RegionStream *st, int slot) {
    RegionChunkSlot *c = &st->slots[slot];
    if (!c->data) return;
    qlcg_region_free(c->data);
    c->data = NULL;
    int last = st->active[--st->count];
    st->active[c->active_index] = last;
    st->slots[last].active_index = c->active_index;
    st->unloaded_total++;
    st->revision++;
}

static bool ensure_chunk(RegionStream *st, int cx, int cz) {
    int slot = qlcg_stream_slot(cx, cz);
    RegionChunkSlot *c = &st->slots[slot];
    if (c->data && c->data->center_cx == cx && c->data->center_cz == cz) return true;
    release_slot(st, slot);
    c->data = qlcg_chunk_sample(st->world, cx, cz, st->season, &st->settings);
    if (!c->data) return false;
    c->serial = ++st->next_serial;
    c->active_index = st->count;
    st->active[st->count++] = slot;
    st->loaded_total++;
    st->revision++;
    return true;
}

RegionData *qlcg_region_chunk_at(const RegionData *reg, float lx, float lz,
                                float *chunk_lx, float *chunk_lz) {
    if (!reg) return NULL;
    RegionData *chunk = (RegionData *)reg;
    if (reg->stream) {
        int cx = reg->center_cx + (int)floorf((lx+CHUNK_SIZE*0.5f)/CHUNK_SIZE);
        int cz = reg->center_cz + (int)floorf((lz+CHUNK_SIZE*0.5f)/CHUNK_SIZE);
        chunk = reg->stream->slots[qlcg_stream_slot(cx, cz)].data;
        if (!chunk || chunk->center_cx != cx || chunk->center_cz != cz) return NULL;
    }
    if (chunk_lx) *chunk_lx = lx + (reg->center_cx-chunk->center_cx)*CHUNK_SIZE;
    if (chunk_lz) *chunk_lz = lz + (reg->center_cz-chunk->center_cz)*CHUNK_SIZE;
    return chunk;
}

void qlcg_region_stream_refresh(RegionData *reg, float wx0, float wz0,
                                float wx1, float wz1, int season, const GameSettings *s) {
    if (!reg || !reg->stream) return;
    RegionStream *st = reg->stream;
    st->season = season;
    st->settings = *s;
    for (int i = 0; i < st->count; i++) {
        RegionData *c = st->slots[st->active[i]].data;
        float half = (c->width - 1) * c->step * 0.5f;
        if (c->center_x + half < wx0 || c->center_x - half > wx1 ||
            c->center_z + half < wz0 || c->center_z - half > wz1) continue;
        qlcg_chunk_resample(c, st->world, season, s);
    }
    st->delta_revision = st->world->delta_version;
    st->surface_revision = st->world->surface_revision;
    st->territory_revision = st->world->territory_revision;
    st->revision++;
}

void qlcg_region_stream_update(RegionData *reg, float lx, float lz,
                               int season, const GameSettings *s, double budget_ms) {
    if (!reg || !reg->stream) return;
    double t0 = qlcg_time_ms();
    RegionStream *st = reg->stream;
    int cx = reg->center_cx + (int)floorf((lx+CHUNK_SIZE*0.5f)/CHUNK_SIZE);
    int cz = reg->center_cz + (int)floorf((lz+CHUNK_SIZE*0.5f)/CHUNK_SIZE);
    int rd = qlcg_clampi(s->render_distance, MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE);
    bool radius_changed = rd != st->radius;
    bool moved = cx != st->center_cx || cz != st->center_cz;
    bool appearance_changed = season != st->season ||
        s->dense_vegetation != st->settings.dense_vegetation ||
        s->vegetation_form != st->settings.vegetation_form;
    if (radius_changed) {
        st->radius = rd;
        st->offset_count = 0;
        for (int z = -rd; z <= rd; z++) for (int x = -rd; x <= rd; x++) {
            if (x*x + z*z > (rd + 0.5f)*(rd + 0.5f)) continue;
            st->offsets[st->offset_count][0] = x;
            st->offsets[st->offset_count++][1] = z;
        }
        qsort(st->offsets, st->offset_count, sizeof(st->offsets[0]), offset_cmp);
    }
    st->season = season;
    st->settings = *s;
    if (radius_changed || moved) {
        st->center_cx = cx;
        st->center_cz = cz;
        st->cursor = 0;
        float limit = rd + (radius_changed ? 0.5f : 1.5f);
        for (int i = st->count - 1; i >= 0; i--) {
            RegionData *c = st->slots[st->active[i]].data;
            float dx = (float)c->center_cx - cx, dz = (float)c->center_cz - cz;
            if (dx*dx + dz*dz > limit*limit) release_slot(st, st->active[i]);
        }
        st->revision++;
    }
    if (st->delta_revision != st->world->delta_version || st->surface_revision != st->world->surface_revision) {
        qlcg_region_stream_refresh(reg, -FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX, season, s);
    } else if (appearance_changed || st->territory_revision != st->world->territory_revision) {
        qlcg_region_recolor(reg, st->world, season);
        qlcg_region_scatter_props(reg, st->world, season, s);
        st->territory_revision = st->world->territory_revision;
    }
    /* 脚下与相邻区块优先保证 */
    for (int z = -1; z <= 1; z++) for (int x = -1; x <= 1; x++) ensure_chunk(st, cx+x, cz+z);
    int built = 0;
    while (st->cursor < st->offset_count) {
        int x = cx + st->offsets[st->cursor][0], z = cz + st->offsets[st->cursor][1];
        RegionData *c = st->slots[qlcg_stream_slot(x, z)].data;
        if (c && c->center_cx == x && c->center_cz == z) { st->cursor++; continue; }
        if (built >= 8 || (built > 0 && qlcg_time_ms() - t0 >= budget_ms)) break;
        if (!ensure_chunk(st, x, z)) break;
        built++;
        st->cursor++;
    }
    RegionData *here = st->slots[qlcg_stream_slot(cx, cz)].data;
    if (here) {
        reg->water_form = here->water_form;
        reg->water_freeze = here->water_freeze;
        reg->site_cell = here->site_cell;
        reg->site_chunk_type = here->site_chunk_type;
        reg->site_height = here->site_height;
    }
    st->update_ms = qlcg_time_ms() - t0;
}

RegionData *qlcg_region_sample(const MacroWorld *w, int cx, int cz,
                               int season, const GameSettings *s) {
    RegionData *reg = calloc(1, sizeof(*reg));
    RegionStream *st = calloc(1, sizeof(*st));
    if (!reg || !st) { free(reg); free(st); return NULL; }
    reg->stream = st;
    reg->center_cx = cx; reg->center_cz = cz;
    reg->center_x = (cx + 0.5f) * CHUNK_SIZE;
    reg->center_z = (cz + 0.5f) * CHUNK_SIZE;
    st->world = w;
    st->settings = *s; st->season = season;
    st->center_cx = cx; st->center_cz = cz;
    st->delta_revision = w->delta_version;
    st->surface_revision = w->surface_revision;
    st->territory_revision = w->territory_revision;
    qlcg_region_stream_update(reg, 0, 0, season, s, 2.0);
    if (!qlcg_region_chunk_at(reg, 0, 0, NULL, NULL)) { qlcg_region_free(reg); return NULL; }
    reg->sample_ms = st->update_ms;
    return reg;
}

void qlcg_region_stream_free(RegionStream *st) {
    if (!st) return;
    while (st->count > 0) release_slot(st, st->active[st->count-1]);
    free(st);
}
