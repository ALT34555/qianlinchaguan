/* 山脉：脊线→Kruskal 连链→分支→栅格化 */
#include "gen_mountain.h"
#include "../../../core/math_util.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define MR_TAU 6.283185307179586f
#define MR_MAX_NODES 4096
#define MR_MAX_RIDGES 256

typedef struct {
    int *cells;
    int  len;
    int  cap;
    int  kind;          /* 0 主脊 1 分支 */
    int  parent;
    int  start_joined;
    int  end_joined;
    int  pass;          /* -1 表示无 */
    float amplitude;
    float width;
} MrRidge;

typedef struct {
    int *items;
    int  count;
    int  cap;
} IntVec;

typedef struct {
    int cols, rows, width, size;
    IntVec *buckets;
} MrSpatial;

/* ---------- 基础 ---------- */

static float mr_clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

static float mr_smooth(float v) {
    float t = mr_clamp(v, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static int mr_wrap(int x, int w) {
    int r = x % w;
    return r < 0 ? r + w : r;
}

static float mr_delta(int x, int w) {
    return (float)(mr_wrap(x + w / 2, w) - w / 2);
}

static float mr_random(int i, uint32_t seed) {
    uint32_t v = (uint32_t)i ^ seed;
    v *= 0x45d9f3bu;
    v = (v ^ (v >> 16)) * 0x45d9f3bu;
    return (float)((double)(v ^ (v >> 16)) / 4294967296.0);
}

static float mr_latitude(const MacroWorld *g, int y) {
    float v = sinf(((float)y + 0.5f) * 3.14159265358979f / (float)g->rows);
    return fmaxf(0.22f, v);
}

static int mr_cell(const MacroWorld *g, int x, int y) {
    int yy = (int)lroundf((float)y);
    if (yy < 0) yy = 0;
    if (yy >= g->rows) yy = g->rows - 1;
    return yy * g->columns + mr_wrap(x, g->columns);
}

static float mr_distance(const MacroWorld *g, int a, int b) {
    int ay = a / g->columns, by = b / g->columns;
    float dx = mr_delta(b % g->columns - a % g->columns, g->columns) *
               mr_latitude(g, (ay + by) / 2);
    float dy = (float)(by - ay);
    return sqrtf(dx * dx + dy * dy);
}

/* ---------- 动态数组 ---------- */

static void iv_push(IntVec *v, int x) {
    if (v->count >= v->cap) {
        int nc = v->cap ? v->cap * 2 : 16;
        int *p = (int *)realloc(v->items, (size_t)nc * sizeof(int));
        if (!p) return;
        v->items = p;
        v->cap = nc;
    }
    v->items[v->count++] = x;
}

static void iv_free(IntVec *v) {
    free(v->items);
    v->items = NULL;
    v->count = v->cap = 0;
}

static void ridge_push(MrRidge *r, int cell) {
    if (r->len >= r->cap) {
        int nc = r->cap ? r->cap * 2 : 32;
        int *p = (int *)realloc(r->cells, (size_t)nc * sizeof(int));
        if (!p) return;
        r->cells = p;
        r->cap = nc;
    }
    if (r->len > 0 && r->cells[r->len - 1] == cell) return;
    r->cells[r->len++] = cell;
}

/* ------ 周期空间索引 ------ */

static void spatial_init(MrSpatial *sp, const MacroWorld *g, int size) {
    memset(sp, 0, sizeof(*sp));
    sp->size = size > 0 ? size : 1;
    sp->cols = g->columns / sp->size;
    if (sp->cols < 1) sp->cols = 1;
    sp->width = g->columns / sp->cols;
    if (sp->width < 1) sp->width = 1;
    sp->rows = (g->rows - 1) / sp->size + 1;
    sp->buckets = (IntVec *)calloc((size_t)(sp->cols * sp->rows), sizeof(IntVec));
}

static void spatial_free(MrSpatial *sp) {
    if (!sp->buckets) return;
    for (int i = 0; i < sp->cols * sp->rows; i++) iv_free(&sp->buckets[i]);
    free(sp->buckets);
    sp->buckets = NULL;
}

static void spatial_add_point(MrSpatial *sp, int cell_index, int value) {
    int col = (cell_index % (sp->width * sp->cols)) / sp->width;
    int row = cell_index / (sp->width * sp->cols);
    if (row < 0 || row >= sp->rows) return;
    if (col < 0 || col >= sp->cols) col = 0;
    iv_push(&sp->buckets[row * sp->cols + col], value);
}

/* ---------- 平滑与软顶 ---------- */

static void smooth_heights(const MacroWorld *g, float *heights, float weight) {
    int n = g->cell_count;
    float *src = (float *)malloc((size_t)n * sizeof(float));
    if (!src) return;
    memcpy(src, heights, (size_t)n * sizeof(float));
    for (int i = 0; i < n; i++) {
        float sum = 0.0f;
        for (int d = 0; d < 8; d++) sum += src[g->neighbors[i * 8 + d]];
        heights[i] = src[i] * (1.0f - weight) + sum * 0.125f * weight;
    }
    free(src);
}

static void soft_cap(float *heights, int n, float start, float span) {
    for (int i = 0; i < n; i++) {
        float h = heights[i];
        if (h > start) heights[i] = start + span * tanhf((h - start) / span);
    }
}

/* ---------- 节点与链 ---------- */

static int belt_nodes(const MacroWorld *g, const float *heights, const float *uplift,
                      const uint8_t *boundary, int gap, int *nodes, int cap) {
    int w = g->columns, h = g->rows;
    IntVec cand = {0};
    for (int i = 0; i < g->cell_count; i++) {
        if (heights[i] <= 0.0f || uplift[i] < 0.16f) continue;
        int x = i % w, y = i / w;
        float c = mr_latitude(g, y);
        float gx = (uplift[mr_cell(g, x + 1, y)] - uplift[mr_cell(g, x - 1, y)]) / c;
        float gy = uplift[mr_cell(g, x, y + 1)] - uplift[mr_cell(g, x, y - 1)];
        float len = hypotf(gx, gy);
        float ox = len > 1e-6f ? gx / len * (float)gap / c : 0.0f;
        float oy = len > 1e-6f ? gy / len * (float)gap : 0.0f;
        if (boundary[i] == 1 ||
            (uplift[i] >= uplift[mr_cell(g, x + (int)lroundf(ox), y + (int)lroundf(oy))] &&
             uplift[i] >= uplift[mr_cell(g, x - (int)lroundf(ox), y - (int)lroundf(oy))])) {
            iv_push(&cand, i);
        }
    }
    /* 按 uplift 降序，稳定 */
    for (int a = 1; a < cand.count; a++) {
        int v = cand.items[a], j = a - 1;
        while (j >= 0 && (uplift[cand.items[j]] < uplift[v])) {
            cand.items[j + 1] = cand.items[j];
            j--;
        }
        cand.items[j + 1] = v;
    }
    MrSpatial sp;
    spatial_init(&sp, g, gap);
    int count = 0;
    int limit = (int)lroundf((float)w * 1.4f);
    for (int k = 0; k < cand.count && count < cap && count < limit; k++) {
        int i = cand.items[k];
        int x = i % w, y = i / w;
        int rx = (int)((float)gap / mr_latitude(g, y)) + 2;
        int minX = (x - rx) / sp.width, maxX = (x + rx) / sp.width;
        int minY = y / sp.size, maxY = y / sp.size;
        int clash = 0;
        for (int gy = minY; gy <= maxY && !clash; gy++) {
            if (gy < 0 || gy >= sp.rows) continue;
            int span = maxX - minX + 1;
            if (span > sp.cols) span = sp.cols;
            for (int dx = 0; dx < span && !clash; dx++) {
                int key = gy * sp.cols + mr_wrap(minX + dx, sp.cols);
                IntVec *b = &sp.buckets[key];
                for (int q = 0; q < b->count; q++) {
                    if (mr_distance(g, i, b->items[q]) < (float)gap) { clash = 1; break; }
                }
            }
        }
        if (clash) continue;
        nodes[count++] = i;
        int col = (x / sp.width) % sp.cols;
        int row = y / sp.size;
        if (row >= 0 && row < sp.rows) iv_push(&sp.buckets[row * sp.cols + col], i);
    }
    spatial_free(&sp);
    iv_free(&cand);
    return count;
}

/* 链：并查集 + 最多 4 条最近邻边 */
typedef struct { int a, b; float score; } MrEdge;

static int uf_find(int *p, int k) {
    while (p[k] != k) { p[k] = p[p[k]]; k = p[k]; }
    return k;
}

static int cmp_edge(const void *x, const void *y) {
    const MrEdge *a = (const MrEdge *)x, *b = (const MrEdge *)y;
    if (a->score < b->score) return -1;
    if (a->score > b->score) return 1;
    if (a->a != b->a) return a->a - b->a;
    return a->b - b->b;
}

typedef struct {
    int  *cells;
    int   len;
    int   start_joined;
    int   end_joined;
    float level;
} MrChain;

static int belt_chains(const MacroWorld *g, const int *nodes, int node_count,
                       const float *heights, const float *uplift, int gap,
                       MrChain *chains, int chain_cap) {
    if (node_count < 2) return 0;
    int w = g->columns;
    MrEdge *edges = (MrEdge *)calloc((size_t)node_count * 4, sizeof(MrEdge));
    int ec = 0;
    for (int a = 0; a < node_count; a++) {
        int i = nodes[a], x = i % w, y = i / w;
        MrEdge near[4];
        int nc = 0;
        for (int b = 0; b < node_count; b++) {
            if (a == b) continue;
            int j = nodes[b];
            float len = mr_distance(g, i, j);
            if (len > (float)gap * 3.8f) continue;
            int dx = j % w - x, dy = j / w - y;
            int steps = (int)ceilf(fmaxf((float)abs(dx), (float)abs(dy)));
            if (steps < 1) steps = 1;
            int valid = 1;
            float strength = 0.0f;
            for (int s = 0; s <= steps; s++) {
                int k = mr_cell(g, x + (int)lroundf((float)dx * (float)s / (float)steps),
                                y + (int)lroundf((float)dy * (float)s / (float)steps));
                if (heights[k] <= 0.0f) { valid = 0; break; }
                strength += uplift[k];
            }
            if (!valid) continue;
            MrEdge e = { a, b, len / (0.4f + strength / (float)(steps + 1)) };
            /* 插入排序保留最近 4 条 */
            if (nc < 4) near[nc++] = e;
            else {
                int worst = 0;
                for (int q = 1; q < 4; q++) if (near[q].score > near[worst].score) worst = q;
                if (e.score < near[worst].score) near[worst] = e;
            }
        }
        for (int q = 0; q < nc; q++) edges[ec++] = near[q];
    }
    qsort(edges, (size_t)ec, sizeof(MrEdge), cmp_edge);

    int *parent = (int *)malloc((size_t)node_count * sizeof(int));
    int *adj_count = (int *)calloc((size_t)node_count, sizeof(int));
    int *adj = (int *)calloc((size_t)node_count * 3, sizeof(int));
    if (!parent || !adj_count || !adj) {
        free(parent); free(adj_count); free(adj); free(edges);
        return 0;
    }
    for (int i = 0; i < node_count; i++) parent[i] = i;

    for (int e = 0; e < ec; e++) {
        int a = edges[e].a, b = edges[e].b;
        if (uf_find(parent, a) == uf_find(parent, b)) continue;
        if (adj_count[a] >= 3 || adj_count[b] >= 3) continue;
        /* 角度拥挤：与已有边夹角 > 0.72 则拒绝 */
        int clash = 0;
        for (int dir = 0; dir < 2 && !clash; dir++) {
            int src = dir ? b : a, dst = dir ? a : b;
            int si = nodes[src], di = nodes[dst];
            int sy = si / w;
            float c = mr_latitude(g, sy);
            float dx = mr_delta(di % w - si % w, w) * c;
            float dy = (float)(di / w - si / w);
            float dl = hypotf(dx, dy);
            for (int q = 0; q < adj_count[src]; q++) {
                int qi = nodes[adj[src * 3 + q]];
                float qx = mr_delta(qi % w - si % w, w) * c;
                float qy = (float)(qi / w - si / w);
                float ql = hypotf(qx, qy);
                if (ql < 1e-6f || dl < 1e-6f) continue;
                if ((qx * dx + qy * dy) / ql / dl > 0.72f) { clash = 1; break; }
            }
        }
        if (clash) continue;
        parent[uf_find(parent, a)] = uf_find(parent, b);
        adj[a * 3 + adj_count[a]++] = b;
        adj[b * 3 + adj_count[b]++] = a;
    }

    /* 抽链 */
    int *used = (int *)calloc((size_t)node_count * node_count, sizeof(int));
    int chain_count = 0;
    if (used) {
        for (int start = 0; start < node_count && chain_count < chain_cap; start++) {
            if (adj_count[start] == 2) continue;
            for (int f = 0; f < adj_count[start] && chain_count < chain_cap; f++) {
                int first = adj[start * 3 + f];
                int key = (start < first ? start : first) * node_count + (start > first ? start : first);
                if (used[key]) continue;
                MrChain *ch = &chains[chain_count];
                memset(ch, 0, sizeof(*ch));
                ch->cells = (int *)malloc((size_t)node_count * sizeof(int));
                if (!ch->cells) continue;
                int prev = start, cur = first;
                ch->cells[ch->len++] = nodes[start];
                for (;;) {
                    int k2 = (prev < cur ? prev : cur) * node_count + (prev > cur ? prev : cur);
                    used[k2] = 1;
                    ch->cells[ch->len++] = nodes[cur];
                    if (adj_count[cur] != 2) break;
                    int next = adj[cur * 3 + 0] != prev ? adj[cur * 3 + 0] : adj[cur * 3 + 1];
                    prev = cur;
                    cur = next;
                    if (ch->len >= node_count) break;
                }
                ch->start_joined = adj_count[start] > 1;
                ch->end_joined = adj_count[cur] > 1;
                float sum = 0.0f;
                for (int q = 0; q < ch->len; q++) sum += uplift[ch->cells[q]];
                ch->level = sum / (float)ch->len;
                chain_count++;
            }
        }
        free(used);
    }
    free(parent); free(adj_count); free(adj); free(edges);

    /* 按 level 降序、起点升序 */
    for (int a = 1; a < chain_count; a++) {
        MrChain tmp = chains[a];
        int j = a - 1;
        while (j >= 0 && (chains[j].level < tmp.level ||
               (chains[j].level == tmp.level && chains[j].cells[0] > tmp.cells[0]))) {
            chains[j + 1] = chains[j];
            j--;
        }
        chains[j + 1] = tmp;
    }
    return chain_count;
}

/* 补齐相邻格 */
static int densify(const MacroWorld *g, const int *nodes, int node_count, int *out, int cap) {
    if (node_count <= 0 || cap <= 0) return 0;
    int len = 0;
    out[len++] = nodes[0];
    int w = g->columns;
    for (int k = 1; k < node_count; k++) {
        int a = nodes[k - 1], b = nodes[k];
        int x = a % w, y = a / w;
        int dx = b % w - x, dy = b / w - y;
        int steps = (int)ceilf(fmaxf((float)abs(dx), (float)abs(dy)));
        if (steps < 1) steps = 1;
        for (int s = 1; s <= steps && len < cap; s++) {
            int i = mr_cell(g, x + (int)lroundf((float)dx * (float)s / (float)steps),
                            y + (int)lroundf((float)dy * (float)s / (float)steps));
            if (len == 0 || out[len - 1] != i) out[len++] = i;
        }
    }
    return len;
}

/* 内侧方向：高的一侧 */
static void inland(const MacroWorld *g, const float *heights, int i,
                   float tx, float ty, const float *pushX, const float *pushY, float scale,
                   float *out_x, float *out_y) {
    int w = g->columns, x = i % w, y = i / w;
    float c = mr_latitude(g, y);
    float nx = -ty, ny = tx;
    float s_pos = 0.0f, s_neg = 0.0f;
    int steps[3] = { 5, 12, 22 };
    for (int q = 0; q < 3; q++) {
        int s = steps[q];
        s_pos += heights[mr_cell(g, x + (int)lroundf(nx * (float)s * scale / c), y + (int)lroundf(ny * (float)s * scale))];
        s_neg += heights[mr_cell(g, x - (int)lroundf(nx * (float)s * scale / c), y - (int)lroundf(ny * (float)s * scale))];
    }
    if (s_neg > s_pos) { nx = -nx; ny = -ny; }
    float px = pushX ? pushX[i] : 0.0f;
    float py = pushY ? pushY[i] : 0.0f;
    float pn = hypotf(px, py);
    if (pn > 1e-6f) {
        float sign = (px * nx + py * ny < 0.0f) ? -1.0f : 1.0f;
        nx = nx * 0.8f + px / pn * 0.2f * sign;
        ny = ny * 0.8f + py / pn * 0.2f * sign;
    }
    float len = hypotf(nx, ny);
    if (len < 1e-6f) len = 1.0f;
    *out_x = nx / len;
    *out_y = ny / len;
}

/* 分支：从主脊连续延伸，遇海即断 */
static int branch(const MacroWorld *g, const float *heights, int root,
                  float angle, float length, uint32_t seed, float bend,
                  int *out, int cap) {
    int w = g->columns;
    float x = (float)(root % w), y = (float)(root / w);
    int len = 0;
    out[len++] = root;
    float phase = mr_random(root, seed) * MR_TAU;
    int total = (int)ceilf(length * 2.0f);
    for (int s = 1; s <= total && len < cap - 64; s++) {
        float t = (float)s / (float)(total > 0 ? total : 1);
        float direction = angle + bend * t + 0.13f * sinf(t * 5.0f + phase);
        float nx = x + cosf(direction) * 0.5f / mr_latitude(g, (int)y);
        float ny = y + sinf(direction) * 0.5f;
        if (ny < 1.0f || ny >= (float)(g->rows - 1)) break;
        int seg[64];
        int pair[2] = { mr_cell(g, (int)lroundf(x), (int)lroundf(y)),
                        mr_cell(g, (int)lroundf(nx), (int)lroundf(ny)) };
        int segn = densify(g, pair, 2, seg, 64);
        int blocked = 0;
        for (int q = 0; q < segn; q++) if (heights[seg[q]] <= 0.0f) { blocked = 1; break; }
        if (blocked) break;
        for (int q = 0; q < segn && len < cap; q++) {
            if (len == 0 || out[len - 1] != seg[q]) out[len++] = seg[q];
        }
        x = nx; y = ny;
    }
    return len;
}

/* 横截面核：按宽度与纬度取缓存 */
typedef struct { float radius, width, c; int *offs; float *vals; int n; } MrKernel;

static int build_kernel(MrKernel *k, float width, float c) {
    k->width = width;
    k->radius = width * 2.8f;
    k->c = c;
    int rx = (int)ceilf(k->radius / c), ry = (int)ceilf(k->radius);
    int cap = (2 * rx + 1) * (2 * ry + 1);
    k->offs = (int *)malloc((size_t)cap * 2 * sizeof(int));
    k->vals = (float *)malloc((size_t)cap * sizeof(float));
    k->n = 0;
    if (!k->offs || !k->vals) return 0;
    for (int dy = -ry; dy <= ry; dy++) {
        for (int dx = -rx; dx <= rx; dx++) {
            float d = hypotf((float)dx * c, (float)dy);
            if (d > k->radius) continue;
            float q = d / width;
            float value = 0.78f * expf(-q * q * 2.0f) + 0.22f * expf(-q * q * 0.35f);
            k->offs[k->n * 2 + 0] = dx;
            k->offs[k->n * 2 + 1] = dy;
            k->vals[k->n] = value;
            k->n++;
        }
    }
    return 1;
}

static void free_kernel(MrKernel *k) {
    free(k->offs);
    free(k->vals);
    k->offs = NULL;
    k->vals = NULL;
    k->n = 0;
}

static void raster_ridges(const MacroWorld *g, MrRidge *ridges, int ridge_count,
                          float *field, const float *heights) {
    MrKernel cache[16];
    int cache_n = 0;
    for (int r = 0; r < ridge_count; r++) {
        MrRidge *rd = &ridges[r];
        int total = rd->len > 1 ? rd->len - 1 : 1;
        float width = rd->width;
        for (int s = 0; s < rd->len; s++) {
            int i = rd->cells[s];
            int x = i % g->columns, y = i / g->columns;
            float along = (float)s / (float)total;
            float c = roundf(mr_latitude(g, y) * 16.0f) / 16.0f;
            MrKernel *k = NULL;
            for (int q = 0; q < cache_n; q++) {
                if (fabsf(cache[q].width - width) < 1e-3f && fabsf(cache[q].c - c) < 1e-3f) {
                    k = &cache[q];
                    break;
                }
            }
            if (!k) {
                if (cache_n >= 16) { k = &cache[0]; free_kernel(k); }
                else k = &cache[cache_n++];
                if (!build_kernel(k, width, c)) continue;
            }
            int start_joined = rd->start_joined, end_joined = rd->end_joined;
            float taper = fminf(start_joined ? 1.0f : mr_smooth(along / 0.12f),
                                end_joined ? 1.0f : mr_smooth((1.0f - along) / 0.18f));
            float envelope = (rd->kind == 1) ? (1.0f - 0.65f * mr_smooth(along))
                                             : (0.88f + 0.12f * sinf(3.14159265f * along));
            float pass = 1.0f;
            if (rd->pass >= 0) {
                float u = fabsf((float)(s - rd->pass)) / fmaxf(2.0f, width * 1.8f / c);
                if (u < 1.0f) pass = 0.46f + 0.54f * mr_smooth(u);
            }
            float amplitude = rd->amplitude * taper * envelope * pass;
            for (int q = 0; q < k->n; q++) {
                int yy = y + k->offs[q * 2 + 1];
                if (yy < 0 || yy >= g->rows) continue;
                int xx = x + k->offs[q * 2];
                if (xx < 0) xx += g->columns;
                else if (xx >= g->columns) xx -= g->columns;
                int j = yy * g->columns + xx;
                if (heights[j] <= 0.0f) continue;
                float value = amplitude * k->vals[q];
                if (value > field[j]) field[j] = value;
            }
        }
    }
    for (int q = 0; q < cache_n; q++) free_kernel(&cache[q]);
}

/* 高原/盆地地貌 */
static void raster_landform(const MacroWorld *g, const float *form, float *field,
                            const float *heights) {
    float x = form[0], y = form[1], tx = form[2], ty = form[3];
    float nx = form[4], ny = form[5], length = form[6], width = form[7];
    float amplitude = form[8];
    int kind = (int)form[9];
    float c = mr_latitude(g, (int)y);
    float radius = hypotf(length, width);
    int rx = (int)ceilf(radius / c), ry = (int)ceilf(radius);
    for (int dy = -ry; dy <= ry; dy++) {
        int yy = (int)lroundf(y) + dy;
        if (yy < 0 || yy >= g->rows) continue;
        for (int dx = -rx; dx <= rx; dx++) {
            int xx = (int)lroundf(x) + dx;
            float px = mr_delta(xx - (int)lroundf(x), g->columns) * c;
            float py = (float)dy;
            float u = (px * tx + py * ty) / length;
            float v = (px * nx + py * ny) / width;
            float r = hypotf(u, v);
            if (r >= 1.0f) continue;
            int i = mr_cell(g, xx, yy);
            if (heights[i] <= 0.0f) continue;
            float edge = 1.0f - mr_smooth((r - 0.48f) / 0.52f);
            float profile = (kind == 0) ? edge : (0.10f + 0.85f * mr_smooth(r / 0.72f)) * edge;
            float outlet = (kind == 2)
                ? 1.0f - 0.85f * (1.0f - mr_smooth(fabsf(u) / 0.3f)) * mr_smooth((v + 0.1f) / 0.8f)
                : 1.0f;
            float value = amplitude * profile * outlet;
            if (value > field[i]) field[i] = value;
        }
    }
}

float qlcg_build_mountain_ridges(MacroWorld *w) {
    if (!w) return 0.0f;
    int n = w->cell_count;
    float scale = mr_clamp((float)w->columns / 1024.0f, 0.3f, 1.0f);
    int gap = (int)fmaxf(2.0f, 6.0f * scale);

    int *nodes = (int *)malloc((size_t)MR_MAX_NODES * sizeof(int));
    MrChain *chains = (MrChain *)calloc(512, sizeof(MrChain));
    MrRidge *ridges = (MrRidge *)calloc(MR_MAX_RIDGES, sizeof(MrRidge));
    float *ridge_field = (float *)calloc((size_t)n, sizeof(float));
    float *region_field = (float *)calloc((size_t)n, sizeof(float));
    if (!nodes || !chains || !ridges || !ridge_field || !region_field) {
        free(nodes); free(chains); free(ridges); free(ridge_field); free(region_field);
        return 0.0f;
    }

    int node_count = belt_nodes(w, w->height, w->uplift, w->boundary, gap, nodes, MR_MAX_NODES);
    int chain_count = belt_chains(w, nodes, node_count, w->height, w->uplift, gap, chains, 512);

    int ridge_count = 0;
    int budget = w->columns * 10;
    for (int ci = 0; ci < chain_count && ridge_count < MR_MAX_RIDGES; ci++) {
        MrChain *ch = &chains[ci];
        int *path = (int *)malloc((size_t)(MR_MAX_NODES * 4) * sizeof(int));
        if (!path) continue;
        int path_len = densify(w, ch->cells, ch->len, path, MR_MAX_NODES * 4);
        if (path_len < 4) { free(path); continue; }

        float amplitude = roundf(600.0f + 1050.0f * mr_clamp(ch->level, 0.0f, 1.0f));
        float width = roundf(fmaxf(1.2f, (2.2f + ch->level * 0.7f) * scale) * 4.0f) / 4.0f;
        int pass = -1;
        if ((float)path_len > 24.0f * scale) {
            pass = (int)floorf((float)path_len * (0.36f + 0.28f * mr_random(path[0], w->opt.seed)));
        }
        MrRidge *rd = &ridges[ridge_count++];
        rd->cells = path;
        rd->len = path_len;
        rd->cap = MR_MAX_NODES * 4;
        rd->kind = 0;
        rd->parent = -1;
        rd->start_joined = ch->start_joined;
        rd->end_joined = ch->end_joined;
        rd->pass = pass;
        rd->amplitude = amplitude;
        rd->width = width;

        int mid = path[path_len / 2];
        int first = path[(int)((float)path_len * 0.2f)];
        int last = path[(int)((float)path_len * 0.8f)];
        int yy = mid / w->columns;
        float c = mr_latitude(w, yy);
        float dx = mr_delta(last % w->columns - first % w->columns, w->columns) * c;
        float dy = (float)(last / w->columns - first / w->columns);
        float span = hypotf(dx, dy);
        if (span < 12.0f * scale || budget <= 0) continue;

        float tx = dx / span, ty = dy / span;
        float nx = 0.0f, ny = 0.0f;
        inland(w, w->height, mid, tx, ty, w->ridge_push_e, w->ridge_push_n, scale, &nx, &ny);

        float arm_len = fminf(42.0f * scale, fmaxf(15.0f * scale, span * 0.65f));
        int arms = 0;
        int count = span > 40.0f * scale ? 3 : 2;
        int arm_first[3] = {0}, arm_last[3] = {0};
        for (int k = 0; k < count && budget > 0; k++) {
            int at = (int)lroundf((0.2f + 0.6f * (float)k / (float)(count - 1)) * (float)(path_len - 1));
            int root = path[at];
            float angle = atan2f(ny, nx) + ((float)k / (float)(count - 1) - 0.5f) * 0.45f;
            int *acells = (int *)malloc((size_t)(MR_MAX_NODES * 4) * sizeof(int));
            if (!acells) continue;
            int alen = branch(w, w->height, root, angle, fminf(arm_len, (float)budget),
                              w->opt.seed, (0.5f - (float)k / (float)(count - 1)) * 0.35f,
                              acells, MR_MAX_NODES * 4);
            if (alen < 4) { free(acells); continue; }
            budget -= alen;
            arms++;
            arm_first[k] = acells[0];
            arm_last[k] = acells[alen - 1];
            if (ridge_count < MR_MAX_RIDGES) {
                MrRidge *br = &ridges[ridge_count++];
                br->cells = acells;
                br->len = alen;
                br->cap = MR_MAX_NODES * 4;
                br->kind = 1;
                br->parent = ridge_count - 2;
                br->start_joined = 1;
                br->end_joined = 0;
                br->pass = -1;
                br->amplitude = roundf(amplitude * 0.8f);
                br->width = fmaxf(1.0f, roundf(width * 0.72f * 4.0f) / 4.0f);
            } else {
                free(acells);
            }
        }
        if (arms < 2) continue;

        float reach = 1e9f;
        for (int k = 0; k < arms; k++) {
            float d = mr_distance(w, arm_first[k], arm_last[k]);
            if (d < reach) reach = d;
        }
        if (reach < 8.0f * scale) continue;

        float form[10];
        form[0] = (float)mr_wrap((int)lroundf((float)(mid % w->columns) + nx * reach * 0.42f / c), w->columns);
        form[1] = (float)yy + ny * reach * 0.42f;
        form[2] = tx; form[3] = ty; form[4] = nx; form[5] = ny;
        form[6] = span * 0.64f;
        form[7] = reach * 0.68f;
        form[8] = roundf(amplitude * 0.52f);
        form[9] = (mr_random(mid, w->opt.seed ^ 0x706c6174u) > 0.48f) ? 0.0f : 2.0f;
        raster_landform(w, form, region_field, w->height);
    }

    raster_ridges(w, ridges, ridge_count, ridge_field, w->height);

    float peak = 0.0f;
    for (int i = 0; i < n; i++) {
        float add = ridge_field[i] + region_field[i];
        if (add > peak) peak = add;
        w->height[i] += add;
    }
    soft_cap(w->height, n, 3000.0f, 300.0f);
    smooth_heights(w, w->height, 0.16f);

    for (int r = 0; r < ridge_count; r++) free(ridges[r].cells);
    for (int c = 0; c < chain_count; c++) free(chains[c].cells);
    free(nodes); free(chains); free(ridges);
    free(ridge_field); free(region_field);
    return peak;
}
