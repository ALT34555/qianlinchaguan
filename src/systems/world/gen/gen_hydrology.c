/* 水文：一次填洼、洼地分组、闭合湖平衡与河网判定 */
#include "gen_hydrology.h"
#include "gen_water.h"
#include "gen_river.h"
#include "../surface_lakes.h"
#include "../../../core/math_util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEPTH_EPS 0.01
#define HYPSE 0.7
#define MIN_DEPRESSION_DEPTH 8.0
#define MIN_CLOSED_LAKE_KM2 400.0
#define MIN_WATER_DEPTH 0.5
#define LAKE_OUTLET_SHARE 0.05
#define MIN_LAKE_CELLS 3
#define MIN_DEEP_LAKE 30.0
#define MIN_RIVER_CATCHMENT_KM2 25000.0
#define MAX_PLAIN_LAKES 4
#define MAX_PLAIN_LAKE_DEPTH 30.0

#define ST_NONE 0
#define ST_OPEN 1
#define ST_SALT 2
#define ST_DRY 3
#define ST_PLAIN 4

typedef struct {
    int   root, slot;
    double area, min_height, evap_area, rain_volume, land_runoff, depth_area;
    int   cells, min_cell, outlet;
    float min_x, max_x, min_y, max_y;
    double capacity, evap_unit, max_depth, supply, overflow, wet_share, loss;
    int   significant, visible, plain;
} Basin;

static float smooth01(float v) {
    float t = qlcg_clampf(v, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

static int uf_find(int *p, int k) {
    while (p[k] != k) { p[k] = p[p[k]]; k = p[k]; }
    return k;
}

/* 闭合湖平衡：按供给与蒸发回推水深 */
static double closed_lake_depth(double supply, double evap_unit, double area, double max_depth, int *exists) {
    double unit = qlcg_evaporation_m3s(evap_unit, 1.0);
    double equilibrium = (unit > 0.0) ? fmax(0.0, supply) / unit : 0.0;
    double wet = (area > 0.0) ? fmin(1.0, equilibrium / area) : 0.0;
    double depth = max_depth * pow(fmax(0.0, wet), HYPSE);
    *exists = (equilibrium >= MIN_CLOSED_LAKE_KM2 && depth >= MIN_WATER_DEPTH) ? 1 : 0;
    return depth;
}

void qlcg_build_hydrology(MacroWorld *w) {
    if (!w) return;
    int n = w->cell_count, cols = w->columns, rows = w->rows;
    int *receiver = (int *)malloc((size_t)n * sizeof(int));
    uint32_t *rank = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
    int *order = (int *)malloc((size_t)n * sizeof(int));
    float *spill = (float *)calloc((size_t)n, sizeof(float));
    uint8_t *visited = (uint8_t *)calloc((size_t)n, sizeof(uint8_t));
    double *flow = (double *)calloc((size_t)n, sizeof(double));
    double *catchment = (double *)calloc((size_t)n, sizeof(double));
    double *potential = (double *)calloc((size_t)n, sizeof(double));
    int *lake_id = w->lake_id;
    int *root_slot = (int *)malloc((size_t)n * sizeof(int));
    int *cell_slot = (int *)malloc((size_t)n * sizeof(int));
    int *outlet_at = (int *)malloc((size_t)n * sizeof(int));
    Basin *basins = (Basin *)calloc((size_t)n, sizeof(Basin));
    int *parent = (int *)malloc((size_t)n * sizeof(int));
    if (!receiver || !rank || !order || !spill || !visited || !flow || !catchment ||
        !potential || !root_slot || !lake_id || !cell_slot || !outlet_at || !basins || !parent) {
        free(receiver); free(rank); free(order); free(spill); free(visited);
        free(flow); free(catchment); free(potential); free(root_slot);
        free(cell_slot); free(outlet_at); free(basins); free(parent);
        return;
    }
    for (int i = 0; i < n; i++) {
        receiver[i] = -1;
        lake_id[i] = -1;
        cell_slot[i] = -1;
        outlet_at[i] = -1;
        parent[i] = i;
        root_slot[i] = -1;
        w->level[i] = -1e9f;
        w->saline[i] = 0;
        w->river_width[i] = 0;
        if (w->river_level) w->river_level[i]=NAN;
        w->water[i] = 0;
    }

    memset(&w->hydrology, 0, sizeof(w->hydrology));
    w->oasis_count = 0;
    /* 1. Priority-Flood 填洼 */
    MinHeap heap;
    qlcg_heap_init(&heap, n * 2);
    int lowest = 0, sea = 0;
    for (int i = 0; i < n; i++) {
        if (w->height[i] < w->height[lowest]) lowest = i;
        if (w->height[i] <= 0.0) {
            visited[i] = 1;
            spill[i] = 0.0;
            w->water[i] = 1;
            qlcg_heap_push(&heap, i, 0.0);
            sea++;
        }
    }
    if (!sea) {
        visited[lowest] = 1;
        spill[lowest] = w->height[lowest];
        qlcg_heap_push(&heap, lowest, w->height[lowest]);
    }
    int count = 0;
    int32_t idx; float key;
    while (qlcg_heap_pop(&heap, &idx, &key)) {
        rank[idx] = (uint32_t)count;
        order[count++] = idx;
        for (int d = 0; d < 8; d++) {
            int j = w->neighbors[idx * 8 + d];
            if (visited[j]) continue;
            visited[j] = 1;
            spill[j] = fmax(w->height[j], spill[idx]);
            receiver[j] = idx;
            qlcg_heap_push(&heap, j, spill[j]);
        }
    }
    qlcg_heap_free(&heap);

    /* 2. 8 邻域坡度重定向 */
    for (int at = 0; at < count; at++) {
        int i = order[at];
        if (w->height[i] <= 0.0 || receiver[i] < 0) continue;
        int best = receiver[i];
        double grade = -INFINITY;
        for (int d = 0; d < 8; d++) {
            int j = w->neighbors[i * 8 + d];
            if (rank[j] >= rank[i] || spill[j] > spill[i] + 0.001) continue;
            double c = ((double)spill[i] - spill[j]) / fmax(1.0, w->lengths[i * 8 + d]);
            if (c > grade) { grade = c; best = j; }
        }
        receiver[i] = best;
    }

    /* 3. 汇流：共享下游代价形成连续走廊 */
    for (int i = 0; i < n; i++) catchment[i] = w->areas[i];
    for (int at = count - 1; at >= 0; at--) {
        int i = order[at], r = receiver[i];
        if (r >= 0) catchment[r] += catchment[i];
    }
    for (int at = 0; at < count; at++) {
        int i = order[at];
        if (w->height[i] <= 0.0 || receiver[i] < 0) continue;
        double steepest = 0.0, original_len = 1.0;
        for (int d = 0; d < 8; d++) {
            int j = w->neighbors[i * 8 + d];
            if (j == receiver[i]) original_len = w->lengths[i * 8 + d];
            if (rank[j] >= rank[i] || spill[j] > spill[i]) continue;
            double g = ((double)spill[i] - spill[j]) / fmax(1.0, w->lengths[i * 8 + d]);
            if (g > steepest) steepest = g;
        }
        double friction = (w->hydro_friction ? w->hydro_friction[i] : 1.0);
        int gentle = (steepest < 0.006);
        if (steepest <= 0.0 || spill[i] - w->height[i] >= 8.0) {
            potential[i] = potential[receiver[i]] + original_len * friction;
            continue;
        }
        int best = receiver[i];
        double score = INFINITY;
        for (int d = 0; d < 8; d++) {
            int j = w->neighbors[i * 8 + d];
            if (rank[j] >= rank[i] || spill[j] > spill[i]) continue;
            double g = ((double)spill[i] - spill[j]) / fmax(1.0, w->lengths[i * 8 + d]);
            if (g < steepest * (gentle ? 0.008 : 0.08)) continue;
            double attraction = log2(1.0 + catchment[j] / fmax(1e-9, w->areas[j]));
            double penalty = 1.0 + (1.0 - g / steepest) * (gentle ? 0.35 : 2.0);
            double turn = 0.0;
            int r = receiver[j];
            if (r >= 0) {
                double ax = (float)(j % cols - i % cols);
                double ay = (float)(j / cols - i / cols);
                double bx = (float)(r % cols - j % cols);
                double by = (float)(r / cols - j / cols);
                if (ax > cols / 2.0) ax -= (float)cols;
                if (ax < -cols / 2.0) ax += (float)cols;
                if (bx > cols / 2.0) bx -= (float)cols;
                if (bx < -cols / 2.0) bx += (float)cols;
                turn = 1.0 - (ax * bx + ay * by) / fmax(1e-9, hypot(ax, ay) * hypot(bx, by));
            }
            double cand = potential[j] + w->lengths[i * 8 + d] * friction * penalty
                         * (1.0 + turn * 0.25) / (1.0 + attraction * 0.35);
            if (cand < score) { score = cand; best = j; }
        }
        receiver[i] = best;
        potential[i] = score;
    }
    memset(catchment, 0, (size_t)n * sizeof(double));
    memset(potential, 0, (size_t)n * sizeof(double));

    /* 4. 产流 */
    double input = 0.0;
    for (int i = 0; i < n; i++) {
        if (w->height[i] > 0.0) {
            flow[i] = qlcg_runoff_m3s(w->rainfall[i], w->evaporation[i], w->areas[i]);
            catchment[i] = w->areas[i];
            input += flow[i];
            w->hydrology.land_area += w->areas[i];
        } else {
            flow[i] = 0.0;
            catchment[i] = 0.0;
        }
    }

    /* 5. 洼地分组：溢流高程相同且相邻 */
    for (int i = 0; i < n; i++) {
        if (w->height[i] <= 0.0 || spill[i] - w->height[i] <= DEPTH_EPS) continue;
        static const int DIRS[4] = { 1, 3, 6, 7 };
        for (int q = 0; q < 4; q++) {
            int j = w->neighbors[i * 8 + DIRS[q]];
            if (w->height[j] <= 0.0 || spill[j] - w->height[j] <= DEPTH_EPS) continue;
            if (fabsf(spill[j] - spill[i]) > DEPTH_EPS * 0.5) continue;
            int a = uf_find(parent, i), b = uf_find(parent, j);
            if (a != b) { if (a < b) parent[b] = a; else parent[a] = b; }
        }
    }
    int basin_count = 0;
    int basin_cap = n;
    for (int i = 0; i < n; i++) {
        if (w->height[i] <= 0.0 || spill[i] - w->height[i] <= DEPTH_EPS) continue;
        int root = uf_find(parent, i);
        parent[i] = root;
        int slot = root_slot[root];
        if (slot < 0) {
            if (basin_count >= basin_cap) continue;
            slot = basin_count++;
            root_slot[root] = slot;
            memset(&basins[slot], 0, sizeof(Basin));
            basins[slot].root = root;
            basins[slot].slot = slot;
            basins[slot].min_height = 1e30;
            basins[slot].outlet = -1;
            basins[slot].min_x = 0.0; basins[slot].max_x = 0.0;
            basins[slot].min_y = 1e30; basins[slot].max_y = 0.0;
        }
        Basin *bs = &basins[slot];
        double area = w->areas[i];
        bs->area += area;
        bs->cells++;
        if (w->height[i] < bs->min_height) { bs->min_height = w->height[i]; bs->min_cell = i; }
        bs->evap_area += w->evaporation[i] * area;
        bs->rain_volume += w->rainfall[i] * area * 1000.0 / (double)QLCG_YEAR_SECONDS;
        bs->land_runoff += flow[i];
        bs->depth_area += (spill[i] - w->height[i]) * area;
        int dx = i % cols - root % cols;
        if (dx > cols / 2) dx -= cols;
        if (dx < -cols / 2) dx += cols;
        float y = (float)(i / cols);
        if ((float)dx < bs->min_x) bs->min_x = (float)dx;
        if ((float)dx > bs->max_x) bs->max_x = (float)dx;
        if (y < bs->min_y) bs->min_y = y;
        if (y > bs->max_y) bs->max_y = y;
        if (bs->outlet < 0 || rank[i] < rank[bs->outlet]) bs->outlet = i;
        cell_slot[i] = slot;
    }

    /* 6. 盆地判决 */
    Basin *shallow[MAX_PLAIN_LAKES + 1];
    int shallow_n = 0;
    for (int b = 0; b < basin_count; b++) {
        Basin *bs = &basins[b];
        if (bs->outlet < 0) continue;
        bs->evap_unit = (bs->area > 0.0) ? bs->evap_area / bs->area : 0.0;
        bs->capacity = qlcg_evaporation_m3s(bs->evap_unit, bs->area);
        bs->max_depth = spill[bs->outlet] - bs->min_height;
        bs->significant = (bs->max_depth >= MIN_DEPRESSION_DEPTH) ? 1 : 0;
        bs->visible = (bs->significant && (bs->cells >= MIN_LAKE_CELLS || bs->max_depth >= MIN_DEEP_LAKE)) ? 1 : 0;
        bs->plain = (bs->max_depth <= MAX_PLAIN_LAKE_DEPTH && bs->max_depth >= 1.0 &&
                     bs->depth_area / fmax(1e-6, bs->area) <= 13.0 &&
                     spill[bs->outlet] <= 300.0) ? 1 : 0;
        if (bs->plain) {
            bs->visible = 0;
            if (bs->cells >= 3 && bs->area >= 2000.0 && bs->area <= 20000.0 &&
                bs->max_x > bs->min_x && bs->max_y > bs->min_y &&
                bs->rain_volume >= bs->capacity * 0.65) {
                w->hydrology.shallow_candidates++;
                int at = shallow_n < MAX_PLAIN_LAKES ? shallow_n++ : MAX_PLAIN_LAKES;
                double value = bs->area * bs->rain_volume / fmax(1, bs->capacity);
                while (at > 0) {
                    Basin *prev = shallow[at - 1];
                    double v = prev->area * prev->rain_volume / fmax(1, prev->capacity);
                    if (v > value || (v == value && prev->root < bs->root)) break;
                    shallow[at] = prev;
                    at--;
                }
                shallow[at] = bs;
            }
        }
    }
    for (int q = 0; q < shallow_n; q++) shallow[q]->visible = 1;

    /* 7. 湖盆内部接向出口 */
    memset(visited, 0, (size_t)n);
    int *queue = (int *)malloc((size_t)n * sizeof(int));
    uint8_t *status = (uint8_t *)calloc((size_t)basin_count + 1, sizeof(uint8_t));
    float *level = (float *)calloc((size_t)basin_count + 1, sizeof(float));
    if (!queue || !status || !level) {
        free(receiver); free(rank); free(order); free(spill); free(visited);
        free(flow); free(catchment); free(potential); free(root_slot);
        free(cell_slot); free(outlet_at); free(basins); free(parent);
        free(queue); free(status); free(level);
        return;
    }
    for (int b = 0; b < basin_count; b++) {
        int root = basins[b].outlet;
        if (root < 0) continue;
        int head = 0, tail = 1;
        queue[0] = root;
        visited[root] = 1;
        while (head < tail) {
            int i = queue[head++];
            for (int d = 0; d < 8; d++) {
                int j = w->neighbors[i * 8 + d];
                if (visited[j] || cell_slot[j] != b) continue;
                visited[j] = 1;
                receiver[j] = i;
                queue[tail++] = j;
            }
        }
    }
    /* 重建无环汇流序 */
    memset(visited, 0, (size_t)n);
    for (int i = 0; i < n; i++) if (receiver[i] >= 0) visited[receiver[i]]++;
    int head = 0, tail = 0;
    for (int i = 0; i < n; i++) if (!visited[i]) order[tail++] = i;
    while (head < tail) {
        int i = order[head++], r = receiver[i];
        if (r >= 0 && --visited[r] == 0) order[tail++] = r;
    }
    if (tail != n) {
        free(receiver); free(rank); free(order); free(spill); free(visited);
        free(flow); free(catchment); free(potential); free(root_slot);
        free(cell_slot); free(outlet_at); free(basins); free(parent);
        free(queue); free(status); free(level);
        return;
    }
    count = tail;
    for (int at = 0; at < count / 2; at++) {
        int t = order[at]; order[at] = order[count - 1 - at]; order[count - 1 - at] = t;
    }
    for (int at = 0; at < count; at++) rank[order[at]] = (uint32_t)at;

    /* 8. 湖口判定与汇流 */
    for (int b = 0; b < basin_count; b++) if (!basins[b].visible) status[b] = ST_PLAIN;
    double units = (double)w->equator_chunks * (float)CHUNK_SIZE / (float)w->columns / 64.0;
    double threshold = qlcg_macro_river_threshold(units, w->opt.precipitation);
    if(qlcg_world_flat(w))threshold*=.25;
    double lake_loss = 0, lake_rain = 0, lake_runoff = 0, dry_loss = 0, river_loss = 0;
    int oasis_slot[8], oasis_shore[8];
    double oasis_gap[8];

    for (int at = count - 1; at >= 0; at--) {
        int i = order[at];
        int slot = cell_slot[i];
        if (slot < 0 && w->height[i] > 0.0 && receiver[i] >= 0 &&
            w->rainfall[i] < 250.0 && flow[i] > (double)threshold * 0.25) {
            double area = qlcg_macro_band_width(flow[i]) * sqrt(w->areas[i]) * 0.004;
            double loss = fmin(flow[i] * 0.08,
                               qlcg_evaporation_m3s(fmax(0.0, (double)w->evaporation[i] - w->rainfall[i]), area));
            flow[i] -= loss;
            river_loss += loss;
        }
        if (slot >= 0 && basins[slot].outlet == i && basins[slot].visible) {
            Basin *bs = &basins[slot];
            double q = flow[i];
            bs->supply = fmax(0.0, q - bs->land_runoff + bs->rain_volume);
            int open = (bs->supply > 0.0 && bs->supply >= bs->capacity) ? 1 : 0;
            bs->overflow = open ? bs->supply - bs->capacity : 0.0;
            if (open) {
                status[slot] = ST_OPEN;
                level[slot] = spill[i];
                bs->wet_share = 1.0;
                bs->loss = bs->capacity;
                if (receiver[i] >= 0 && bs->overflow > threshold * LAKE_OUTLET_SHARE) outlet_at[receiver[i]] = slot;
            } else {
                double cost = bs->capacity - bs->rain_volume + bs->land_runoff;
                bs->wet_share = (cost > 0.0) ? fmin(1.0, q / cost) : 0.0;
                int exists = 0;
                double depth = closed_lake_depth(bs->capacity * bs->wet_share, bs->evap_unit,
                                                bs->area, bs->max_depth, &exists);
                status[slot] = exists ? ST_SALT : ST_DRY;
                level[slot] = bs->min_height + depth;
                if (exists) {
                    bs->supply = q + bs->wet_share * (bs->rain_volume - bs->land_runoff);
                    bs->loss = bs->supply;
                } else {
                    bs->wet_share = 0.0;
                    bs->loss = 0.0;
                    dry_loss += q;
                }
                double oasis_area = bs->evap_unit > 0 ? q / qlcg_evaporation_m3s(bs->evap_unit, 1) : 0;
                if (bs->rain_volume < bs->capacity * .45 && oasis_area >= 2 && w->temperature[bs->min_cell] > 5) {
                    int at = w->oasis_count < 8 ? w->oasis_count++ : 8;
                    while (at > 0 && (q > w->oases[at-1].inflow ||
                           (q == w->oases[at-1].inflow && bs->min_cell < w->oases[at-1].cell))) {
                        if (at < 8) { w->oases[at] = w->oases[at-1]; oasis_slot[at] = oasis_slot[at-1]; }
                        at--;
                    }
                    if (at < 8) {
                        w->oases[at] = (MacroOasis){bs->min_cell, fmin(400, oasis_area * .25), q, status[slot] == ST_SALT};
                        oasis_slot[at] = slot;
                    }
                }
                receiver[i] = -1;
            }
            lake_rain += bs->rain_volume * bs->wet_share;
            lake_runoff += bs->land_runoff * bs->wet_share;
            lake_loss += bs->loss;
            flow[i] = bs->overflow;
        }
        int r = receiver[i];
        if (r >= 0) { flow[r] += flow[i]; catchment[r] += catchment[i]; }
    }

    /* 9. 湖泊记录与水面 */
    w->lake_count = 0;
    qlcg_lakes_free(w);
    free(w->lakes);
    w->lakes = (MacroLake *)calloc((size_t)basin_count + 1, sizeof(MacroLake));
    int *lake_index = (int *)malloc((size_t)basin_count * sizeof(int));
    if (lake_index && w->lakes) {
        for (int b = 0; b < basin_count; b++) lake_index[b] = -1;
        for (int b = 0; b < basin_count; b++) {
            if (status[b] != ST_OPEN && status[b] != ST_SALT) continue;
            MacroLake *lk = &w->lakes[w->lake_count];
            memset(lk, 0, sizeof(*lk));
            lk->id = w->lake_count;
            lk->level = level[b];
            lk->outlet = (status[b] == ST_OPEN && receiver[basins[b].outlet] >= 0) ? receiver[basins[b].outlet] : -1;
            lk->exit_cell = basins[b].outlet;
            lk->outflow = basins[b].overflow;
            lk->inflow = basins[b].supply;
            lk->evaporation = basins[b].loss;
            lk->rainfall = basins[b].rain_volume * basins[b].wet_share;
            lk->runoff_replaced = basins[b].land_runoff * basins[b].wet_share;
            lk->estimated_area_km2 = basins[b].area * basins[b].wet_share;
            lk->closed = (status[b] == ST_SALT) ? 1 : 0;
            lk->plain = basins[b].plain;
            if (lk->outlet>=0 && lk->outflow>threshold*LAKE_OUTLET_SHARE) {
                w->river_width[lk->exit_cell]=(float)(qlcg_macro_band_width(lk->outflow)*.5);
                if (w->river_level) w->river_level[lk->exit_cell]=lk->level;
            }
            lake_index[b] = w->lake_count++;
        }
        for (int i = 0; i < n; i++) {
            int slot = cell_slot[i];
            if (slot < 0) continue;
            int entry = lake_index[slot];
            if (entry < 0) continue;
            float depth = level[slot] - w->height[i];
            if (depth <= DEPTH_EPS) continue;
            w->water[i] = 2;
            lake_id[i] = entry;
            w->level[i] = level[slot];
            w->saline[i] = w->lakes[entry].closed ? 1 : 0;
            w->lakes[entry].area_km2 += w->areas[i];
            w->lakes[entry].cells++;
            qlcg_lakes_record(w,entry,i);
            if (depth > w->lakes[entry].max_depth) w->lakes[entry].max_depth = depth;
        }
        free(lake_index);
    }
    qlcg_lakes_prepare(w);

    /* 10. 河网 */
    double area_sum = 0.0;
    for (int i = 0; i < n; i++) area_sum += w->areas[i];
    double average = (double)(area_sum / (double)n);
    double min_catch = MIN_RIVER_CATCHMENT_KM2 * fmin(1.0, (units / 625.0) * (units / 625.0));
    for (int k = 0; k < w->oasis_count; k++) { oasis_shore[k] = -1; oasis_gap[k] = INFINITY; }
    memset(visited, 0, (size_t)n);
    for (int at = 0; at < count; at++) {
        int i = order[at];
        if (w->water[i] == 2) {
            if (w->lakes[lake_id[i]].plain) visited[i] = 1;
            for (int k = 0; k < w->oasis_count; k++) if (w->oases[k].lake && oasis_slot[k] == cell_slot[i]) {
                for (int d = 0; d < 8; d++) {
                    int j = w->neighbors[i*8+d];
                    double gap = fabs(w->height[j] - (double)level[oasis_slot[k]]);
                    if (w->height[j] > 0 && w->water[j] != 2 && gap < oasis_gap[k]) {
                        oasis_shore[k] = j; oasis_gap[k] = gap;
                    }
                }
            }
        }
        if (w->height[i] <= 0.0 || w->water[i] == 2) continue;
        int bs = cell_slot[i];
        if (bs >= 0 && (status[bs] == ST_SALT || status[bs] == ST_DRY)) continue;
        double catch_need = fmax(average * 4.0, min_catch);
        int qualifies = (flow[i] > (double)threshold && catchment[i] > (double)catch_need) ? 1 : 0;
        int slot = outlet_at[i];
        int outlet = (slot >= 0 && status[slot] == ST_OPEN && flow[i] > (double)threshold * LAKE_OUTLET_SHARE) ? 1 : 0;
        int r = receiver[i];
        int inlet = (r >= 0 && visited[r] && flow[i] > (double)threshold * 0.25 &&
                     catchment[i] > fmax(w->areas[i] * 2.0, 1000.0)) ? 1 : 0;
        if (inlet) visited[i] = 1;
        if (!qualifies && !outlet && !inlet) continue;
        w->water[i] = 3;
        if (outlet) {
            int exit=basins[slot].outlet;
            if (!w->river_width[exit]) w->river_width[exit]=(float)(qlcg_macro_band_width(flow[exit])*.5);
            if (w->river_level) w->river_level[exit]=level[slot];
        }
        if (r>=0 && w->water[r]==2) qlcg_lakes_attach_river(w,lake_id[r],r,i);
        if (w->river_level && r>=0 && (w->water[r]==1 || w->water[r]==2))
            w->river_level[i]=isfinite(w->river_level[i])?fminf(w->river_level[i],spill[i]):spill[i];
        w->river_width[i] = qlcg_macro_band_width(flow[i]) * 0.5;
    }

    for (int i = 0; i < n; i++) {
        w->flow[i] = flow[i];
        w->catchment[i] = catchment[i];
        w->spill[i] = spill[i];
        w->receiver[i] = receiver[i];
        w->rank[i] = rank[i];
    }

    w->hydrology.input = input;
    w->hydrology.lake_loss = lake_loss;
    w->hydrology.lake_rain = lake_rain;
    w->hydrology.lake_runoff = lake_runoff;
    w->hydrology.dry_loss = dry_loss;
    w->hydrology.river_loss = river_loss;
    for (int i = 0; i < n; i++) if (receiver[i] < 0) w->hydrology.output += flow[i];
    for (int b = 0; b < basin_count; b++) {
        if (status[b] == ST_DRY) w->hydrology.dry_basins++;
        if (status[b] == ST_SALT) w->hydrology.salt_basins++;
    }
    for (int k = 0; k < w->lake_count; k++) w->hydrology.plain_lakes += w->lakes[k].plain != 0;
    for (int k = 0; k < w->oasis_count; k++) if (oasis_shore[k] >= 0) w->oases[k].cell = oasis_shore[k];
    w->hydrology.valid = 1;
    if (w->main_river_width) {
        memcpy(w->main_river_width,w->river_width,(size_t)n*sizeof(float));
        for (int at=count-1;at>=0;at--) {
            int i=order[at],r=receiver[i];
            if (!w->main_river_width[i] || r<0 || w->water[r]==1 || w->water[r]==2 ||
                !(w->height[r]>0) || !(flow[r]>0)) continue;
            if (!w->main_river_width[r]) w->main_river_width[r]=(float)(qlcg_macro_band_width(flow[r])/2);
            if (w->river_level && isfinite(w->river_level[i])) {
                double distance=0;
                for (int k=0;k<8;k++) if (w->neighbors[i*8+k]==r) { distance=w->lengths[i*8+k]; break; }
                double grade=qlcg_river_friction_slope(flow[i],qlcg_river_discharge_width(flow[i]),
                    qlcg_macro_depth(flow[i]),w->hydro_friction?w->hydro_friction[i]:1);
                double drop=fmax(.02,grade*distance)/fmax(.01,w->height_scale);
                float cap=(float)fmax(0,fmin(w->river_level[i]-drop,spill[r]));
                if (!isfinite(w->river_level[r]) || cap<w->river_level[r]) w->river_level[r]=cap;
            }
        }
    }
    free(receiver); free(rank); free(order); free(spill); free(visited);
    free(flow); free(catchment); free(potential); free(root_slot);
    free(cell_slot); free(outlet_at); free(basins); free(parent);
    free(queue); free(status); free(level);
}
