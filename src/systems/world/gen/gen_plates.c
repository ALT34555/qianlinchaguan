/* 板块布局：主板块取最大分离，小板块落在主板块交界处 */
#include "gen_plates.h"
#include "../../../core/math_util.h"
#include <math.h>
#include <string.h>

#define GEN_TAU 6.283185307179586f

/* mulberry32：与 base random 等价 */
static float rand01(uint32_t *s) {
    uint32_t t = (*s += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    uint32_t a = t ^ (t >> 7);
    uint32_t b = a * (t | 61u);
    t = t ^ (t + b);
    return (float)((double)(t ^ (t >> 14)) / 4294967296.0);
}

static float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
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

static void sphere_point(float lon, float y, float out[3]) {
    float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
    out[0] = r * cosf(lon);
    out[1] = y;
    out[2] = r * sinf(lon);
}

void qlcg_planet_size(int columns, int *major, int *minor) {
    int mj = (int)roundf((float)columns / 64.0f);
    if (mj < 4) mj = 4;
    int mn = (int)roundf((float)mj / 4.0f);
    if (mn < 1) mn = 1;
    *major = mj;
    *minor = mn;
}

DriftProfile qlcg_drift_profile(int activity) {
    float a = qlcg_clampf((float)activity / 9.0f, 0.0f, 1.0f);
    DriftProfile p;
    p.span         = 0.05f + 0.027f * a + 0.473f * a * a;
    p.speed        = 0.10f + 0.41f * a + 0.74f * a * a;
    p.dispersion   = 0.15f + 0.36f * a;
    p.belt_width   = 0.090f - 0.055f * a;
    p.uplift_scale = 1100.0f + 900.0f * a;
    p.rift_depth   = 250.0f + 1068.0f * a + 182.0f * a * a;
    p.steps        = 2.0f + 2.14f * a + 5.86f * a * a;
    return p;
}

int qlcg_create_plates(TectonicPlate *plates, int cap, const WorldCreatorOptions *opt,
                       int major, int minor) {
    if (!plates || cap < major + minor) return 0;
    uint32_t rng = opt->seed ^ 0x74656374u;

    for (int id = 0; id < major; id++) {
        float best = -1e9f, c[3] = {1.0f, 0.0f, 0.0f};
        for (int attempt = 0; attempt < 12; attempt++) {
            /* 显式两步取随机，C 的实参求值顺序未定义 */
            float lon = rand01(&rng) * GEN_TAU;
            float yy = rand01(&rng) * 2.0f - 1.0f;
            float p[3];
            sphere_point(lon, yy, p);
            float separation = 1.0f;
            if (id > 0) {
                float mx = -1e9f;
                for (int b = 0; b < id; b++) {
                    float d = dot3(p, plates[b].start);
                    if (d > mx) mx = d;
                }
                separation = 1.0f - mx;
            }
            if (separation > best) {
                best = separation;
                c[0] = p[0]; c[1] = p[1]; c[2] = p[2];
            }
        }
        float lon_a = rand01(&rng) * GEN_TAU;
        float yy_a = rand01(&rng) * 2.0f - 1.0f;
        float a[3];
        sphere_point(lon_a, yy_a, a);
        float axis[3];
        cross3(c, a, axis);
        unit3(axis);

        TectonicPlate *pl = &plates[id];
        memset(pl, 0, sizeof(*pl));
        pl->id = id;
        pl->is_minor = false;
        pl->kind = 0;
        memcpy(pl->start, c, sizeof(c));
        memcpy(pl->center, c, sizeof(c));
        memcpy(pl->axis, axis, sizeof(axis));
        pl->speed = 0.65f + rand01(&rng) * 0.7f;
        pl->oceanic = rand01(&rng) > 0.5f;
        pl->bias = (rand01(&rng) - 0.5f) * 0.025f;
    }

    /* 交界候选：两两主板块之间的中点 */
    struct { float center[3]; int a, b; float priority; bool used; } cand[128];
    int cc = 0;
    for (int a = 0; a < major && cc < 128; a++) {
        for (int b = a + 1; b < major && cc < 128; b++) {
            float delta[3] = {
                plates[a].start[0] - plates[b].start[0],
                plates[a].start[1] - plates[b].start[1],
                plates[a].start[2] - plates[b].start[2]
            };
            float len = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
            float offset = (plates[b].bias - plates[a].bias) / (len > 1e-9f ? len : 1e-9f);
            float mid[3] = {
                plates[a].start[0] + plates[b].start[0],
                plates[a].start[1] + plates[b].start[1],
                plates[a].start[2] + plates[b].start[2]
            };
            unit3(mid);
            float center[3];
            for (int k = 0; k < 3; k++) {
                center[k] = mid[k] * sqrtf(fmaxf(0.0f, 1.0f - offset * offset)) + delta[k] / (len > 1e-9f ? len : 1e-9f) * offset;
            }
            unit3(center);
            float score = dot3(center, plates[a].start) + plates[a].bias;
            bool skip = false;
            for (int p = 0; p < major; p++) {
                if (dot3(center, plates[p].start) + plates[p].bias > score + 1e-9f) { skip = true; break; }
            }
            if (skip) continue;
            memcpy(cand[cc].center, center, sizeof(center));
            cand[cc].a = a;
            cand[cc].b = b;
            cand[cc].priority = rand01(&rng);
            cand[cc].used = false;
            cc++;
        }
    }

    if (opt->map_mode==WORLD_MAP_INFINITE && cc<minor) {
        float normal[3],a[3],b[3];
        for(int k=0;k<3;k++) normal[k]=plates[0].start[k]-plates[1].start[k];
        unit3(normal); cross3(normal,plates[0].axis,a); unit3(a); cross3(normal,a,b);
        for(int k=cc;k<minor;k++) {
            float t=GEN_TAU*(k+.5f)/minor;
            for(int j=0;j<3;j++) cand[k].center[j]=a[j]*cosf(t)+b[j]*sinf(t);
            cand[k].a=0; cand[k].b=1; cand[k].priority=rand01(&rng); cand[k].used=false;
        }
        cc=minor;
    }
    int made = 0;
    for (int k = 0; k < minor; k++) {
        int chosen = -1;
        float best = -1e9f;
        for (int i = 0; i < cc; i++) {
            if (cand[i].used) continue;
            float clearance = 1.0f;
            if (k > 0) {
                float mn = 1e9f;
                for (int j = major; j < major + k; j++) {
                    float d = 1.0f - dot3(cand[i].center, plates[j].start);
                    if (d < mn) mn = d;
                }
                clearance = mn;
            }
            float score = clearance * (0.8f + cand[i].priority * 0.4f);
            if (score > best) { best = score; chosen = i; }
        }
        if (chosen < 0) break;
        cand[chosen].used = true;

        TectonicPlate *pl = &plates[major + made];
        memset(pl, 0, sizeof(*pl));
        pl->id = major + made;
        pl->is_minor = true;
        pl->kind = 1;
        pl->neighbors[0] = cand[chosen].a;
        pl->neighbors[1] = cand[chosen].b;
        memcpy(pl->center, cand[chosen].center, sizeof(pl->center));
        memcpy(pl->start, cand[chosen].center, sizeof(pl->start));
        float radius = acosf(qlcg_clampf(1.0f - 2.0f / (float)major, -1.0f, 1.0f)) *
                       (0.28f + rand01(&rng) * 0.1f);
        pl->radius = radius;
        pl->cap_cos = cosf(radius);
        float ax[3];
        for (int k2 = 0; k2 < 3; k2++) {
            ax[k2] = plates[cand[chosen].a].axis[k2] + plates[cand[chosen].b].axis[k2];
        }
        unit3(ax);
        memcpy(pl->axis, ax, sizeof(ax));
        pl->speed = (plates[cand[chosen].a].speed + plates[cand[chosen].b].speed) * 0.5f;
        pl->oceanic = rand01(&rng) > 0.5f;
        pl->bias = 0.0f;
        made++;
    }
    return major + made;
}

void qlcg_assign_plate_ids(const float *points, const TectonicPlate *plates, int total,
                           int major, uint16_t *ids, int count) {
    for (int i = 0; i < count; i++) {
        const float *p = &points[i * 3];
        float best = -1e9f;
        int id = 0;
        for (int k = 0; k < major; k++) {
            float s = dot3(p, plates[k].center) + plates[k].bias;
            if (s > best) { best = s; id = k; }
        }
        float closest = 1.0f;
        for (int k = major; k < total; k++) {
            float denom = 1.0f - plates[k].cap_cos;
            if (denom < 1e-6f) continue;
            float d = (1.0f - dot3(p, plates[k].center)) / denom;
            if (d < closest) { closest = d; id = k; }
        }
        ids[i] = (uint16_t)id;
    }
}
