/* 构造：warp → 板块漂移 → 碰撞累积 → 高度合成 */
#include "gen_tectonics.h"
#include "gen_plates.h"
#include "gen_crust.h"
#include "gen_noise.h"
#include "../world_internal.h"
#include "../../../core/math_util.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static float smooth01(float v) {
    v = qlcg_clampf(v, 0.0f, 1.0f);
    return v * v * (3.0f - 2.0f * v);
}

/* 指数衰减扩散（对齐 base 的 spread） */
void qlcg_spread_tectonic(const MacroWorld *w, const float *src, float width, float *out) {
    int n = w->cell_count;
    memcpy(out, src, (size_t)n * sizeof(float));
    MinHeap heap;
    qlcg_heap_init(&heap, n * 2);
    for (int i = 0; i < n; i++) {
        if (out[i] > 0.0f) qlcg_heap_push(&heap, i, -out[i]);
    }
    int32_t idx;
    float key;
    while (qlcg_heap_pop(&heap, &idx, &key)) {
        float v = -key;
        if (v < out[idx] - 1e-6f) continue;
        for (int d = 0; d < 4; d++) {
            int j = w->neighbors[idx * 8 + d];
            float next = v * expf(-w->lengths[idx * 8 + d] / width);
            if (next > 0.025f && next > out[j] + 1e-5f) {
                out[j] = next;
                qlcg_heap_push(&heap, j, -next);
            }
        }
    }
    qlcg_heap_free(&heap);
}

void qlcg_gen_tectonics(MacroWorld *w) {
    if (!w) return;
    if(qlcg_world_flat(w)) { qlcg_gen_planar(w); return; }
    int n = w->cell_count;
    int major = 0, minor = 0;
    qlcg_planet_size(w->opt.columns, &major, &minor);
    w->plate_count = qlcg_create_plates(w->plates, MAX_PLATES, &w->opt, major, minor);
    DriftProfile P = qlcg_drift_profile(w->opt.tectonic_activity);
    for (int p = 0; p < w->plate_count; p++) {
        w->plates[p].speed = 1.0f + (w->plates[p].speed - 1.0f) * (P.dispersion / 0.35f);
    }

    /* 大陆壳场决定洋陆归属 */
    CrustField cf;
    qlcg_crust_build(&cf, w, w->plates, w->plate_count, &w->opt, P.span);
    for (int p = 0; p < w->plate_count; p++) {
        float cont = qlcg_crust_continental_xyz(&cf, w->plates[p].start[0], w->plates[p].start[1],
                                                w->plates[p].start[2], &w->plates[p], 0.0f);
        w->plates[p].oceanic = cont < 0.5f;
    }

    /* 球面 warp，避免板块边界过于笔直 */
    float *warped = (float *)malloc((size_t)n * 3 * sizeof(float));
    uint16_t *ids = (uint16_t *)malloc((size_t)n * sizeof(uint16_t));
    float *uplift_src = (float *)calloc((size_t)n, sizeof(float));
    float *rift_src = (float *)calloc((size_t)n, sizeof(float));
    float *shear_src = (float *)calloc((size_t)n, sizeof(float));
    float *uplift = (float *)calloc((size_t)n, sizeof(float));
    float *rift = (float *)calloc((size_t)n, sizeof(float));
    float *shear = (float *)calloc((size_t)n, sizeof(float));
    if (!warped || !ids || !uplift_src || !rift_src || !shear_src || !uplift || !rift || !shear) {
        free(warped); free(ids); free(uplift_src); free(rift_src); free(shear_src);
        free(uplift); free(rift); free(shear);
        return;
    }

    for (int i = 0; i < n; i++) {
        const float *pt = &w->points[i * 3];
        float sx = pt[0] * 3.6f, sy = pt[1] * 3.6f - 7.0f, sz = pt[2] * 3.6f + 11.0f;
        float qx = pt[0] + qlcg_base_noise(sx, sy, sz, w->opt.seed) * 0.10f;
        float qy = pt[1] + qlcg_base_noise(sx + 19.0f, sy, sz, w->opt.seed) * 0.10f;
        float qz = pt[2] + qlcg_base_noise(sx + 38.0f, sy, sz, w->opt.seed) * 0.10f;
        float len = sqrtf(qx * qx + qy * qy + qz * qz);
        if (len < 1e-6f) len = 1.0f;
        warped[i * 3 + 0] = qx / len;
        warped[i * 3 + 1] = qy / len;
        warped[i * 3 + 2] = qz / len;
    }

    float angle = P.span;
    int steps = (int)roundf(P.steps);

    for (int step = 0; step <= steps; step++) {
        float fraction = (float)step / (float)(steps > 0 ? steps : 1);
        bool is_last = (step == steps);
        for (int p = 0; p < w->plate_count; p++) {
            float out[3];
            rotate_vec3(w->plates[p].start, w->plates[p].axis, angle * w->plates[p].speed * fraction, out);
            memcpy(w->plates[p].center, out, sizeof(out));
        }
        qlcg_assign_plate_ids(warped, w->plates, w->plate_count, major, ids, n);
        if (step == 0 || angle == 0.0f) continue;

        for (int i = 0; i < n; i++) {
            /* 邻域 0 = 东、2 = 北向行（两侧都要算） */
            for (int k = 0; k < 2; k++) {
                int dir = (k == 0) ? 0 : 2;
                int j = w->neighbors[i * 8 + dir];
                if (ids[i] == ids[j]) continue;

                const TectonicPlate *a = &w->plates[ids[i]];
                const TectonicPlate *b = &w->plates[ids[j]];
                float ax = w->points[i * 3], ay = w->points[i * 3 + 1], az = w->points[i * 3 + 2];
                float bx = w->points[j * 3], by = w->points[j * 3 + 1], bz = w->points[j * 3 + 2];

                float mx = ax + bx, my = ay + by, mz = az + bz;
                float ml = sqrtf(mx * mx + my * my + mz * mz);
                if (ml < 1e-6f) ml = 1.0f;
                mx /= ml; my /= ml; mz /= ml;
                float nx = bx - ax, ny = by - ay, nz = bz - az;
                float nl = sqrtf(nx * nx + ny * ny + nz * nz);
                if (nl < 1e-6f) nl = 1.0f;
                nx /= nl; ny /= nl; nz /= nl;

                float vax = a->axis[1] * mz - a->axis[2] * my;
                float vay = a->axis[2] * mx - a->axis[0] * mz;
                float vaz = a->axis[0] * my - a->axis[1] * mx;
                float vbx = b->axis[1] * mz - b->axis[2] * my;
                float vby = b->axis[2] * mx - b->axis[0] * mz;
                float vbz = b->axis[0] * my - b->axis[1] * mx;
                float relx = (vax * a->speed - vbx * b->speed) * P.speed;
                float rely = (vay * a->speed - vby * b->speed) * P.speed;
                float relz = (vaz * a->speed - vbz * b->speed) * P.speed;

                float push = relx * nx + rely * ny + relz * nz;
                float cx = my * nz - mz * ny, cy = mz * nx - mx * nz, cz = mx * ny - my * nx;
                float slide = fabsf(relx * cx + rely * cy + relz * cz);
                float convergence = qlcg_clampf((push - 0.08f) / 1.25f, 0.0f, 1.0f);
                float divergence = qlcg_clampf((-push - 0.08f) / 1.25f, 0.0f, 1.0f);
                float sh = qlcg_clampf((slide - 0.12f) / 1.6f, 0.0f, 1.0f);

                float cont_i = qlcg_crust_continental_xyz(&cf, ax, ay, az, a, fraction);
                float cont_j = qlcg_crust_continental_xyz(&cf, bx, by, bz, b, fraction);
                float up_i = convergence * (0.60f + 0.85f * cont_i) + divergence * (1.0f - cont_i) * 0.38f;
                float up_j = convergence * (0.60f + 0.85f * cont_j) + divergence * (1.0f - cont_j) * 0.38f;
                float rf_i = divergence * (0.25f + 0.75f * cont_i) + convergence * (1.0f - cont_i) * 0.58f;
                float rf_j = divergence * (0.25f + 0.75f * cont_j) + convergence * (1.0f - cont_j) * 0.58f;

                if (up_i > uplift_src[i]) uplift_src[i] = up_i;
                if (up_j > uplift_src[j]) uplift_src[j] = up_j;
                if (rf_i > rift_src[i]) rift_src[i] = rf_i;
                if (rf_j > rift_src[j]) rift_src[j] = rf_j;
                if (sh > shear_src[i]) shear_src[i] = sh;
                if (sh > shear_src[j]) shear_src[j] = sh;

                if (is_last) {
                    uint8_t type = (convergence > 0.12f) ? 1 : (divergence > 0.12f) ? 2
                                 : (sh > 0.12f) ? 3 : 4;
                    w->boundary[i] = type;
                    w->boundary[j] = type;
                    if (type == 1) {
                        /* 球面切向挤压的东/北分量，供山脉脊线使用 */
                        float along = relx * mx + rely * my + relz * mz;
                        float tx = relx - along * mx;
                        float ty = rely - along * my;
                        float tz = relz - along * mz;
                        float el = sqrtf(mx * mx + mz * mz);
                        if (el < 1e-6f) el = 1e-6f;
                        float ex = -mz / el, ez = mx / el;
                        float nx2 = -my * mx / el, ny2 = el, nz2 = -my * mz / el;
                        float east = tx * ex + tz * ez;
                        float north = tx * nx2 + ty * ny2 + tz * nz2;
                        w->ridge_push_e[i] = -east;
                        w->ridge_push_n[i] = north;
                        w->ridge_push_e[j] = east;
                        w->ridge_push_n[j] = -north;
                    }
                }
            }
        }
    }

    qlcg_spread_tectonic(w, uplift_src, w->radius * P.belt_width, uplift);
    qlcg_spread_tectonic(w, rift_src, w->radius * P.belt_width * 0.6f, rift);
    qlcg_spread_tectonic(w, shear_src, w->radius * P.belt_width / 3.0f, shear);

    /* 高度合成 */
    uint32_t s71 = w->opt.seed ^ 71u;
    for (int i = 0; i < n; i++) {
        const float *pt = &w->points[i * 3];
        int pid = ids[i];
        const TectonicPlate *p = &w->plates[pid];
        float t = -angle * p->speed;
        float c = cosf(t), s = sinf(t);
        float ax = p->axis[0], ay = p->axis[1], az = p->axis[2];
        float d = ax * pt[0] + ay * pt[1] + az * pt[2];
        float k = 1.0f - c;
        float rx = pt[0] * c + (ay * pt[2] - az * pt[1]) * s + ax * d * k;
        float ry = pt[1] * c + (az * pt[0] - ax * pt[2]) * s + ay * d * k;
        float rz = pt[2] * c + (ax * pt[1] - ay * pt[0]) * s + az * d * k;

        float basin = 0.0f;
        float score = qlcg_crust_score(&cf, rx, ry, rz, &basin, NULL);
        if (w->basin) w->basin[i] = basin;
        float margin = score - cf.reference;
        float continental = smooth01(margin / 0.045f + 0.5f);
        float sh = shear[i];
        float h = margin * 6000.0f
                + qlcg_base_noise(rx * 5.0f, ry * 5.0f, rz * 5.0f, s71) * 120.0f
                + uplift[i] * P.uplift_scale * (0.15f + 0.85f * continental) * (1.0f - 0.85f * basin)
                - rift[i] * P.rift_depth
                + (sh > 0.0f ? sh * qlcg_base_noise(pt[0] * 18.0f, pt[1] * 18.0f, pt[2] * 18.0f, w->opt.seed) * 120.0f : 0.0f);
        float activity = w->opt.tectonic_activity / 9.0f;
        float edge = 1.0f - smooth01(fabsf(margin) / .16f);
        float broad = qlcg_base_noise(rx*9+rz*2,ry*9,rz*9,w->opt.seed^0x62617931u);
        float detail = qlcg_base_noise(rx*23,ry*23+sh*rx*5,rz*23,w->opt.seed^0x62617932u);
        h += edge*((260+170*activity+90*basin)*broad+
                   (75+80*sh+100*rift[i]+55*uplift[i])*detail);
        w->height[i] = h;
        w->uplift[i] = uplift[i];
        w->rift[i] = rift[i];
        w->shear[i] = sh;
    }

    /* 大陆核碰撞走廊：压低陆桥 */
    float half_km = cf.half_width * w->radius / 1000.0f;
    if (half_km > 0.0f) {
        float *corridor_src = (float *)calloc((size_t)n, sizeof(float));
        float *corridors = (float *)calloc((size_t)n, sizeof(float));
        if (corridor_src && corridors) {
            for (int i = 0; i < n; i++) {
                int core_i = 0;
                qlcg_crust_score(&cf, w->points[i * 3], w->points[i * 3 + 1], w->points[i * 3 + 2], NULL, &core_i);
                for (int k = 0; k < 2; k++) {
                    int dir = (k == 0) ? 0 : 2;
                    int j = w->neighbors[i * 8 + dir];
                    int core_j = 0;
                    qlcg_crust_score(&cf, w->points[j * 3], w->points[j * 3 + 1], w->points[j * 3 + 2], NULL, &core_j);
                    if (core_i == core_j) continue;
                    float mn = fminf(uplift[i], uplift[j]);
                    float mx = fmaxf(rift[i], rift[j]);
                    float collision = smooth01((mn - 0.7f) / 0.55f) * (1.0f - qlcg_clampf(mx, 0.0f, 1.0f));
                    float value = 1.0f - collision;
                    if (value > corridor_src[i]) corridor_src[i] = value;
                    if (value > corridor_src[j]) corridor_src[j] = value;
                }
            }
            qlcg_spread_tectonic(w, corridor_src, half_km * 700.0f, corridors);
            for (int i = 0; i < n; i++) {
                w->height[i] -= corridors[i] * (1400.0f + 800.0f * (1.0f - w->opt.land_ratio));
            }
        }
        free(corridor_src);
        free(corridors);
    }

    free(warped); free(ids);
    free(uplift_src); free(rift_src); free(shear_src);
    free(uplift); free(rift); free(shear);
}
