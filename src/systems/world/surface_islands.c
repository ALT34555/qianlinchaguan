/* 单格海岛：对齐 island-coasts.mjs */
#include "surface_islands.h"
#include "gen/gen_height.h"
#include "island_atlas_data.inc"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const double ISLAND_PI = 3.14159265358979323846;

typedef struct {
    int    cell, variant;
    double wx, wz, sine, cosine, span, peak, delta;
} IslandEntry;

typedef struct { int cell, entry; } IslandRef;

struct IslandCatalog {
    IslandEntry *items;
    int          count;
    IslandRef   *refs;
    int          refs_count;
};

static double clampd(double v, double a, double b) { return fmax(a, fmin(b, v)); }
static double smooth(double t) { t = clampd(t, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }

/* 与 base hash 同位运算 */
static uint32_t hash_u32(uint32_t v) {
    v = (v ^ (v >> 16)) * 0x7feb352du;
    v = (v ^ (v >> 15)) * 0x846ca68bu;
    return v ^ (v >> 16);
}

/* 经度短弧：|dx|<C 等价双取模，避开 fmod */
static double wrap_short(double dx, double C) {
    if (dx > C * .5) dx -= C;
    else if (dx < -C * .5) dx += C;
    return dx;
}

/* 末行越界返回 -1，经度取模 */
static int cell_at(const MacroWorld *w, int x, int y) {
    if (y < 0 || y >= w->rows) return -1;
    int columns = w->columns;
    return y * columns + ((x % columns) + columns) % columns;
}

/* 有符号平方根 8 位量化反解 */
static double decode_byte(signed char raw) {
    double v = (double)raw / 127.0;
    return (v < 0.0 ? -1.0 : 1.0) * v * v * 300.0;
}

/* 模板双线性取值，u/v 越界返回 NAN */
static double template_height(int variant, double u, double v) {
    if (u < 0.0 || v < 0.0 || u > 1.0 || v > 1.0) return NAN;
    const int side = ISLAND_ATLAS_SIDE, last = side - 1;
    double x = u * last, z = v * last;
    int ix = (int)floor(x), iz = (int)floor(z);
    if (ix > last - 1) ix = last - 1;
    if (iz > last - 1) iz = last - 1;
    double tx = x - ix, tz = z - iz;
    const signed char *g = island_atlas + (size_t)variant * side * side + (size_t)iz * side + ix;
    double a = decode_byte(g[0]) + (decode_byte(g[1]) - decode_byte(g[0])) * tx;
    double b = decode_byte(g[side]) + (decode_byte(g[side + 1]) - decode_byte(g[side])) * tx;
    return a + (b - a) * tz;
}

/* 峰高包络（对齐 coast-scale.mjs） */
static double island_relief_peak(const MacroWorld *w, const MacroIsland *island) {
    double scale = w->height_scale > 0.0f ? (double)w->height_scale : 1.0;
    double cap = (double)qlcg_terrain_height_cap(&w->opt);
    double original = fmax(1.0, (double)w->height[island->cell]);
    double seed_height = clampd(original / (160.0 * scale), 0.0, 1.0);
    double activity = pow(clampd((double)w->opt.tectonic_activity, 0.0, 9.0) / 9.0, 1.504);
    double uplift = clampd((double)w->uplift[island->cell], 0.0, 1.0);
    double fraction = .01 + .035 * seed_height +
        activity * (.04 + .16 * uplift) * (.35 + .65 * seed_height);
    return fmin(cap * .3, fmax(original, cap * fraction));
}

/* 孤立深海单格岛判据：八邻全海、三乘三无湖无主河 */
static int island_eligible(const MacroWorld *w, const MacroIsland *island) {
    int columns = w->columns, col = island->cell % columns, row = island->cell / columns;
    if (row < 1 || row >= w->rows - 1) return 0;
    if (cos((((double)row + .5) / w->rows - .5) * ISLAND_PI) < 0.2) return 0;
    if (!(w->height[island->cell] > 0.0f) || w->water[island->cell] != 0) return 0;
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        int i = cell_at(w, col + dx, row + dz);
        if (i < 0) return 0;
        if (w->water[i] == 2) return 0;
        if (w->main_river_width && w->main_river_width[i] > 0.0f) return 0;
        if ((dx || dz) && (w->height[i] >= 0.0f || w->water[i] != 1)) return 0;
    }
    return 1;
}

static struct IslandCatalog *build_catalog(const MacroWorld *w) {
    struct IslandCatalog *c = (struct IslandCatalog *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    size_t n = (size_t)w->macro_island_count;
    c->items = (IslandEntry *)calloc(n ? n : 1, sizeof(*c->items));
    c->refs = (IslandRef *)calloc(n ? n * 9 : 1, sizeof(*c->refs));
    if (!c->items || !c->refs) { free(c->items); free(c->refs); free(c); return NULL; }
    double C = (double)w->circumference, span = C / (double)w->columns;
    double scale = w->height_scale > 0.0f ? (double)w->height_scale : 1.0;
    for (int k = 0; k < w->macro_island_count; k++) {
        const MacroIsland *src = &w->macro_islands[k];
        if (!island_eligible(w, src)) continue;
        int col = src->cell % w->columns, row = src->cell / w->columns;
        uint32_t seed = hash_u32(w->opt.seed ^ (uint32_t)((src->cell + 1) * 374761393));
        double angle = (double)hash_u32(seed ^ 0x726f7461u) / 4294967296.0 * 2.0 * ISLAND_PI;
        IslandEntry *e = &c->items[c->count++];
        e->cell = src->cell;
        e->variant = (int)(seed % (uint32_t)ISLAND_ATLAS_COUNT);
        e->span = span;
        e->wx = ((double)col + .5) * span - C * .5;
        e->wz = ((double)row + .5) * span - C * qlcg_world_z_offset(w);
        e->sine = sin(angle);
        e->cosine = cos(angle);
        e->peak = island_relief_peak(w, src);
        e->delta = (double)w->height[src->cell] - (double)src->base_height * scale;
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            int cell = cell_at(w, col + dx, row + dz);
            if (cell < 0) continue;
            c->refs[c->refs_count++] = (IslandRef){cell, c->count - 1};
        }
    }
    /* 稳定按格排序：采样时二分只走本格条目 */
    for (int i = 1; i < c->refs_count; i++) {
        IslandRef key = c->refs[i];
        int j = i - 1;
        while (j >= 0 && c->refs[j].cell > key.cell) { c->refs[j + 1] = c->refs[j]; j--; }
        c->refs[j + 1] = key;
    }
    return c;
}

static const struct IslandCatalog *catalog_at(const MacroWorld *w) {
    MacroWorld *mutable_w = (MacroWorld *)w;
    if (w->island_catalog) return w->island_catalog;
    if (!w->islands_ready || !w->macro_islands || w->macro_island_count <= 0) return NULL;
    mutable_w->island_catalog = build_catalog(w);
    return mutable_w->island_catalog;
}

static int first_ref(const struct IslandCatalog *c, int cell) {
    int lo = 0, hi = c->refs_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (c->refs[mid].cell < cell) lo = mid + 1; else hi = mid;
    }
    return lo;
}

bool qlcg_island_coast(const MacroWorld *w, double wx, double wz,
                       double natural, double *out_height) {
    const struct IslandCatalog *c = catalog_at(w);
    if (!c || !c->refs_count) return false;
    double C = (double)w->circumference, span = C / (double)w->columns;
    int x = (int)floor((wx / C + .5) * w->columns);
    int y = (int)floor((wz / C + qlcg_world_z_offset(w)) * w->columns);
    int cell = cell_at(w, x, y);
    if (cell < 0) return false;
    for (int r = first_ref(c, cell); r < c->refs_count && c->refs[r].cell == cell; r++) {
        const IslandEntry *island = &c->items[c->refs[r].entry];
        double dx = wx - island->wx;
        dx = wrap_short(dx, C);
        double dz = wz - island->wz;
        if (fabs(dx) >= span || fabs(dz) >= span) continue;
        double weight = (1.0 - fabs(dx) / span) * (1.0 - fabs(dz) / span);
        double restored = natural - island->delta * weight;
        double physical = span * .92;
        double u = .5 + (dx * island->cosine + dz * island->sine) / physical;
        double v = .5 + (-dx * island->sine + dz * island->cosine) / physical;
        double h = template_height(island->variant, u, v);
        double height = restored;
        if (!isnan(h)) {
            double scale = island->peak / island_atlas_peaks[island->variant];
            double target = fmax(-3000.0, h * scale);
            double blend = h >= 0.0 ? 1.0 : 1.0 - smooth((-h - 12.0) / 60.0);
            height = restored + (target - restored) * blend;
        }
        if (out_height) *out_height = height;
        return true;
    }
    return false;
}

bool qlcg_island_at(const MacroWorld *w, double wx, double wz) {
    const struct IslandCatalog *c = catalog_at(w);
    if (!c || !c->refs_count) return false;
    double C = (double)w->circumference, span = C / (double)w->columns;
    int cell = cell_at(w, (int)floor((wx / C + .5) * w->columns),
                          (int)floor((wz / C + qlcg_world_z_offset(w)) * w->columns));
    if (cell < 0) return false;
    for (int r = first_ref(c, cell); r < c->refs_count && c->refs[r].cell == cell; r++) {
        const IslandEntry *island = &c->items[c->refs[r].entry];
        double dx = wx - island->wx;
        dx = wrap_short(dx, C);
        if (fabs(dx) < span && fabs(wz - island->wz) < span) return true;
    }
    return false;
}

int qlcg_island_list(const MacroWorld *w, IslandSummary *out, int capacity) {
    const struct IslandCatalog *c = catalog_at(w);
    if (!c) return 0;
    int n = 0;
    for (int k = 0; k < c->count && n < capacity; k++) {
        const IslandEntry *e = &c->items[k];
        out[n++] = (IslandSummary){e->cell, e->variant, (float)e->wx, (float)e->wz,
                                   (float)e->peak, (float)e->span};
    }
    return c->count;
}

void qlcg_islands_free(MacroWorld *w) {
    if (!w || !w->island_catalog) return;
    free(w->island_catalog->items);
    free(w->island_catalog->refs);
    free(w->island_catalog);
    w->island_catalog = NULL;
}
