/* 山川拾取：射线与瞄准探针 */
#include "terrain_view.h"
#include "terrain_dolls.h"
#include "../systems/world/world_civ_economy.h"
#include "../systems/world/world_infinite.h"
#include "../systems/world/world_settlement.h"
#include "../systems/world/world_prop_edit.h"
#include "../core/i18n.h"
#include "../systems/world/faction_naming.h"
#include "../systems/calendar/gan_zhi.h"
#include "terrain_types.h"
#include "terrain_visibility.h"
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
    const PlantBatch *bounds[QLCG_MAX_PLANT_BATCH]={0};
    for (int i = 0; i < tr->plant_batch_count; i++) {
        const PlantBatch *batch = &tr->plants[i];
        if(batch->inst_count>0)bounds[qlcg_plant_batch_key(batch->species,batch->season,batch->form)]=batch;
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
        int form = qlcg_plant_placed_form(qlcg_plant_model_form(tr->props_form, p->shape_variant),p->hanging);
        const LpGeometry *g = geometry[qlcg_plant_batch_key(p->species, p->visual_season, form)];
        if (!g || p->scale_y <= 0.0f) continue;

        float y = tr->y - p->y;
        float along = horizontal_sq > 1e-8f
            ? qlcg_clampf(-(x * dir[0] + z * dir[2]) / horizontal_sq, 0.0f, *limit)
            : 0.0f;
        float near_x = x + along * dir[0], near_z = z + along * dir[2];
        float scale = qlcg_plant_instance_scale(p->scale_y, g->height);
        const PlantBatch *box=bounds[qlcg_plant_batch_key(p->species,p->visual_season,form)];
        float radius=(box?box->radius:(float)g->radius)*scale;
        if(box){float low=box->low_y*scale,high=box->high_y*scale;
            if(fminf(y,y+*limit*dir[1])>high||fmaxf(y,y+*limit*dir[1])<low)continue;}

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

static float rock_tool_distance(const PropInstance *rock, const float ray[3],
                                float x, float y, float z) {
    float c = cosf(rock->rot_y), s = sinf(rock->rot_y);
    float rx = fmaxf(.04f, rock->scale_x * .45f);
    float ry = fmaxf(.04f, rock->scale_y * .55f);
    float rz = fmaxf(.04f, rock->scale_z * .45f);
    const float o[3] = {(x*c + z*s)/rx, (y - rock->scale_y*.45f)/ry, (z*c - x*s)/rz};
    const float d[3] = {(ray[0]*c + ray[2]*s)/rx, ray[1]/ry, (ray[2]*c - ray[0]*s)/rz};
    float a = qlcg_dot3(d, d), b = qlcg_dot3(o, d), outside = qlcg_dot3(o, o) - 1.0f;
    if (outside <= 0) return 0;
    float discriminant = b*b - a*outside;
    return discriminant >= 0 && b < 0 ? (-b - sqrtf(discriminant))/a : INFINITY;
}

static float flag_box(const float o[3],const float d[3],const float lo[3],const float hi[3]) {
    float enter=0,leave=INFINITY;
    for(int k=0;k<3;k++) {
        if(fabsf(d[k])<1e-6f) {
            if(o[k]<lo[k] || o[k]>hi[k]) return INFINITY;
        } else {
            float a=(lo[k]-o[k])/d[k],b=(hi[k]-o[k])/d[k];
            enter=fmaxf(enter,fminf(a,b));leave=fminf(leave,fmaxf(a,b));
        }
    }
    return enter<=leave?enter:INFINITY;
}
static float flag_cylinder(const float o[3],const float d[3],float radius,float height) {
    float a=d[0]*d[0]+d[2]*d[2],b=o[0]*d[0]+o[2]*d[2],c=o[0]*o[0]+o[2]*o[2]-radius*radius;
    float enter=0,leave=INFINITY;
    if(a<1e-8f) {if(c>0) return INFINITY;}
    else {
        float disc=b*b-a*c;if(disc<0) return INFINITY;
        float root=sqrtf(disc);
        enter=fmaxf(0,(-b-root)/a);leave=(-b+root)/a;
    }
    if(fabsf(d[1])<1e-6f) {if(o[1]<0 || o[1]>height) return INFINITY;}
    else {
        float t=-o[1]/d[1],u=(height-o[1])/d[1];
        enter=fmaxf(enter,fminf(t,u));leave=fminf(leave,fmaxf(t,u));
    }
    return enter<=leave?enter:INFINITY;
}
static bool flag_probe(const TerrainRenderer *tr,const RegionData *reg,const float ray[3],float limit,TerrainToolHit *hit) {
    bool found=false;
    int extent=reg->stream?1:0;
    for(int dz=-extent;dz<=extent;dz++) for(int dx=-extent;dx<=extent;dx++) {
        float x,z;
        const RegionData *chunk=qlcg_region_chunk_at(reg,tr->x+dx*CHUNK_SIZE,tr->z+dz*CHUNK_SIZE,&x,&z);
        if(!chunk) continue;
        for(int i=0;i<chunk->flag_count;i++) {
            const PropInstance *f=chunk->flags+i;
            float o[]={x-dx*CHUNK_SIZE-f->x,tr->y-f->y,z-dz*CHUNK_SIZE-f->z};
            float t=fminf(flag_cylinder(o,ray,.15f,6.5f),flag_cylinder(o,ray,.85f,.28f));
            t=fminf(t,flag_box(o,ray,(float[]){-1.86f,1.8f,.08f},(float[]){1.86f,5.95f,.47f}));
            t=fminf(t,flag_box(o,ray,(float[]){-2.04f,5.89f,-.01f},(float[]){2.04f,6.11f,.21f}));
            if(t>=limit) continue;
            *hit=(TerrainToolHit){.kind=AIM_HIT_FLAG,.distance=t,.wx=chunk->center_x+f->x,.wz=chunk->center_z+f->z};
            found=true;limit=t;
        }
    }
    return found;
}

bool qlcg_terrain_tool_target(const TerrainRenderer *tr, const RegionData *reg,
                              const MacroWorld *w, float reach, TerrainToolHit *hit) {
    if (!tr || !reg || !w || !hit || reach <= 0) return false;
    *hit = (TerrainToolHit){0};
    float cp = cosf(tr->pitch);
    const float ray[3] = {sinf(tr->yaw)*cp, sinf(tr->pitch), -cosf(tr->yaw)*cp};
    float limit = reach;
    for (float t = .06f; t <= reach; t += .06f) {
        float x = tr->x + t*ray[0], z = tr->z + t*ray[2];
        float y = tr->y + t*ray[1];
        float ground = qlcg_region_height_at(reg, x, z);
        if (!isfinite(ground) || qlcg_settlement_blocked(reg, x, y - .03f, z, .06f, .03f)) {
            limit = t;
            break;
        }
        float water = qlcg_region_water_at(reg, x, z);
        if (y > fmaxf(ground, water)) continue;
        *hit = (TerrainToolHit){.kind = water > ground + .1f ? AIM_HIT_WATER : AIM_HIT_GROUND,
                                .wx = reg->center_x + x, .wz = reg->center_z + z};
        limit = t;
        break;
    }
    float distance = limit;
    const PropInstance *plant = probe_plant(tr, reg, ray, &distance);
    if (plant && distance < limit) {
        *hit = (TerrainToolHit){.kind = AIM_HIT_PLANT,
                                .wx = plant->edit_x,
                                .wz = plant->edit_z,
                                .prop_kind = CHD_PROP_PLANT_BASE + plant->species,
                                .category = plant->category, .state = plant->model_state};
        limit = distance;
    }
    int extent = reg->stream ? 1 : 0;
    for (int dz = -extent; dz <= extent; ++dz) for (int dx = -extent; dx <= extent; ++dx) {
        float x, z;
        const RegionData *chunk = qlcg_region_chunk_at(reg, tr->x + dx*CHUNK_SIZE,
                                                       tr->z + dz*CHUNK_SIZE, &x, &z);
        if (!chunk) continue;
        x -= dx*CHUNK_SIZE;
        z -= dz*CHUNK_SIZE;
        for (int i = 0; i < chunk->rock_count; ++i) {
            const PropInstance *rock = &chunk->rocks[i];
            if (qlcg_coral_index(rock->archetype) >= 0) continue;
            float ox = x - rock->x, oz = z - rock->z;
            if (hypotf(ox, oz) > reach + rock->scale_x) continue;
            distance = rock_tool_distance(rock, ray, ox, tr->y - rock->y, oz);
            if (distance >= limit) continue;
            *hit = (TerrainToolHit){.kind = AIM_HIT_ROCK, .wx = chunk->center_x + rock->x,
                                    .wz = chunk->center_z + rock->z,
                                    .prop_kind = CHD_PROP_ROCK_BASE + rock->archetype};
            limit = distance;
        }
    }
    TerrainToolHit flag;
    if(flag_probe(tr,reg,ray,limit,&flag)) {*hit=flag;limit=flag.distance;}
    AimProbeResult doll;
    if (qlcg_terrain_dolls_probe(tr, reg, w, ray, limit, &doll)) {
        *hit=(TerrainToolHit){.kind=AIM_HIT_DOLL,.wx=doll.wx,.wz=doll.wz,.doll=doll.doll};
        limit=doll.dist_m;
    }
    hit->distance=limit;
    return hit->kind != AIM_HIT_NONE;
}

void qlcg_terrain_update_aim_probe(TerrainRenderer *tr, const RegionData *reg,
                                   const MacroWorld *w, const GameSettings *s, bool inspect_plants) {
    if (!reg || !w) {
        memset(&tr->aim,0,sizeof(tr->aim));tr->has_hover=false;tr->probe_cache.valid=false;return;
    }
    float pose[5]={tr->x,tr->y,tr->z,tr->yaw,tr->pitch};
    uint64_t revision=reg->stream?reg->stream->revision:0;
    int locale=qlcg_i18n_get_index();
    if(tr->probe_cache.valid && !inspect_plants && !tr->probe_cache.inspecting &&
       tr->eng_tool==ENG_TOOL_NONE && tr->aim.kind!=AIM_HIT_WATER &&
       tr->probe_cache.region==reg && tr->probe_cache.world==w &&
       !memcmp(tr->probe_cache.pose,pose,sizeof(pose)) && tr->probe_cache.stream==revision &&
       tr->probe_cache.mesh==tr->mesh_version && tr->probe_cache.props==tr->props_revision &&
       tr->probe_cache.dolls==qlcg_terrain_dolls_revision(tr) &&
       tr->probe_cache.delta==w->delta_version && tr->probe_cache.surface==w->surface_revision &&
       tr->probe_cache.territory==w->territory_revision && tr->probe_cache.region_revision==reg->mesh_revision &&
       tr->probe_cache.measurement==s->measurement_system && tr->probe_cache.locale==locale)return;
    tr->probe_cache.valid=tr->eng_tool==ENG_TOOL_NONE;
    tr->probe_cache.region=reg;tr->probe_cache.world=w;tr->probe_cache.inspecting=inspect_plants;
    memcpy(tr->probe_cache.pose,pose,sizeof(pose));tr->probe_cache.stream=revision;
    tr->probe_cache.mesh=tr->mesh_version;tr->probe_cache.props=tr->props_revision;
    tr->probe_cache.dolls=qlcg_terrain_dolls_revision(tr);
    tr->probe_cache.delta=w->delta_version;tr->probe_cache.surface=w->surface_revision;
    tr->probe_cache.territory=w->territory_revision;tr->probe_cache.region_revision=reg->mesh_revision;
    tr->probe_cache.measurement=s->measurement_system;tr->probe_cache.locale=locale;
    memset(&tr->aim,0,sizeof(tr->aim));

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

    const float doll_direction[3]={dx,dy,dz};
    TerrainToolHit flag;
    if(flag_probe(tr,reg,doll_direction,tr->has_hover?hdist:280,&flag)) {
        TerrainToolHit front;
        if(flag.distance>QLCG_FLAG_INTERACT_REACH ||
           (qlcg_terrain_tool_target(tr,reg,w,fminf(QLCG_FLAG_INTERACT_REACH+.001f,flag.distance+.06f),&front) && front.kind==AIM_HIT_FLAG)) {
            int cx=(int)floorf(flag.wx/CHUNK_SIZE),cz=(int)floorf(flag.wz/CHUNK_SIZE);
            const TerritoryFaction *owner=qlcg_settlement_chunk(w,cx,cz);
            tr->aim=(AimProbeResult){.kind=AIM_HIT_FLAG,.dist_m=flag.distance,.wx=flag.wx,.wz=flag.wz,
                .lx=tr->x+dx*flag.distance,.ly=tr->y+dy*flag.distance,.lz=tr->z+dz*flag.distance};
            snprintf(tr->aim.title,sizeof(tr->aim.title),"%s",owner?qlcg_faction_display_name(owner):"主旗");
            snprintf(tr->aim.detail1,sizeof(tr->aim.detail1),qlcg_i18n_content_text("人造区块 · 阵营主旗 (距离 %.1f m)"),flag.distance);
            QlcgCivEconomySnapshot economy;
            if(qlcg_civ_economy_at(w,cx,cz,NULL,NULL,&economy))
                snprintf(tr->aim.detail2,sizeof(tr->aim.detail2),qlcg_i18n_content_text("人口 %d/%d · 粮 %d 木 %d 纤 %d"),economy.population,economy.capacity,economy.food,economy.wood,economy.fiber);
            return;
        }
    }
    if (qlcg_terrain_dolls_probe(tr,reg,w,doll_direction,tr->has_hover?hdist:280.0f,&tr->aim)) {
        if (tr->aim.dist_m>QLCG_DOLL_INTERACT_REACH) return;
        TerrainToolHit hit;
        float reach=fminf(QLCG_DOLL_INTERACT_REACH,tr->aim.dist_m+.12f);
        if (qlcg_terrain_tool_target(tr,reg,w,reach,&hit) && hit.kind==AIM_HIT_DOLL) return;
        tr->aim=(AimProbeResult){0};
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
            snprintf(tr->aim.title, sizeof(tr->aim.title), qlcg_i18n_content_text("植物 · %s"), qlcg_i18n_content_text(plant->label));
            snprintf(tr->aim.detail1, sizeof(tr->aim.detail1), qlcg_i18n_content_text("季相：%s | 株高 %.*f m"),
                     qlcg_season_name(plant->visual_season),
                     plant->scale_y < 1.0f ? 2 : 1, plant->scale_y);
            snprintf(tr->aim.detail2, sizeof(tr->aim.detail2), qlcg_i18n_content_text("距离 %.1f m"), distance);
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

    for (int i = 0; i < hit_chunk->rock_count; i++) {
        const PropInstance *rk = &hit_chunk->rocks[i];
        if (qlcg_coral_index(rk->archetype) >= 0) continue;
        if (hypotf(rk->x - chunk_hx, rk->z - chunk_hz) < 2.4f) {
            tr->aim.kind = AIM_HIT_ROCK;
            snprintf(tr->aim.title, sizeof(tr->aim.title), qlcg_i18n_content_text("山石 · %s"), qlcg_i18n_content_text(rk->label));
            snprintf(tr->aim.detail1, sizeof(tr->aim.detail1), qlcg_i18n_content_text("岩体原型 #%d | 粒径 %.1f m"), (int)rk->archetype + 1, rk->scale_x);
            snprintf(tr->aim.detail2, sizeof(tr->aim.detail2), qlcg_i18n_content_text("距离 %.1f m"), hdist);
            return;
        }
    }

    float gh = qlcg_region_height_at(reg, hx, hz);
    float wl = qlcg_region_water_at(reg, hx, hz);
    int cx = (int)floorf(tr->aim.wx / (float)QLCG_CHUNK_SIZE_M);
    int cz = (int)floorf(tr->aim.wz / (float)QLCG_CHUNK_SIZE_M);
    ChunkInspectInfo ci={0};
    if(wl<=gh+.1f && reg->stream && reg->stream->delta_revision==w->delta_version &&
       reg->stream->surface_revision==w->surface_revision && reg->stream->territory_revision==w->territory_revision) {
        ci.type_def=qlcg_get_chunk_type(hit_chunk->site_chunk_type);
        double lx=((double)cx+.5)*CHUNK_SIZE,lz=((double)cz+.5)*CHUNK_SIZE;
        const MacroWorld *owner=qlcg_world_flat(w)?qlcg_infinite_resolve(w,lx,lz,&lx,&lz):w;
        if (!owner) owner=w;
        int cell=qlcg_world_cell_at_wxz(owner,(float)lx,(float)lz);
        ci.temperature=owner->temperature[cell];ci.moisture=owner->moisture[cell];
    } else qlcg_world_inspect_chunk(w,cx,cz,&ci);

    if (wl > gh + 0.1f) {
        SurfaceWater sw;
        qlcg_sample_water(w, tr->aim.wx, tr->aim.wz, tr->plant_season, &sw, NULL);
        const char *kind = qlcg_t("probe.water.generic");
        if (sw.kind == WATER_KIND_SEA) kind = qlcg_t("probe.water.sea");
        else if (sw.kind == WATER_KIND_LAKE) kind = qlcg_t("probe.water.lake");
        else if (sw.kind == WATER_KIND_RIVER) kind = qlcg_t("probe.water.river");
        else if (sw.kind == WATER_KIND_FALL) kind = qlcg_t("probe.water.fall");
        else if (sw.kind == WATER_KIND_STATIC) kind = qlcg_t("probe.water.still");
        const char *wf = (sw.form == WATER_FORM_LOW) ? qlcg_i18n_content_text("枯水期") :
                         ((sw.form == WATER_FORM_DRY) ? qlcg_i18n_content_text("断流态") :
                         ((sw.form == WATER_FORM_FLOODED) ? qlcg_i18n_content_text("泛滥期") : qlcg_i18n_content_text("正常水体")));
        const char *fz = (sw.freeze == WATER_FREEZE_FULL) ? qlcg_i18n_content_text("厚冰壳") :
                         ((sw.freeze == WATER_FREEZE_PARTIAL) ? qlcg_i18n_content_text("薄冰层") : qlcg_i18n_content_text("未结冰"));
        tr->aim.kind = AIM_HIT_WATER;
        snprintf(tr->aim.title, sizeof(tr->aim.title), qlcg_t("probe.water.title"), kind, qlcg_chunk_type_name(ci.type_def));
        snprintf(tr->aim.detail1, sizeof(tr->aim.detail1),
                 qlcg_i18n_content_text("形态：%s | 冻结：%s（%.2f m）| 水深 %.1f m | 水温 %.1f℃"),
                 wf, fz, sw.ice_thickness, wl - gh, sw.temperature);
        if (sw.speed > 0.05f) {
            float ang = atan2f(-sw.vz, sw.vx);
            int dir8 = ((int)roundf(ang / (QLCG_PI * 0.25f)) + 8) % 8;
            snprintf(tr->aim.detail2, sizeof(tr->aim.detail2),
                     qlcg_i18n_content_text("流向：%s | 流速 %.1f m/s | 流量 %.0f m³/s | 河宽 %.0f m"),
                     qlcg_i18n_content_text(FLOW_DIR_NAMES[dir8]), sw.speed, sw.discharge, sw.width_m);
        } else {
            snprintf(tr->aim.detail2, sizeof(tr->aim.detail2), qlcg_i18n_content_text("静水无流 | 冰厚 %.2f m | 距离 %.1f m"),
                     sw.ice_thickness, hdist);
        }
    } else {
        tr->aim.kind = AIM_HIT_GROUND;
        char elev_str[48];
        qlcg_format_elevation(s->measurement_system, gh, elev_str, sizeof(elev_str));
        snprintf(tr->aim.title, sizeof(tr->aim.title), qlcg_t("probe.surface.title"), ci.type_def->code, qlcg_chunk_type_name(ci.type_def));
        snprintf(tr->aim.detail1, sizeof(tr->aim.detail1), qlcg_i18n_content_text("海拔 %s | 气温 %.1f℃ | 湿润 %.0f%%"),
                 elev_str, ci.temperature, ci.moisture * 100.0f);
        snprintf(tr->aim.detail2, sizeof(tr->aim.detail2), qlcg_i18n_content_text("区块 (%d, %d) | 距离 %.1f m"), cx, cz, hdist);
    }
}

