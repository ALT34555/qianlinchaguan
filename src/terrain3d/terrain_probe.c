/* 山川拾取：射线与瞄准探针 */
#include "terrain_view.h"
#include "../core/i18n.h"
#include "../systems/world/faction_naming.h"
#include "../systems/calendar/gan_zhi.h"
#include "terrain_types.h"
#include "../systems/world/model/plant_material.h"
#include "../core/math_util.h"
#include "../core/measurements.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

extern const char *FLOW_DIR_NAMES[8];

bool qlcg_terrain_raycast(const TerrainRenderer *tr, const RegionData *reg,
                          float ray_ox, float ray_oy, float ray_oz,
                          float ray_dx, float ray_dy, float ray_dz,
                          float *out_lx, float *out_ly, float *out_lz, float *out_dist) {
    if (!reg) return false;
    float t = 0.5f;
    for (int i = 0; i < 160; i++) {
        float px = ray_ox + ray_dx * t;
        float py = ray_oy + ray_dy * t;
        float pz = ray_oz + ray_dz * t;
        const RegionData *chunk = qlcg_region_chunk_at(reg,px,pz,NULL,NULL);
        if (!chunk) return false;
        if (reg->stream) {
            int dx = chunk->center_cx-reg->stream->center_cx;
            int dz = chunk->center_cz-reg->stream->center_cz;
            float radius = reg->stream->radius+0.5f;
            if (dx*dx+dz*dz > radius*radius) return false;
        }
        float gh = qlcg_region_height_at(reg, px, pz);
        float wl = qlcg_region_water_at(reg, px, pz);
        float surf = qlcg_maxf(gh, wl);
        if (py <= surf) {
            if (out_lx)   *out_lx   = px;
            if (out_ly)   *out_ly   = surf;
            if (out_lz)   *out_lz   = pz;
            if (out_dist) *out_dist = t;
            return true;
        }
        t += qlcg_maxf(0.6f, (py - surf) * 0.45f);
        if (t > 280.0f) break;
    }
    (void)tr;
    return false;
}

static float plant_mesh_hit(const LpGeometry *g, const float origin[3],
                            const float dir[3], float limit) {
    for (int i = 0; i < g->index_count; i += 3) {
        const float *a = g->positions + 3 * g->indices[i];
        const float *b = g->positions + 3 * g->indices[i + 1];
        const float *c = g->positions + 3 * g->indices[i + 2];
        float ab[3], ac[3], from_a[3], cross[3], side[3];
        for (int axis = 0; axis < 3; axis++) {
            ab[axis] = b[axis] - a[axis];
            ac[axis] = c[axis] - a[axis];
            from_a[axis] = origin[axis] - a[axis];
        }
        qlcg_cross3(dir, ac, cross);
        float det = qlcg_dot3(ab, cross);
        if (fabsf(det) < 1e-8f) continue;
        float u = qlcg_dot3(from_a, cross) / det;
        if (u < 0.0f || u > 1.0f) continue;
        qlcg_cross3(from_a, ab, side);
        float v = qlcg_dot3(dir, side) / det;
        if (v < 0.0f || u + v > 1.0f) continue;
        float t = qlcg_dot3(ac, side) / det;
        if (t >= 0.0f && t < limit) limit = t;
    }
    return limit;
}

static const PropInstance *probe_plant(const TerrainRenderer *tr, const RegionData *reg,
                                       const float dir[3], float *limit) {
    if (tr->bound_stream != reg->stream || !tr->plant_inst_total) return NULL;
    const LpGeometry *geometry[QLCG_MAX_PLANT_BATCH] = {0};
    for (int i = 0; i < tr->plant_batch_count; i++) {
        const PlantBatch *batch = &tr->plants[i];
        if (batch->inst_count > 0)
            geometry[qlcg_plant_batch_key(batch->species, batch->season, batch->form)] =
                qlcg_plant_geometry(batch->species, batch->form, batch->season);
    }

    float ox = (float)((double)reg->center_x - tr->anchor_x + tr->x);
    float oz = (float)((double)reg->center_z - tr->anchor_z + tr->z);
    float horizontal_sq = dir[0] * dir[0] + dir[2] * dir[2];
    const PropInstance *hit = NULL;
    for (int i = 0; i < tr->probe_plant_count; i++) {
        const PropInstance *p = &tr->probe_plants[i];
        if (p->species >= 255 || p->visual_season >= PD_SEASON_COUNT) continue;
        float x = ox - p->x, z = oz - p->z;
        /* 只检视 3D 全模型范围，贴图实例不参与 */
        float reach = fminf(*limit + 64.0f, (float)(QLCG_PROP_FULL_CHUNKS * CHUNK_SIZE));
        if (x * x + z * z > reach * reach) continue;
        int form = qlcg_plant_model_form(tr->props_form, p->shape_variant);
        const LpGeometry *g = geometry[qlcg_plant_batch_key(p->species, p->visual_season, form)];
        if (!g || p->scale_y <= 0.0f) continue;

        float y = tr->y - p->y;
        float along = horizontal_sq > 1e-8f
            ? qlcg_clampf(-(x * dir[0] + z * dir[2]) / horizontal_sq, 0.0f, *limit)
            : 0.0f;
        float near_x = x + along * dir[0], near_z = z + along * dir[2];
        float scale = qlcg_plant_instance_scale(p->scale_y, g->height);
        float radius = (float)g->radius * scale;
        if (near_x * near_x + near_z * near_z > radius * radius) continue;
        if (fminf(y, y + *limit * dir[1]) > (float)g->height * scale) continue;

        float cos_yaw = cosf(p->rot_y), sin_yaw = sinf(p->rot_y);
        float local_origin[3] = {(x * cos_yaw + z * sin_yaw) / scale, y / scale,
                                 (z * cos_yaw - x * sin_yaw) / scale};
        float local_dir[3] = {(dir[0] * cos_yaw + dir[2] * sin_yaw) / scale, dir[1] / scale,
                              (dir[2] * cos_yaw - dir[0] * sin_yaw) / scale};
        float distance = plant_mesh_hit(g, local_origin, local_dir, *limit);
        if (distance < *limit) {
            *limit = distance;
            hit = p;
        }
    }
    return hit;
}
void qlcg_terrain_update_aim_probe(TerrainRenderer *tr, const RegionData *reg,
                                   const MacroWorld *w, const GameSettings *s, bool inspect_plants) {
    memset(&tr->aim, 0, sizeof(tr->aim));
    if (!reg) return;

    float cp = cosf(tr->pitch), sp = sinf(tr->pitch);
    float cy = cosf(tr->yaw),   sy = sinf(tr->yaw);
    float dx = sy * cp, dy = sp, dz = -cy * cp;

    float hx = 0, hy = 0, hz = 0, hdist = 0;
    tr->has_hover = qlcg_terrain_raycast(tr, reg, tr->x, tr->y, tr->z, dx, dy, dz,
                                          &hx, &hy, &hz, &hdist);
    if (tr->has_hover) {
        tr->hover_lx = hx;
        tr->hover_ly = hy;
        tr->hover_lz = hz;
    }

    if (inspect_plants) {
        const float dir[3] = {dx, dy, dz};
        float distance = tr->has_hover ? hdist : 280.0f;
        const PropInstance *plant = probe_plant(tr, reg, dir, &distance);
        if (plant) {
            tr->aim.kind = AIM_HIT_PLANT;
            tr->aim.lx = tr->x + dx * distance;
            tr->aim.ly = tr->y + dy * distance;
            tr->aim.lz = tr->z + dz * distance;
            tr->aim.wx = reg->center_x + tr->aim.lx;
            tr->aim.wz = reg->center_z + tr->aim.lz;
            tr->aim.dist_m = distance;
            snprintf(tr->aim.title, sizeof(tr->aim.title), "植物 · %s", plant->label);
            snprintf(tr->aim.detail1, sizeof(tr->aim.detail1), "季相：%s | 株高 %.*f m",
                     qlcg_season_name(plant->visual_season),
                     plant->scale_y < 1.0f ? 2 : 1, plant->scale_y);
            snprintf(tr->aim.detail2, sizeof(tr->aim.detail2), "距离 %.1f m", distance);
            return;
        }
    }
    if (!tr->has_hover) return;

    tr->aim.lx     = hx;
    tr->aim.ly     = hy;
    tr->aim.lz     = hz;
    tr->aim.wx     = reg->center_x + hx;
    tr->aim.wz     = reg->center_z + hz;
    tr->aim.dist_m = hdist;

    float chunk_hx, chunk_hz;
    const RegionData *hit_chunk = qlcg_region_chunk_at(reg,hx,hz,&chunk_hx,&chunk_hz);
    if (!hit_chunk) return;

    /* 匹配地面附近的旗帜和岩石 */
    for (int i = 0; i < hit_chunk->flag_count; i++) {
        const PropInstance *fl = &hit_chunk->flags[i];
        if (hypotf(fl->x - chunk_hx, fl->z - chunk_hz) < 2.6f) {
            tr->aim.kind = AIM_HIT_FLAG;
            const char *label = fl->label;
            if (fl->faction_id >= 0 && fl->faction_id < w->faction_count)
                label = qlcg_faction_display_name(&w->factions[fl->faction_id]);
            snprintf(tr->aim.title, sizeof(tr->aim.title), "%s", label);
            snprintf(tr->aim.detail1, sizeof(tr->aim.detail1), "人造区块 · 阵营主旗 (距离 %.1f m)", hdist);
            snprintf(tr->aim.detail2, sizeof(tr->aim.detail2), "世界坐标 (%.0f, %.0f)", tr->aim.wx, tr->aim.wz);
            return;
        }
    }

    for (int i = 0; i < hit_chunk->rock_count; i++) {
        const PropInstance *rk = &hit_chunk->rocks[i];
        if (hypotf(rk->x - chunk_hx, rk->z - chunk_hz) < 2.4f) {
            tr->aim.kind = AIM_HIT_ROCK;
            snprintf(tr->aim.title, sizeof(tr->aim.title), "山石 · %s", rk->label);
            snprintf(tr->aim.detail1, sizeof(tr->aim.detail1), "岩体原型 #%d | 粒径 %.1f m", (int)rk->archetype + 1, rk->scale_x);
            snprintf(tr->aim.detail2, sizeof(tr->aim.detail2), "距离 %.1f m", hdist);
            return;
        }
    }

    float gh = qlcg_region_height_at(reg, hx, hz);
    float wl = qlcg_region_water_at(reg, hx, hz);
    int cx = (int)floorf(tr->aim.wx / (float)QLCG_CHUNK_SIZE_M);
    int cz = (int)floorf(tr->aim.wz / (float)QLCG_CHUNK_SIZE_M);
    ChunkInspectInfo ci;
    qlcg_world_inspect_chunk(w, cx, cz, &ci);

    if (wl > gh + 0.1f) {
        SurfaceWater sw;
        qlcg_sample_water(w, tr->aim.wx, tr->aim.wz, tr->plant_season, &sw, NULL);
        const char *kind = qlcg_t("probe.water.generic");
        if (sw.kind == WATER_KIND_SEA) kind = qlcg_t("probe.water.sea");
        else if (sw.kind == WATER_KIND_LAKE) kind = qlcg_t("probe.water.lake");
        else if (sw.kind == WATER_KIND_RIVER) kind = qlcg_t("probe.water.river");
        else if (sw.kind == WATER_KIND_FALL) kind = qlcg_t("probe.water.fall");
        else if (sw.kind == WATER_KIND_STATIC) kind = qlcg_t("probe.water.still");
        const char *wf = (sw.form == WATER_FORM_LOW) ? "枯水期" :
                         ((sw.form == WATER_FORM_DRY) ? "断流态" :
                         ((sw.form == WATER_FORM_FLOODED) ? "泛滥期" : "正常水体"));
        const char *fz = (sw.freeze == WATER_FREEZE_FULL) ? "厚冰壳" :
                         ((sw.freeze == WATER_FREEZE_PARTIAL) ? "薄冰层" : "未结冰");
        tr->aim.kind = AIM_HIT_WATER;
        snprintf(tr->aim.title, sizeof(tr->aim.title), qlcg_t("probe.water.title"), kind, qlcg_chunk_type_name(ci.type_def));
        snprintf(tr->aim.detail1, sizeof(tr->aim.detail1),
                 "形态：%s | 冻结：%s（%.2f m）| 水深 %.1f m | 水温 %.1f℃",
                 wf, fz, sw.ice_thickness, wl - gh, sw.temperature);
        if (sw.speed > 0.05f) {
            float ang = atan2f(-sw.vz, sw.vx);
            int dir8 = ((int)roundf(ang / (QLCG_PI * 0.25f)) + 8) % 8;
            snprintf(tr->aim.detail2, sizeof(tr->aim.detail2),
                     "流向：%s | 流速 %.1f m/s | 流量 %.0f m³/s | 河宽 %.0f m",
                     FLOW_DIR_NAMES[dir8], sw.speed, sw.discharge, sw.width_m);
        } else {
            snprintf(tr->aim.detail2, sizeof(tr->aim.detail2), "静水无流 | 冰厚 %.2f m | 距离 %.1f m",
                     sw.ice_thickness, hdist);
        }
    } else {
        tr->aim.kind = AIM_HIT_GROUND;
        char elev_str[48];
        qlcg_format_elevation(s->measurement_system, gh, elev_str, sizeof(elev_str));
        snprintf(tr->aim.title, sizeof(tr->aim.title), qlcg_t("probe.surface.title"), ci.type_def->code, qlcg_chunk_type_name(ci.type_def));
        snprintf(tr->aim.detail1, sizeof(tr->aim.detail1), "海拔 %s | 气温 %.1f℃ | 湿润 %.0f%%",
                 elev_str, ci.temperature, ci.moisture * 100.0f);
        snprintf(tr->aim.detail2, sizeof(tr->aim.detail2), "区块 (%d, %d) | 距离 %.1f m", cx, cz, hdist);
    }
}

