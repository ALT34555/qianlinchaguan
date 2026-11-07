/* 地形配方：沿地形窗口叠加高斯抬升/下沉 */
#include "gen_recipes.h"
#include "../../../core/math_util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RECIPE_MAX_OPS 24

typedef struct {
    float center[3], axis[3], side[3];
    float len_long, len_wide, amplitude;
} RecipeOp;

typedef struct { int idx; float val; } RecipeCand;

static float rand01(uint32_t *s) {
    uint32_t t = (*s += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    uint32_t a = t ^ (t >> 7);
    uint32_t b = a * (t | 61u);
    t = t ^ (t + b);
    return (float)((double)(t ^ (t >> 14)) / 4294967296.0);
}

static void sph_point(float lon, float y, float out[3]) {
    float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
    out[0] = r * cosf(lon);
    out[1] = y;
    out[2] = r * sinf(lon);
}

static void cross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void unit3(float v[3]) {
    float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n < 1e-9f) n = 1.0f;
    v[0] /= n; v[1] /= n; v[2] /= n;
}

static int cmp_cand(const void *x, const void *y) {
    const RecipeCand *a = (const RecipeCand *)x, *b = (const RecipeCand *)y;
    if (a->val > b->val) return -1;
    if (a->val < b->val) return 1;
    return a->idx - b->idx;
}

void qlcg_apply_recipes(MacroWorld *w) {
    if (!w) return;
    int n = w->cell_count;
    uint32_t rng = w->opt.seed ^ 0x72656369u;
    double area = 0.0;
    for (int i = 0; i < n; i++) area += w->areas[i];

    const float *src = NULL;
    if (w->opt.terrain_recipe == TERRAIN_MOUNTAINS) src = w->uplift;
    else if (w->opt.terrain_recipe == TERRAIN_RIFT) src = w->rift;

    int *focus = NULL;
    int focus_n = 0;
    float *weights = NULL;
    double focus_area = 0.0;

    if (src) {
        double target = area / 8.0;
        const int B = 2048;
        float maxv = 0.0f;
        for (int i = 0; i < n; i++) if (src[i] > maxv) maxv = src[i];
        if (maxv > 0.0f) {
            double *b_area = (double *)calloc(B, sizeof(double));
            float scale = (float)(B - 1) / maxv;
            if (b_area) {
                for (int i = 0; i < n; i++) {
                    int b = (int)(src[i] * scale);
                    if (b < 0) b = 0;
                    if (b >= B) b = B - 1;
                    b_area[b] += w->areas[i];
                }
                double acc = 0.0;
                int cut = B - 1;
                for (; cut >= 0; cut--) { acc += b_area[cut]; if (acc >= target) break; }
                float cutoff = (cut > 0) ? (float)cut / scale : 0.0f;
                RecipeCand *cand = (RecipeCand *)malloc((size_t)n * sizeof(RecipeCand));
                if (cand) {
                    for (int i = 0; i < n; i++) {
                        if (src[i] >= cutoff) {
                            cand[focus_n].idx = i;
                            cand[focus_n].val = src[i];
                            focus_n++;
                        }
                    }
                    qsort(cand, (size_t)focus_n, sizeof(RecipeCand), cmp_cand);
                    focus = (int *)malloc((size_t)focus_n * sizeof(int));
                    weights = (float *)malloc((size_t)focus_n * sizeof(float));
                    if (focus && weights) {
                        for (int k = 0; k < focus_n; k++) {
                            focus[k] = cand[k].idx;
                            focus_area += w->areas[cand[k].idx];
                            weights[k] = (float)focus_area;
                        }
                    } else {
                        free(focus); free(weights);
                        focus = NULL; weights = NULL; focus_n = 0;
                    }
                    free(cand);
                }
                free(b_area);
            }
        } else {
            focus = (int *)malloc((size_t)n * sizeof(int));
            weights = (float *)malloc((size_t)n * sizeof(float));
            if (focus && weights) {
                focus_n = n;
                for (int k = 0; k < n; k++) {
                    focus[k] = k;
                    focus_area += w->areas[k];
                    weights[k] = (float)focus_area;
                }
            } else {
                free(focus); free(weights);
                focus = NULL; weights = NULL; focus_n = 0;
            }
        }
    }

    RecipeOp ops[RECIPE_MAX_OPS];
    int op_count = 0;
    int count = (w->opt.terrain_recipe == TERRAIN_ARCHIPELAGO) ? 24 : 12;
    for (int k = 0; k < count; k++) {
        float center[3];
        if (focus && focus_n > 0 && focus_area > 0.0f) {
            float at = rand01(&rng) * (float)focus_area;
            int lo = 0, hi = focus_n - 1;
            while (lo < hi) {
                int mid = (lo + hi) >> 1;
                if (weights[mid] < at) lo = mid + 1;
                else hi = mid;
            }
            int idx = focus[lo];
            center[0] = w->points[idx * 3];
            center[1] = w->points[idx * 3 + 1];
            center[2] = w->points[idx * 3 + 2];
        } else {
            float lon = rand01(&rng) * 6.2831853f;
            float lat = asinf(qlcg_clampf(rand01(&rng) * 2.0f - 1.0f, -1.0f, 1.0f));
            sph_point(lon, lat, center);
        }
        float a_lon = rand01(&rng) * 6.2831853f;
        float a_lat = asinf(qlcg_clampf(rand01(&rng) * 2.0f - 1.0f, -1.0f, 1.0f));
        float a_pt[3];
        sph_point(a_lon, a_lat, a_pt);
        float axis[3];
        cross3(center, a_pt, axis);
        unit3(axis);
        float side[3];
        cross3(center, axis, side);
        int arch = (w->opt.terrain_recipe == TERRAIN_ARCHIPELAGO);
        float len_long = arch ? (0.05f + rand01(&rng) * 0.09f) : (0.12f + rand01(&rng) * 0.22f);
        float amplitude;
        if (w->opt.terrain_recipe == TERRAIN_RIFT) {
            amplitude = -150.0f - rand01(&rng) * 200.0f;
        } else {
            float sign = (k % 4 == 0) ? -1.0f : 1.0f;
            amplitude = sign * (70.0f + rand01(&rng) * 240.0f);
        }
        RecipeOp *op = &ops[op_count++];
        memcpy(op->center, center, sizeof(center));
        memcpy(op->axis, axis, sizeof(axis));
        memcpy(op->side, side, sizeof(side));
        op->len_long = len_long;
        op->len_wide = len_long * (0.25f + rand01(&rng) * 0.35f);
        op->amplitude = amplitude;
    }

    float op_hr[RECIPE_MAX_OPS];
    for (int k = 0; k < op_count; k++) {
        op_hr[k] = hypotf(ops[k].center[0], ops[k].center[2]);
    }

    for (int y = 0; y < w->rows; y++) {
        int row = y * w->columns;
        float py = w->points[row * 3 + 1];
        float cos_lat = sqrtf(fmaxf(0.0f, 1.0f - py * py));
        int active[RECIPE_MAX_OPS];
        int act_n = 0;
        for (int k = 0; k < op_count; k++) {
            if (py * ops[k].center[1] + cos_lat * op_hr[k] + 1e-6f >= 0.75f) active[act_n++] = k;
        }
        if (!act_n) continue;
        for (int x = 0; x < w->columns; x++) {
            int i = row + x;
            float px = w->points[i * 3];
            float pz = w->points[i * 3 + 2];
            float change = 0.0f;
            for (int a = 0; a < act_n; a++) {
                const RecipeOp *op = &ops[active[a]];
                const float *c = op->center;
                if (px * c[0] + py * c[1] + pz * c[2] < 0.75f) continue;
                float ux = (px * op->axis[0] + py * op->axis[1] + pz * op->axis[2]) / op->len_long;
                float uy = (px * op->side[0] + py * op->side[1] + pz * op->side[2]) / op->len_wide;
                change += op->amplitude * expf(-2.0f * (ux * ux + uy * uy));
            }
            float basin = w->basin ? w->basin[i] : 0.0f;
            w->height[i] += qlcg_clampf(change, -400.0f, 400.0f) * (1.0f - 0.9f * basin);
        }
    }

    free(focus);
    free(weights);
}
