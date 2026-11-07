/* ice_runtime：缓存刷新+孔洞编辑+再冻 */
#include "ice_runtime.h"
#include "core/dev_tools.h"
#include "surface_internal.h"
#include "../../core/math_util.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ICE_EDIT_EMPTY INT64_MIN
#define ICE_EDIT_TOMB  INT64_MAX

/* 世界级稀疏编辑表 */

static uint64_t mix64(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static uint32_t edit_hash(int64_t gx, int64_t gz) {
    return (uint32_t)mix64((uint64_t)gx * 73856093ULL ^ (uint64_t)gz * 19349663ULL);
}

static IceEdit *edit_slot(IceEdit *tab, uint32_t cap, int64_t gx, int64_t gz, bool for_insert) {
    uint32_t mask = cap - 1u;
    uint32_t i = edit_hash(gx, gz) & mask;
    IceEdit *tomb = NULL;
    for (uint32_t probe = 0; probe <= mask; probe++) {
        IceEdit *e = &tab[i];
        if (e->gx == ICE_EDIT_EMPTY) return for_insert ? (tomb ? tomb : e) : NULL;
        if (e->gx == ICE_EDIT_TOMB) { if (!tomb) tomb = e; }
        else if (e->gx == gx && e->gz == gz) return e;
        i = (i + 1u) & mask;
    }
    return for_insert ? tomb : NULL;
}

static bool edit_grow(IceWorldState *st) {
    uint32_t ncap = st->cap ? st->cap * 2u : 1024u;
    if (ncap > ICE_EDIT_CAP_MAX) { st->overflow++; return false; }
    IceEdit *nt = (IceEdit *)malloc((size_t)ncap * sizeof(IceEdit));
    if (!nt) { st->overflow++; return false; }
    for (uint32_t i = 0; i < ncap; i++) {
        nt[i].gx = ICE_EDIT_EMPTY;
        nt[i].gz = 0;
        nt[i].broken_at = 0.0;
        nt[i].damage = 0.0f;
        nt[i].flags = 0;
        nt[i].stamp = 0;
    }
    for (uint32_t i = 0; i < st->cap; i++) {
        IceEdit *src = &st->edits[i];
        if (src->gx == ICE_EDIT_EMPTY || src->gx == ICE_EDIT_TOMB) continue;
        IceEdit *dst = edit_slot(nt, ncap, src->gx, src->gz, true);
        if (dst) *dst = *src;
    }
    free(st->edits);
    st->edits = nt;
    st->cap = ncap;
    return true;
}

static IceEdit *edit_get(IceWorldState *st, int64_t gx, int64_t gz, bool create) {
    if (!st) return NULL;
    if (!st->edits) {
        if (!create) return NULL;
        if (!edit_grow(st)) return NULL;
    } else if (create && (st->count + 1u) * 2u >= st->cap) {
        if (st->count < st->soft_limit && !edit_grow(st)) {
            /* 扩容失败仍可覆盖已有键 */
        }
    }
    if (!st->edits) return NULL;
    IceEdit *e = edit_slot(st->edits, st->cap, gx, gz, create);
    if (!e) return NULL;
    if (e->gx != gx || e->gz != gz) {
        if (!create) return NULL;
        if (e->gx == ICE_EDIT_EMPTY) st->count++;
        e->gx = gx;
        e->gz = gz;
        e->broken_at = 0.0;
        e->damage = 0.0f;
        e->flags = 0;
        e->stamp = 0;
    }
    return e;
}

static void edit_drop(IceWorldState *st, int64_t gx, int64_t gz) {
    if (!st || !st->edits) return;
    IceEdit *e = edit_slot(st->edits, st->cap, gx, gz, false);
    if (!e) return;
    e->gx = ICE_EDIT_TOMB;
    if (st->count) st->count--;
    st->edit_revision++;
}

IceWorldState *qlcg_ice_world_create(void) {
    IceWorldState *st = (IceWorldState *)calloc(1, sizeof(*st));
    if (!st) return NULL;
    st->soft_limit = ICE_EDIT_SOFT;
    return st;
}

void qlcg_ice_world_free(IceWorldState *st) {
    if (!st) return;
    free(st->edits);
    free(st);
}

IceChunkData *qlcg_ice_chunk_create(void) {
    IceChunkData *c = (IceChunkData *)calloc(1, sizeof(*c));
    if (c) c->refreshed_day = -1e30;
    return c;
}

void qlcg_ice_chunk_free(IceChunkData *c) { free(c); }

void qlcg_ice_cell_key(const MacroWorld *w, float wx, float wz, int64_t *gx, int64_t *gz) {
    double cx = floor((double)wx / (double)ICE_CELL_M);
    double cz = floor((double)wz / (double)ICE_CELL_M);
    if (w && w->circumference > 1.0f) {
        double n = floor((double)w->circumference / (double)ICE_CELL_M + .5);
        if (n > 1.0) {
            double r = fmod(cx, n);
            if (r < 0.0) r += n;
            cx = r;
        }
    }
    if (gx) *gx = (int64_t)cx;
    if (gz) *gz = (int64_t)cz;
}

void qlcg_ice_set_time(MacroWorld *w, const IceTime *t) {
    if (!w || !t) return;
    IceWorldState *st = w->ice_state;
    bool first = !w->ice_time_valid;
    if (st && !first && t->climate_day < st->last_climate_day - .01) {
        /* 回拨或切历法：重锚，不产生负 elapsed */
        st->epoch++;
        for (int i = 0; i < ICE_TRANS_CAP; i++) st->trans[i].used = false;
    }
    w->ice_time = *t;
    w->ice_time_valid = true;
    if (st) {
        st->last_climate_day = t->climate_day;
        w->ice_time.epoch = st->epoch;
    }
}

uint32_t qlcg_ice_epoch(const MacroWorld *w) {
    return (w && w->ice_state) ? w->ice_state->epoch : 0u;
}

void qlcg_ice_set_occupant(const MacroWorld *w, float wx, float wz, float half) {
    if (!w || !w->ice_state) return;
    IceWorldState *st = w->ice_state;
    st->occ_x = wx;
    st->occ_z = wz;
    st->occ_half = half > 0.0f ? half : 0.3f;
    st->occ_valid = true;
}

static bool occupied(const IceWorldState *st, float wx, float wz) {
    if (!st || !st->occ_valid) return false;
    float d = st->occ_half + ICE_CELL_M * 0.5f;
    return fabsf(wx - st->occ_x) <= d && fabsf(wz - st->occ_z) <= d;
}

static void set_open_bit(IceChunkData *c, int ix, int iz, bool on);
static void cell_world(const RegionData *reg, int ix, int iz, float *wx, float *wz);
static void merge_edits(const MacroWorld *w, IceChunkData *c, RegionData *reg);

void qlcg_ice_cache_site(IceChunkData *c, int vertex, const IceSite *site) {
    if (!c || !site || vertex < 0 || vertex >= ICE_VERTS) return;
    int16_t annual = (int16_t)qlcg_clampi((int)lrintf(site->annual_temp * 100.0f), -32768, 32767);
    int     depth_cm = qlcg_clampi((int)lrintf(site->depth * 100.0f), 0, 65535);
    float   u = qlcg_clampf((site->raw_speed / 2.5f), 0.0f, 1.0f);
    float   by_grade = qlcg_smoothf(.008f, .04f, site->grade);
    if (by_grade > u) u = by_grade;
    u *= 1.0f - .65f * site->lateral * site->lateral;
    c->annual_c[vertex] = annual;
    c->depth_cm[vertex] = (uint16_t)depth_cm;
    c->flow_u[vertex]   = (uint8_t)qlcg_clampi((int)lrintf(qlcg_clampf(u, 0.0f, 1.0f) * 255.0f), 0, 255);
    c->shore[vertex]    = (uint8_t)qlcg_clampi((int)lrintf(qlcg_clampf(site->shore_weight, 0.0f, 1.0f) * 255.0f), 0, 255);
    c->noise[vertex]    = (uint8_t)qlcg_clampi((int)lrintf(qlcg_clampf(site->noise, 0.0f, 1.0f) * 255.0f), 0, 255);
    c->kind[vertex]     = site->kind;
    c->wet[vertex]      = (site->depth > .02f && site->kind != ICE_KIND_DRY) ? 1 : 0;
    if (c->wet[vertex] && c->wet_count < ICE_VERTS) c->wet_count++;
}

/* 由缓存标量重建 IceSite：只用于本块顶点 */
static void site_from_cache(const MacroWorld *w, const IceChunkData *c, const RegionData *reg,
                            int vertex, float top, float bed, IceSite *site) {
    int vz = vertex / reg->width;
    float wz = reg->origin_z + (float)vz * reg->step;
    memset(site, 0, sizeof(*site));
    site->annual_temp = (float)c->annual_c[vertex] / 100.0f;
    site->latitude = (w && w->circumference > 1.0f)
                   ? -wz / (w->circumference * 0.5f) * 3.14159265f : 0.0f;
    site->depth = (float)c->depth_cm[vertex] / 100.0f;
    site->raw_speed = (float)c->flow_u[vertex] / 255.0f * 2.5f;
    site->shore_weight = (float)c->shore[vertex] / 255.0f;
    site->noise = (float)c->noise[vertex] / 255.0f;
    site->kind = c->kind[vertex];
    site->top = top;
    site->bed = bed;
}

static void apply_transition(const MacroWorld *w, float wx, float wz, double day,
                             float *dh, float *dq) {
    *dh = 0.0f;
    *dq = 0.0f;
    const IceWorldState *st = w->ice_state;
    if (!st) return;
    for (int i = 0; i < ICE_TRANS_CAP; i++) {
        const IceThermalTransition *t = &st->trans[i];
        if (!t->used || t->tau <= 0.0f) continue;
        float d = hypotf(wx - t->wx, wz - t->wz);
        if (d > 160.0f) continue;
        float k = 1.0f - d / 160.0f;
        double el = day - t->start_day;
        if (el < 0.0) el = 0.0;
        float decay = (float)exp(-el / (double)t->tau);
        *dh += t->delta_h * decay * k;
        *dq += t->delta_q * decay * k;
    }
}

void qlcg_ice_thermal_begin(const MacroWorld *w, float wx, float wz, double start_day,
                            float delta_h, float delta_q, float tau) {
    if (!w || !w->ice_state) return;
    IceWorldState *st = w->ice_state;
    for (int i = 0; i < ICE_TRANS_CAP; i++) {
        IceThermalTransition *t = &st->trans[i];
        if (!t->used) {
            memset(t, 0, sizeof(*t));
            t->used = true;
            t->wx = wx;
            t->wz = wz;
            t->start_day = start_day;
            t->delta_h = delta_h;
            t->delta_q = delta_q;
            t->tau = tau > 0.0f ? tau : 2.0f;
            return;
        }
    }
    /* 池满：替换最旧的一条，不叠加无限事件链 */
    int oldest = 0;
    for (int i = 1; i < ICE_TRANS_CAP; i++)
        if (st->trans[i].start_day < st->trans[oldest].start_day) oldest = i;
    memset(&st->trans[oldest], 0, sizeof(st->trans[oldest]));
    st->trans[oldest].used = true;
    st->trans[oldest].wx = wx;
    st->trans[oldest].wz = wz;
    st->trans[oldest].start_day = start_day;
    st->trans[oldest].delta_h = delta_h;
    st->trans[oldest].delta_q = delta_q;
    st->trans[oldest].tau = tau > 0.0f ? tau : 2.0f;
}

void qlcg_ice_thermal_sample(const MacroWorld *w, float wx, float wz, double now_day,
                             float *delta_h, float *delta_q) {
    apply_transition(w, wx, wz, now_day, delta_h, delta_q);
}

const IceEdit *qlcg_ice_edit_find(const MacroWorld *w, int64_t gx, int64_t gz) {
    if (!w || !w->ice_state || !w->ice_state->edits) return NULL;
    IceWorldState *st = w->ice_state;
    return edit_slot(st->edits, st->cap, gx, gz, false);
}

int qlcg_ice_edit_count(const MacroWorld *w) {
    return (w && w->ice_state) ? (int)w->ice_state->count : 0;
}

/* 找到世界坐标所在的已加载块与块内单元号 */
static RegionData *chunk_of(const RegionData *reg, float wx, float wz, int *ix, int *iz,
                            float *out_lx, float *out_lz) {
    if (!reg) return NULL;
    float lx = wx - reg->center_x, lz = wz - reg->center_z;
    RegionData *c = qlcg_region_chunk_at(reg, lx, lz, out_lx, out_lz);
    if (!c) return NULL;
    float half = (float)CHUNK_SIZE * 0.5f;
    int cx = qlcg_clampi((int)floorf((*out_lx + half) / ICE_CELL_M), 0, ICE_GRID_CELLS - 1);
    int cz = qlcg_clampi((int)floorf((*out_lz + half) / ICE_CELL_M), 0, ICE_GRID_CELLS - 1);
    if (ix) *ix = cx;
    if (iz) *iz = cz;
    return c;
}

void qlcg_ice_break_cells(const MacroWorld *w, RegionData *reg, const IceContact *cells, int count) {
    if (!w || !reg) return;
    double now = w->ice_time.climate_day;
    int changed = 0;
    for (int i = 0; i < count; i++) {
        const IceContact *k = &cells[i];
        int64_t gx, gz;
        qlcg_ice_cell_key(w, k->wx, k->wz, &gx, &gz);
        IceWorldState *st = w->ice_state;
        if (!st) continue;
        IceEdit *e = edit_get(st, gx, gz, true);
        if (!e) continue;
        e->flags |= ICE_EDIT_HOLE;
        e->broken_at = now;
        e->damage = 0.0f;
        st->edit_revision++;
        float lx = 0.0f, lz = 0.0f;
        int ix = 0, iz = 0;
        RegionData *c = chunk_of(reg, k->wx, k->wz, &ix, &iz, &lx, &lz);
        if (c && c->ice) {
            set_open_bit(c->ice, ix, iz, true);
            c->ice->damage[iz * ICE_GRID_CELLS + ix] = 0;
            c->ice->revision++;
            c->ice_revision = c->ice->revision;
        }
        changed = 1;
    }
    (void)changed;
}

void qlcg_ice_add_damage(const MacroWorld *w, RegionData *reg, const IceContact *cells,
                         int count, float amount) {
    if (!w || !reg || amount <= 0.0f) return;
    for (int i = 0; i < count; i++) {
        const IceContact *k = &cells[i];
        float lx = 0.0f, lz = 0.0f;
        int ix = 0, iz = 0;
        RegionData *c = chunk_of(reg, k->wx, k->wz, &ix, &iz, &lx, &lz);
        if (!c || !c->ice) continue;
        int bit = iz * ICE_GRID_CELLS + ix;
        if (qlcg_ice_cell_open(c->ice, ix, iz)) continue;
        float dmg = qlcg_ice_cell_damage(c->ice, ix, iz) + amount;
        if (dmg > 1.0f) dmg = 1.0f;
        c->ice->damage[bit] = dmg;
        c->ice->revision++;
        c->ice_revision = c->ice->revision;
        int64_t gx, gz;
        qlcg_ice_cell_key(w, k->wx, k->wz, &gx, &gz);
        IceWorldState *st = w->ice_state;
        if (st) {
            IceEdit *e = edit_get(st, gx, gz, true);
            if (e) { e->damage = dmg; e->flags |= ICE_EDIT_DAMAGE; st->edit_revision++; }
        }
    }
}

bool qlcg_ice_cell_open(const IceChunkData *c, int ix, int iz) {
    if (!c || ix < 0 || iz < 0 || ix >= ICE_GRID_CELLS || iz >= ICE_GRID_CELLS) return false;
    int bit = iz * ICE_GRID_CELLS + ix;
    return (c->open[bit >> 5] >> (bit & 31)) & 1u;
}

int qlcg_region_ice_apply_load(const MacroWorld *w, RegionData *reg, float x, float z, float half,
                               float feet, float load, float dt, bool impact) {
    if (!w || !reg) return 0;
    IceContact cells[16];
    int n = qlcg_region_ice_contacts(reg, x, z, half, feet, cells, 16);
    if (n <= 0) return 0;
    /* 只保留悬空深水冰：底冻冰与浅滩踩不碎 */
    int live = 0;
    for (int i = 0; i < n; i++) {
        if (cells[i].depth < ICE_BREAK_DEPTH) continue;
        if (cells[i].thickness > cells[i].depth - 0.03f) continue;
        cells[live++] = cells[i];
    }
    n = live;
    if (n <= 0) return 0;
    float s = INFINITY;
    for (int i = 0; i < n; i++) if (cells[i].strength < s) s = cells[i].strength;
    if (!isfinite(s)) return 0;
    float ratio = load / fmaxf(s, .01f);
    bool broke = (s < .01f) || (impact ? (load > s) : (ratio >= 1.8f));
    if (broke) {
        /* 破裂=接触∪外扩0.35m∪中心格 */
        IceContact wide[16];
        int wn = qlcg_region_ice_contacts(reg, x, z, half + .35f, feet, wide, 16);
        int wk = 0;
        for (int i = 0; i < wn; i++) {
            if (wide[i].depth < ICE_BREAK_DEPTH) continue;
            if (wide[i].thickness > wide[i].depth - 0.03f) continue;
            wide[wk++] = wide[i];
        }
        wn = wk;
        qlcg_ice_break_cells(w, reg, cells, n);
        if (wn > 0) qlcg_ice_break_cells(w, reg, wide, wn);
        /* 兜底：中心所在单元即使没进接触集合也必须破 */
        IceContact centre;
        memset(&centre, 0, sizeof(centre));
        centre.wx = floorf((x + reg->center_x) / ICE_CELL_M) * ICE_CELL_M
                  + ICE_CELL_M * 0.5f;
        centre.wz = floorf((z + reg->center_z) / ICE_CELL_M) * ICE_CELL_M
                  + ICE_CELL_M * 0.5f;
        qlcg_ice_break_cells(w, reg, &centre, 1);
        /* 再随机多破一格邻格：洞口不再是规整矩形 */
        {
            uint32_t hsh = (uint32_t)(int64_t)(x * 71.0f + z * 131.0f);
            int pick = (int)((hsh >> 7) % 4u);
            IceContact extra[4];
            int en = qlcg_region_ice_contacts(reg, x, z, half + 1.4f, feet, extra, 4);
            if (en > pick) qlcg_ice_break_cells(w, reg, &extra[pick], 1);
        }
        return 2;
    }
    if (ratio > 1.0f) {
        float amount = (ratio - 1.0f) * dt / .7f;
        if (amount > 0.0f) {
            bool broke_now = false;
            qlcg_ice_add_damage(w, reg, cells, n, amount);
            for (int i = 0; i < n; i++) {
                float lx = 0.0f, lz = 0.0f;
                int ix = 0, iz = 0;
                RegionData *c = chunk_of(reg, cells[i].wx, cells[i].wz, &ix, &iz, &lx, &lz);
                if (!c || !c->ice) continue;
                if (qlcg_ice_cell_damage(c->ice, ix, iz) >= .999f) {
                    qlcg_ice_break_cells(w, reg, &cells[i], 1);
                    broke_now = true;
                }
            }
            if (broke_now) return 2;
        }
        return 1;
    }
    if (impact && load > .8f * s) {
        qlcg_ice_add_damage(w, reg, cells, n, .12f * (load / s) * (load / s));
        return 1;
    }
    return 0;
}

float qlcg_ice_cell_damage(const IceChunkData *c, int ix, int iz) {
    if (!c || ix < 0 || iz < 0 || ix >= ICE_GRID_CELLS || iz >= ICE_GRID_CELLS) return 0.0f;
    return c->damage[iz * ICE_GRID_CELLS + ix];
}

static void set_open_bit(IceChunkData *c, int ix, int iz, bool on) {
    int bit = iz * ICE_GRID_CELLS + ix;
    if (on) c->open[bit >> 5] |= 1u << (bit & 31);
    else    c->open[bit >> 5] &= ~(1u << (bit & 31));
}

static void cell_world(const RegionData *reg, int ix, int iz, float *wx, float *wz) {
    float half = (float)CHUNK_SIZE * 0.5f;
    *wx = reg->center_x - half + ((float)ix + .5f) * ICE_CELL_M;
    *wz = reg->center_z - half + ((float)iz + .5f) * ICE_CELL_M;
}

/* 单元四顶点的平均再冻厚度 */
float qlcg_ice_cell_regrow(const MacroWorld *w, const IceChunkData *c,
                           const RegionData *reg, int ix, int iz, float *out_quality) {
    if (!w || !c || !reg || ix < 0 || iz < 0 || ix >= ICE_GRID_CELLS || iz >= ICE_GRID_CELLS) {
        if (out_quality) *out_quality = .08f;
        return 0.0f;
    }
    float wx, wz;
    cell_world(reg, ix, iz, &wx, &wz);
    int64_t gx, gz;
    qlcg_ice_cell_key(w, wx, wz, &gx, &gz);
    const IceEdit *e = qlcg_ice_edit_find(w, gx, gz);
    if (!e || !(e->flags & ICE_EDIT_HOLE)) {
        if (out_quality) *out_quality = .08f;
        return 0.0f;
    }
    IceSite site;
    IceSample base;
    int vx = ix + 1, vz = iz + 1;
    int vi = vz * reg->width + vx;
    site_from_cache(w, c, reg, vi, qlcg_chunk_water_vertex(reg, vx, vz), reg->heights[vi], &site);
    IceParams p;
    qlcg_ice_params(&site, &p);
    qlcg_ice_sample_params(&p, &site, w->ice_time.climate_day, &base);
    if (base.thickness <= 1e-6f) return 0.0f;
    double q = .08;
    double h = qlcg_ice_regrow(&site, e->broken_at, w->ice_time.climate_day,
                               occupied(w->ice_state, wx, wz), &q);
    if (out_quality) *out_quality = (float)q;
    return (float)h;
}

bool qlcg_ice_chunk_stale(const MacroWorld *w, const RegionData *reg) {
    if (!w || !reg || !reg->ice) return false;
    if (!w->ice_time_valid) return false;
    return fabs(w->ice_time.climate_day - reg->ice->refreshed_day) >= (1.0 / 24.0);
}

/* 首次刷新时把世界稀疏编辑合并进块掩码 */
static void merge_edits(const MacroWorld *w, IceChunkData *c, RegionData *reg) {
    memset(c->open, 0, sizeof(c->open));
    memset(c->damage, 0, sizeof(c->damage));
    for (int iz = 0; iz < ICE_GRID_CELLS; iz++) {
        for (int ix = 0; ix < ICE_GRID_CELLS; ix++) {
            float wx, wz;
            cell_world(reg, ix, iz, &wx, &wz);
            int64_t gx, gz;
            qlcg_ice_cell_key(w, wx, wz, &gx, &gz);
            const IceEdit *e = qlcg_ice_edit_find(w, gx, gz);
            if (!e) continue;
            int bit = iz * ICE_GRID_CELLS + ix;
            if (e->flags & ICE_EDIT_HOLE) c->open[bit >> 5] |= 1u << (bit & 31);
            if (e->damage > 0.0f)
                c->damage[bit] = qlcg_clampf(e->damage, 0.0f, 1.0f);
        }
    }
}

void qlcg_ice_chunk_invalidate(RegionData *reg) {
    if (reg && reg->ice) reg->ice->refreshed_day = -1e30;
}

void qlcg_ice_chunk_attach(const MacroWorld *w, RegionData *reg) {
    if (!w || !reg || !reg->ice) return;
    merge_edits(w, reg->ice, reg);
    reg->ice->merged_edits = w->ice_state ? w->ice_state->edit_revision : 0u;
    reg->ice->revision = 1;
    reg->ice->refreshed_day = w->ice_time_valid ? w->ice_time.climate_day : -1e30;
    reg->ice_revision = reg->ice->revision;
}

bool qlcg_ice_chunk_refresh(MacroWorld *w, RegionData *reg) {
    if (!w || !reg || !reg->ice || !reg->ice_bottom || !reg->ice_coverage) return false;
    IceChunkData *c = reg->ice;
    if (!c->revision) merge_edits(w, c, reg);   /* revision 0 表示首次 */
    double day = w->ice_time.climate_day;
    /* 新破的洞可能落在当时未加载的邻块：按编辑版本补合并 */
    if (w->ice_state && c->merged_edits != w->ice_state->edit_revision) {
        merge_edits(w, c, reg);
        c->merged_edits = w->ice_state->edit_revision;
    }
    int changed = (int)(c->revision == 0);
    float span = (float)(reg->width - 1) * reg->step;
    float origin = -span * 0.5f;

    for (int vz = 1; vz < reg->width - 1; vz++) {
        for (int vx = 1; vx < reg->width - 1; vx++) {
            int vi = vz * reg->width + vx;
            float top = qlcg_chunk_water_vertex(reg, vx, vz);
            float bed = reg->heights[vi];
            /* 湿判据取实时水位，抽干即失效 */
            if (!(reg->water_levels[vi] > bed + .02f) || c->kind[vi] == ICE_KIND_DRY) {
                if (reg->ice_coverage[vi] != 0.0f) { reg->ice_coverage[vi] = 0.0f; reg->ice_bottom[vi] = -1e9f; changed = 1; }
                continue;
            }
            IceSite site;
            site_from_cache(w, c, reg, vi, top, bed, &site);
            site.depth = (top > bed + .02f) ? (top - bed) : 0.0f;
            IceParams p;
            qlcg_ice_params(&site, &p);
            IceSample o;
            qlcg_ice_sample_params(&p, &site, day, &o);
            float dh = 0.0f, dq = 0.0f;
            apply_transition(w, reg->center_x + origin + (float)vx * reg->step,
                             reg->center_z + origin + (float)vz * reg->step, day, &dh, &dq);
            float h = qlcg_clampf(o.thickness + dh, 0.0f, qlcg_maxf(0.0f, site.depth));
            float q = qlcg_clampf(o.quality + dq, .08f, 1.0f);
            qlcg_dev_ice_sample(&h, &q, site.depth);
            float cover = qlcg_ice_coverage(h, site.depth);
            float bottom = fmaxf(bed, top - h);
            if (fabsf(h - c->h[vi]) > .0015f || fabsf(cover - reg->ice_coverage[vi]) > .01f) changed = 1;
            c->h[vi] = h;
            c->q[vi] = q;
            reg->ice_coverage[vi] = cover;
            reg->ice_bottom[vi] = (h > 1e-4f) ? bottom : -1e9f;
        }
    }

    /* 已破单元：推进再冻，达到基线后合回自然冰 */
    for (int iz = 0; iz < ICE_GRID_CELLS; iz++) {
        for (int ix = 0; ix < ICE_GRID_CELLS; ix++) {
            if (!qlcg_ice_cell_open(c, ix, iz)) continue;
            float wx, wz;
            cell_world(reg, ix, iz, &wx, &wz);
            int64_t gx, gz;
            qlcg_ice_cell_key(w, wx, wz, &gx, &gz);
            const IceEdit *e = qlcg_ice_edit_find(w, gx, gz);
            if (!e || !(e->flags & ICE_EDIT_HOLE)) { set_open_bit(c, ix, iz, false); changed = 1; continue; }
            IceSite site;
            int vi = (iz + 1) * reg->width + (ix + 1);
            site_from_cache(w, c, reg, vi, qlcg_chunk_water_vertex(reg, ix + 1, iz + 1), reg->heights[vi], &site);
            IceParams p;
            qlcg_ice_params(&site, &p);
            IceSample base;
            qlcg_ice_sample_params(&p, &site, day, &base);
            if (base.thickness <= 1e-6f) {
                /* 暖季自然冰消失：清记录，立即失去支撑 */
                edit_drop(w->ice_state, gx, gz);
                set_open_bit(c, ix, iz, false);
                changed = 1;
                continue;
            }
            double qq = .08;
            double h = qlcg_ice_regrow(&site, e->broken_at, day, occupied(w->ice_state, wx, wz), &qq);
            /* 洞内冰面始终低于自然基线：达到基线才合回 */
            if (h >= (double)base.thickness - .002) {
                edit_drop(w->ice_state, gx, gz);
                set_open_bit(c, ix, iz, false);
                changed = 1;
            }
        }
    }

    c->revision++;
    c->refreshed_day = day;
    if (changed) reg->ice_revision = c->revision;
    return changed != 0;
}

/* 单元疲劳：供裂纹反馈与探针读取 */
float qlcg_ice_cell_damage_at(const MacroWorld *w, const RegionData *reg, float wx, float wz) {
    (void)w;
    int ix = 0, iz = 0;
    float lx = 0.0f, lz = 0.0f;
    const RegionData *c = chunk_of(reg, wx, wz, &ix, &iz, &lx, &lz);
    if (!c || !c->ice) return 0.0f;
    return qlcg_ice_cell_damage(c->ice, ix, iz);
}

/* 夹具/事件后强制重刷全部已加载块 */
void qlcg_ice_refresh_all(RegionData *reg) {
    if (!reg || !reg->stream) return;
    RegionStream *st = reg->stream;
    for (int i = 0; i < st->count; i++) {
        RegionData *c = st->slots[st->active[i]].data;
        if (!c || !c->ice) continue;
        c->ice->refreshed_day = -1e30;
        qlcg_ice_chunk_refresh((MacroWorld *)st->world, c);
    }
}

/* 每帧冰刷新调度：脚下 3×3 不限预算，其余按预算推进 */
void qlcg_ice_tick(RegionData *reg, float player_lx, float player_lz, int budget) {
    if (!reg || !reg->stream) return;
    RegionStream *st = reg->stream;
    const MacroWorld *w = st->world;
    if (!w || !w->ice_time_valid) return;
    int pcx = reg->center_cx + (int)floorf((player_lx + CHUNK_SIZE * .5f) / CHUNK_SIZE);
    int pcz = reg->center_cz + (int)floorf((player_lz + CHUNK_SIZE * .5f) / CHUNK_SIZE);
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        RegionData *c = st->slots[qlcg_stream_slot(pcx + dx, pcz + dz)].data;
        if (!c || c->center_cx != pcx + dx || c->center_cz != pcz + dz) continue;
        if (qlcg_ice_chunk_stale(w, c)) qlcg_ice_chunk_refresh((MacroWorld *)w, c);
    }
    int done = 0;
    for (int i = st->count - 1; i >= 0 && done < budget; i--) {
        RegionData *c = st->slots[st->active[i]].data;
        if (!c || !c->ice) continue;
        if (!qlcg_ice_chunk_stale(w, c)) continue;
        qlcg_ice_chunk_refresh((MacroWorld *)w, c);
        done++;
    }
}

/* 以给定年均温求自然基线厚度：温度事件用它估算过渡量 */
float qlcg_ice_baseline_h(const MacroWorld *w, const RegionData *reg, float wx, float wz,
                          float annual) {
    if (!w || !reg) return 0.0f;
    float lx = 0.0f, lz = 0.0f;
    int ix = 0, iz = 0;
    const RegionData *c = chunk_of(reg, wx, wz, &ix, &iz, &lx, &lz);
    if (!c || !c->ice) return 0.0f;
    int vi = (iz + 1) * c->width + (ix + 1);
    if (vi < 0 || vi >= ICE_VERTS) return 0.0f;
    IceSite site;
    site_from_cache(w, c->ice, c, vi, qlcg_chunk_water_vertex(c, ix + 1, iz + 1),
                    c->heights[vi], &site);
    site.annual_temp = annual;
    site.depth = qlcg_maxf(0.0f, site.top - site.bed);
    IceParams p;
    qlcg_ice_params(&site, &p);
    IceSample o;
    qlcg_ice_sample_params(&p, &site, w->ice_time.climate_day, &o);
    return o.thickness;
}
