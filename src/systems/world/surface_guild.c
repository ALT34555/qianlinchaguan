/* 丛格判定、圆盘轮廓与丛内布局 */
#include "surface_guild.h"
#include "surface_internal.h"
#include "plant_distribution.h"
#include "model/rock_material.h"
#include "model/rock_def.h"
#include "world_prop_edit.h"
#include "world_delta.h"
#include "hydrology_headwater.h"
#include "climate_zone.h"
#include "model/plant_guild.h"
#include "model/plant_material.h"
#include "model/plant_forms.h"
#include "model/plant_def.h"
#include "model/plant_phenology.h"
#include "gen/gen_noise.h"
#include "../../core/math_util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 环位：单位圆半径比例 */
static const float RING_LO[4] = { 0.00f, 0.26f, 0.58f, 0.00f };
static const float RING_HI[4] = { 0.40f, 0.72f, 0.97f, 0.95f };

/* 轮廓起伏与边缘过渡宽度 */
#define SHAPE_WAVE   0.20f
#define EDGE_BAND    0.16f

static float unit_of(uint32_t h) {
    return (float)(h & 0xFFFFu) / 65536.0f;
}

/* 径向轮廓：随角度起伏的闭合曲线 */
static float shape_radius(const QlcgGuildShape *s, float theta) {
    if (s->ambient) {
        const float tau = 2.0f * QLCG_PI;
        float turn = tau * unit_of(s->seed);
        float lobes = tau * unit_of(s->seed >> 16);
        return s->r * (0.71f + 0.14f * cosf(2.0f * theta + turn)
            + 0.09f * cosf(3.0f * theta + lobes)
            + 0.05f * sinf(5.0f * theta + turn + lobes));
    }
    float cs = cosf(theta), sn = sinf(theta);
    double n = qlcg_base_noise_d(cs * 1.35 + s->cx * 0.013, 0.0,
                                 sn * 1.35 + s->cz * 0.011, s->seed);
    return s->r * (1.0f - SHAPE_WAVE + SHAPE_WAVE * (float)(n * 0.5 + 0.5));
}

/* 边缘按概率保留，避免硬切 */
static int edge_keep(float d, uint32_t h) {
    float k = (1.0f - d) / EDGE_BAND;
    if (k >= 1.0f) return 1;
    if (k <= 0.0f) return 0;
    return unit_of(qlcg_hash_u32(h ^ 0x65646765u)) < k;
}

static void tier_range(int tier, float *lo, float *hi) {
    switch (tier) {
        case QLCG_GUILD_TIER_GROUND: *lo = 4.0f;  *hi = 8.0f;  break;
        case QLCG_GUILD_TIER_TREE:   *lo = 10.0f; *hi = 16.0f; break;
        default:                     *lo = 6.0f;  *hi = 12.0f; break;
    }
}

static uint32_t cell_seed(const MacroWorld *w, int cx, int cz, uint32_t salt) {
    return qlcg_hash_u32((uint32_t)cx * 73856093u ^ (uint32_t)cz * 19349663u
                         ^ w->opt.seed ^ salt);
}

/* 丛成片噪声，双频混合 */
static float guild_noise(double x, double z, uint32_t seed) {
    float a = (float)qlcg_base_noise_d(x / 72.0, 0.0, z / 72.0, seed ^ 0x6775u);
    float b = (float)qlcg_base_noise_d(x / 27.0, 0.0, z / 27.0, seed ^ 0x6776u);
    return a * 0.65f + b * 0.35f;
}

/* 极坐标取点，返回单位圆内比例 */
static void polar_point(uint32_t state, int ring, float *ox, float *oz) {
    int r = (ring < 0 || ring > 3) ? 3 : ring;
    float lo = RING_LO[r], hi = RING_HI[r];
    float a = unit_of(state) * 6.28318f;
    float b = lo + (hi - lo) * unit_of(state >> 16);
    *ox = cosf(a) * b;
    *oz = sinf(a) * b;
}

/* 单株落地：水陆、几何与三角预算 */
static bool chunk_owns(float x, float z) {
    const float half = CHUNK_SIZE * 0.5f;
    return x >= -half && x < half && z >= -half && z < half;
}

static int try_place(RegionData *reg, const QlcgClimateSeason *cs,
                     const GameSettings *settings, int species,
                     float wx, float wz, uint32_t roll,
                     float scale_lo, float scale_hi, bool owned, int *plants, int *tris) {
    if (*plants >= QLCG_GUILD_PLANTS_MAX) return 0;
    const PdSpecies *sp = qlcg_pd_species(species);
    if (!sp) return 0;
    float span = (float)(reg->width - 1) * reg->step;
    float half = span * 0.5f;
    float lx = wx - reg->center_x;
    float lz = wz - reg->center_z;
    if (owned && !chunk_owns(lx, lz)) return 0;
    if (lx < -half || lz < -half || lx >= half || lz >= half) return 0;

    float bed = qlcg_region_height_at(reg, lx, lz);
    float water = qlcg_region_water_at(reg, lx, lz);
    float depth = water - bed;
    int aquatic = depth > 0.15f;
    int water_plant = sp->category == PLANT_CAT_SHUI;
    if (water_plant) {
        if (!aquatic || depth >= 2.2f) return 0;
    } else if (aquatic || bed <= water + 0.2f) {
        return 0;
    }

    int ix = qlcg_clampi((int)floorf((lx + half) / reg->step + 0.5f), 0, reg->width - 1);
    int iz = qlcg_clampi((int)floorf((lz + half) / reg->step + 0.5f), 0, reg->height - 1);
    int idx = iz * reg->width + ix;
    float temp = reg->temperatures ? reg->temperatures[idx] : 15.0f;

    int visual_season = qlcg_plant_visual_season(cs, temp, roll);
    int form = settings ? (int)settings->vegetation_form : VEG_FORM_NORMAL;
    int variant = 0;
    if (form == VEG_FORM_RICH && qlcg_plant_category_has_variants(sp->category))
        variant = (int)((roll >> 16) % 3u);
    const LpGeometry *geo = qlcg_plant_geometry(species,
        qlcg_plant_model_form(form, (unsigned)variant), visual_season);
    if (!geo || geo->height <= 0.01) return 0;

    int tri = geo->triangle_count > 0 ? geo->triangle_count : 1;
    if (*tris + tri > QLCG_GUILD_TRIS_MAX) return 0;
    *tris += tri;

    float scale = scale_lo + (scale_hi - scale_lo) * unit_of(qlcg_hash_u32(roll ^ 0x5ca1eu));
    int floating = water_plant && qlcg_pd_has_tag(sp, "fushui");
    float y = aquatic ? (floating ? water - 0.01f : bed - 0.06f) : bed;
    float rot = unit_of(qlcg_hash_u32(roll ^ 0x726f74u)) * 6.28318f;

    PropInstance *p = qlcg_region_add_plant(reg, species, sp->category, aquatic,
        lx, lz, y, rot,
        (float)geo->radius * 2.0f * scale,
        (float)geo->height * scale,
        visual_season, variant, roll);
    if (!p) return 0;
    (*plants)++;
    return 1;
}

/* 混生丛成员布局 */
static int layout_members(RegionData *reg, const QlcgClimateSeason *cs,
                          const GameSettings *settings, const QlcgGuildDef *def,
                          const QlcgGuildShape *shape, uint32_t seed,
                          int *plants, int *tris) {
    int placed = 0;
    bool ambient = def->grass_tufts != 0;
    for (int m = 0; m < def->member_count; m++) {
        const QlcgGuildMember *mem = &def->members[m];
        if (mem->species < 0 || mem->attachment != GUILD_GROUND) continue;
        const PdSpecies *sp = qlcg_pd_species(mem->species);
        if (!sp) continue;
        int span = (int)mem->n_max - (int)mem->n_min + 1;
        if (span < 1) span = 1;
        uint32_t hm = qlcg_hash_u32(seed ^ ((uint32_t)(m + 1) * 0x9E3779B9u));
        int n = (int)mem->n_min + (int)(hm % (uint32_t)span);
        if (n <= 0) continue;
        if (ambient && (sp->category == PLANT_CAT_SHU || sp->category == PLANT_CAT_TENG) && n == 1) {
            placed += try_place(reg, cs, settings, mem->species, shape->cx, shape->cz, hm,
                                def->scale_lo, def->scale_hi, true, plants, tris);
            continue;
        }
        int clusters = mem->clump && n > 1 ? (n + 5) / 6 : 1;
        if (clusters < 1) clusters = 1;
        if (clusters > 4) clusters = 4;
        int left = n;
        for (int c = 0; c < clusters && left > 0; c++) {
            uint32_t hc = qlcg_hash_u32(hm ^ ((uint32_t)(c + 1) * 0x85EBCA6Bu));
            float ox, oz;
            polar_point(hc, mem->ring, &ox, &oz);
            float theta = atan2f(oz, ox);
            float rr = shape_radius(shape, theta);
            float spread = fminf(2.6f, rr * 0.4f);
            int per = (left + (clusters - c) - 1) / (clusters - c);
            for (int i = 0; i < per && left > 0; i++, left--) {
                uint32_t hp = qlcg_hash_u32(hc ^ ((uint32_t)(i + 1) * 0xC2B2AE35u));
                float ang = unit_of(hp) * 6.28318f;
                float dist = unit_of(hp >> 16) * spread;
                float px = ox * rr + cosf(ang) * dist;
                float pz = oz * rr + sinf(ang) * dist;
                float d = sqrtf(px * px + pz * pz) / fmaxf(shape_radius(shape, atan2f(pz, px)), 0.5f);
                if (!edge_keep(d, hp)) continue;
                placed += try_place(reg, cs, settings, mem->species,
                                    shape->cx + px, shape->cz + pz, hp,
                                    ambient ? def->scale_lo : 1.0f,
                                    ambient ? def->scale_hi : 1.35f, ambient, plants, tris);
            }
        }
    }
    return placed;
}

/* 优势丛：纯乔木，方格、成行或散点 */
static int layout_mono(RegionData *reg, const QlcgClimateSeason *cs,
                       const GameSettings *settings, const QlcgGuildDef *def,
                       const QlcgGuildShape *shape, uint32_t seed,
                       int *plants, int *tris) {
    if (def->species < 0) return 0;
    float lo = def->scale_lo > 0.0f ? def->scale_lo : 0.9f;
    float hi = def->scale_hi > 0.0f ? def->scale_hi : 1.15f;
    float lim = shape->r * 0.95f;
    int placed = 0;

    if (def->layout == QLCG_GUILD_LAYOUT_GRID || def->layout == QLCG_GUILD_LAYOUT_ROWS) {
        int rows_mode = def->layout == QLCG_GUILD_LAYOUT_ROWS;
        float gz_step = rows_mode ? def->row_gap : def->spacing;
        float gx_step = rows_mode ? def->plant_gap : def->spacing;
        if (gz_step < 0.5f) gz_step = 0.5f;
        if (gx_step < 0.4f) gx_step = 0.4f;
        float jr = def->jitter;
        int rows = (int)((lim * 2.0f) / gz_step) + 1;
        int cols = (int)((lim * 2.0f) / gx_step) + 1;
        for (int j = 0; j < rows; j++) {
            for (int i = 0; i < cols; i++) {
                uint32_t hp = qlcg_hash_u32(seed ^
                    ((uint32_t)(j * 4096 + i) * 0x9E3779B9u));
                float x = -lim + (float)i * gx_step;
                float z = -lim + (float)j * gz_step;
                x += (unit_of(hp) * 2.0f - 1.0f) * jr * gx_step;
                z += (unit_of(hp >> 16) * 2.0f - 1.0f) * jr * gz_step;
                float rr = shape_radius(shape, atan2f(z, x));
                float d = sqrtf(x * x + z * z) / fmaxf(rr, 0.5f);
                if (d >= 1.0f || !edge_keep(d, hp)) continue;
                placed += try_place(reg, cs, settings, def->species,
                                    shape->cx + x, shape->cz + z, hp, lo, hi,
                                    false, plants, tris);
            }
        }
    } else {
        int n0 = def->n_min > 0 ? def->n_min : 8;
        int n1 = def->n_max >= n0 ? def->n_max : n0;
        int n = n0 + (int)(qlcg_hash_u32(seed ^ 0x51a3u) % (uint32_t)(n1 - n0 + 1));
        float gap = def->min_gap > 0.2f ? def->min_gap : 0.2f;
        float px[64], pz[64];
        int used = 0;
        for (int a = 0; a < n * 8 && used < n && used < 64; a++) {
            uint32_t hp = qlcg_hash_u32(seed ^ ((uint32_t)(a + 1) * 0x85EBCA6Bu));
            float x = (unit_of(hp) * 2.0f - 1.0f) * lim;
            float z = (unit_of(hp >> 16) * 2.0f - 1.0f) * lim;
            float rr = shape_radius(shape, atan2f(z, x));
            float d = sqrtf(x * x + z * z) / fmaxf(rr, 0.5f);
            if (d >= 1.0f || !edge_keep(d, hp)) continue;
            int ok = 1;
            for (int k = 0; k < used; k++) {
                float dx = px[k] - x, dz = pz[k] - z;
                if (dx * dx + dz * dz < gap * gap) { ok = 0; break; }
            }
            if (!ok) continue;
            px[used] = x;
            pz[used] = z;
            used++;
            placed += try_place(reg, cs, settings, def->species,
                                shape->cx + x, shape->cz + z, hp, lo, hi,
                                false, plants, tris);
        }
    }
    return placed;
}

int qlcg_aquatic_guild_site(const RegionData *reg, float x, float z, int kind,
                           float *bed, float *depth) {
    if (!chunk_owns(x, z) || !reg->water_kind ||
        (kind != WATER_KIND_SEA && kind != WATER_KIND_LAKE &&
         kind != WATER_KIND_RIVER && kind != WATER_KIND_STATIC)) return -1;
    float half = (reg->width - 1) * reg->step * 0.5f;
    int ix = qlcg_clampi((int)roundf((x + half) / reg->step), 0, reg->width - 1);
    int iz = qlcg_clampi((int)roundf((z + half) / reg->step), 0, reg->height - 1);
    int i = iz * reg->width + ix;
    if (reg->water_kind[i] != kind) return -1;
    *bed = qlcg_region_height_at(reg, x, z);
    *depth = qlcg_region_water_at(reg, x, z) - *bed;
    float limit = kind == WATER_KIND_SEA ? 9.0f : 2.6f;
    if (!isfinite(*depth) || *depth < 0.46f || *depth > limit ||
        qlcg_region_slope_at(reg, x, z) > 1.05f) return -1;
    return i;
}

static float aquatic_grass_scale(float depth, int kind, uint32_t seed) {
    uint32_t lengths = qlcg_hash_u32(seed ^ 0x6c656166u);
    float group = unit_of(lengths);
    float v = unit_of(lengths >> 16);
    float wave = kind == WATER_KIND_SEA || kind == WATER_KIND_RIVER ? 0.32f : 0.20f;
    float clearance = 0.035f + wave * fminf(1.0f, depth / 1.8f);
    float span = depth - clearance;
    float height;
    if (group < 0.12f)
        height = fminf(qlcg_lerpf(0.36f, 1.18f, v), span * qlcg_lerpf(0.34f, 0.57f, v));
    else if (group < 0.31f)
        height = fminf(qlcg_lerpf(1.18f, 2.75f, v), span * qlcg_lerpf(0.65f, 0.84f, v));
    else if (group < 0.73f)
        height = fminf(qlcg_lerpf(2.65f, 5.70f, v), span * qlcg_lerpf(0.84f, 0.97f, v));
    else
        height = span * qlcg_lerpf(0.96f, 0.998f, v);
    return height / 0.86f;
}

static bool place_grass(RegionData *reg, float x, float z, float scale,
                        uint32_t seed, uint8_t rank, int kind) {
    if (!chunk_owns(x, z)) return true;
    float bed, depth;
    if (kind != WATER_KIND_DRY) {
        if (qlcg_aquatic_guild_site(reg, x, z, kind, &bed, &depth) < 0) return true;
        scale *= aquatic_grass_scale(depth, kind, seed);
    } else {
        bed = qlcg_region_height_at(reg, x, z);
        if (!isfinite(bed) || bed <= qlcg_region_water_at(reg, x, z) + 0.05f ||
            qlcg_region_slope_at(reg, x, z) > 1.1f) return true;
    }
    if (reg->grass_count == reg->grass_cap) {
        int cap = reg->grass_cap ? reg->grass_cap * 2 : 64;
        GrassTuft *grass = realloc(reg->grass, (size_t)cap * sizeof(*grass));
        if (!grass) return false;
        reg->grass = grass;
        reg->grass_cap = cap;
    }
    reg->grass[reg->grass_count++] = (GrassTuft){x, bed, z, scale, seed, rank, (uint8_t)kind};
    return true;
}

static void layout_grass(RegionData *reg, const QlcgGuildShape *shape,
                         int tufts, float grass_scale, float clearance, int kind) {
    float lx = shape->cx - reg->center_x, lz = shape->cz - reg->center_z;
    for (int i = 0; i < tufts; i += 7) {
        uint32_t patch = qlcg_hash_u32(shape->seed ^ (uint32_t)i * 0x85EBCA6Bu ^ 0x67726173u);
        float angle = unit_of(patch) * (2.0f * QLCG_PI);
        float radius = shape_radius(shape, angle);
        float inner = fmaxf(radius * 0.16f, clearance);
        float outer = radius * 0.96f;
        if (inner >= outer) continue;
        float distance = sqrtf(qlcg_lerpf(inner * inner, outer * outer, unit_of(patch >> 16)));
        float cx = cosf(angle) * distance, cz = sinf(angle) * distance;
        uint32_t site_rank = qlcg_hash_u32(patch ^ 0x72616e6bu) % 255u;
        for (int member = 0; member < 7 && i + member < tufts; member++) {
            uint32_t h = qlcg_hash_u32(patch ^ (uint32_t)(member + 1) * 0x9E3779B9u);
            float a = unit_of(h) * (2.0f * QLCG_PI);
            float spread = unit_of(h >> 16) * radius * 0.31f;
            float dx = cx + cosf(a) * spread, dz = cz + sinf(a) * spread;
            float dist2 = dx * dx + dz * dz;
            if (dist2 > shape->r * shape->r || dist2 < clearance * clearance) continue;
            float radial = sqrtf(dist2) / shape_radius(shape, atan2f(dz, dx));
            float fade = qlcg_smoothf(0.70f, 1.0f, radial);
            if (radial >= 1.0f || (member != 0 && unit_of(qlcg_hash_u32(h ^ 0x65646765u)) < fade))
                continue;
            float scale = kind == WATER_KIND_DRY ? grass_scale * (0.68f + 0.62f * unit_of(h >> 8)) : 1.0f;
            if (kind == WATER_KIND_SEA && clearance >= 0.50f)
                scale *= qlcg_lerpf(0.55f, 1.0f,
                    qlcg_smoothf(clearance, clearance + 0.85f, sqrtf(dist2)));
            bool permanent = i % 14 == 0 && member == 0;
            uint8_t rank = permanent ? 0 : (uint8_t)(1 + (site_rank + (h % 53u)) % 255u);
            if (!place_grass(reg, lx + dx, lz + dz, scale, h, rank, kind)) return;
        }
    }
}

int qlcg_aquatic_guild_grass(RegionData *reg, float x, float z, int kind, int zone,
                            float clearance, uint32_t seed) {
    QlcgGuildShape shape = {
        .cx = reg->center_x + x, .cz = reg->center_z + z,
        .r = qlcg_lerpf(2.30f, 3.05f, unit_of(seed >> 16)), .seed = seed, .ambient = 1
    };
    bool sea = kind == WATER_KIND_SEA;
    float density = 1.0f;
    const QlcgClimateZoneInfo *climate = qlcg_climate_zone_info(zone);
    if (climate) {
        static const float heat_density[] = {1.20f, 1.12f, 1.00f, 0.67f, 0.40f};
        density = heat_density[climate->heat];
        if (!sea) density *= 0.89f + 0.0375f * climate->wet;
    }
    int tufts = (int)roundf(((sea ? 112 : 91) + (int)(seed % 4u) * 7) * density);
    int first = reg->grass_count;
    layout_grass(reg, &shape, tufts, 1.0f, clearance, kind);
    return reg->grass_count - first;
}

static bool guild_rock_ground(const RegionData *reg, const MacroWorld *w,
                              float x, float z, float *bed) {
    float wx = reg->center_x + x, wz = reg->center_z + z;
    if (qlcg_headwater_excludes(w, wx, wz, 4)) return false;
    float half = (reg->width - 1) * reg->step * 0.5f;
    if (fabsf(x) <= half && fabsf(z) <= half) {
        *bed = qlcg_region_height_at(reg, x, z);
        return isfinite(*bed) && *bed > qlcg_region_water_at(reg, x, z) + 0.2f &&
            qlcg_region_slope_at(reg, x, z) <= 1.1f;
    }
    SurfaceMapPoint point;
    qlcg_sample_surface_detail_map(w, wx, wz, &point);
    *bed = point.h;
    return isfinite(*bed) && *bed > point.wl + 0.2f;
}

float qlcg_guild_radius(const QlcgGuildDef *def, uint32_t roll) {
    float lo, hi;
    tier_range(def->tier, &lo, &hi);
    float t = unit_of(roll);
    float size = lo + (hi - lo) * (def->mono ? (0.55f + 0.45f * t) : t);
    return floorf(size * 2.0f + 0.5f) * 0.25f;
}

/* 群系占宿主：先复制宿主再追加子项 */
static void attach_members(RegionData *reg,const MacroWorld *world,const QlcgGuildDef *def,
                           const GameSettings *settings,const PropInstance *wood,int first,int season,
                           uint32_t seed,int *plants,int *tris) {
    const int last=reg->tree_count;
    int plant_cursor=0,wood_cursor=0;
    for(int m=0;m<def->member_count;m++) {
        const QlcgGuildMember *member=def->members+m;
        if (!member->attachment) continue;
        const bool log=member->attachment==GUILD_ON_WOOD;
        const uint32_t roll=qlcg_hash_u32(seed^(uint32_t)(m+1)*0x9e3779b9u);
        int wanted=member->n_min+roll%(member->n_max-member->n_min+1);
        int *cursor=log?&wood_cursor:&plant_cursor, ordinal=0;
        for(int h=log?0:first; h<(log?(wood!=NULL):last) && wanted>0;h++) {
            PropInstance host=log?*wood:reg->trees[h];
            const LpSocket *sockets; int n;
            float scale;
            int visual=log?season:host.visual_season;
            int setting=settings?settings->vegetation_form:VEG_FORM_NORMAL;
            if (log) {
                const RockGeometry *g=qlcg_rock_geometry(host.archetype,ROCK_FORM_OUTCROP,visual,1);
                if (!g) continue;
                sockets=g->sockets;n=g->socket_count;scale=qlcg_rock_instance_scale(host.scale_x,g->radius);
            } else {
                if (host.category!=PLANT_CAT_SHU && host.category!=PLANT_CAT_GUAN) continue;
                const LpGeometry *g=qlcg_plant_geometry(host.species,qlcg_plant_model_form(setting,host.shape_variant),visual);
                if (!g) continue;
                sockets=g->sockets;n=g->socket_count;scale=qlcg_plant_instance_scale(host.scale_y,g->height);
            }
            int host_kind=log?CHD_PROP_ROCK_BASE+host.archetype:CHD_PROP_PLANT_BASE+host.species;
            if(world && world->chunk_deltas && qlcg_chd_prop_state(world,reg->center_x+host.x,reg->center_z+host.z,host_kind)==CHD_PROP_REMOVED) continue;
            float c=cosf(host.rot_y),s=sinf(host.rot_y);
            for(int j=0;j<n && wanted>0;j++){
                const LpSocket *site=sockets+(j+seed%n)%n;
                if (site->kind!=(log?LP_SOCKET_FUNGUS:LP_SOCKET_CLING) || ordinal++ < *cursor) continue;
                *cursor=ordinal;
                uint32_t pose=qlcg_hash_u32(roll^(uint32_t)ordinal);
                unsigned variant=setting==VEG_FORM_RICH?pose%3u:0;
                int form=qlcg_plant_placed_form(qlcg_plant_model_form(setting,variant),1);
                const LpGeometry *g=qlcg_plant_geometry(member->species,form,visual);
                if (!g || *plants>=QLCG_GUILD_PLANTS_MAX || *tris+g->triangle_count>QLCG_GUILD_TRIS_MAX) continue;
                float dx=site->translation[0], dz=site->translation[2];
                float x=host.x+(c*dx-s*dz)*scale,z=host.z+(s*dx+c*dz)*scale;
                float y=host.y+site->translation[1]*scale;
                float yaw=host.rot_y+atan2f(dz,log?0:dx);
                float size=.85f+.30f*unit_of(pose>>8);
                PropInstance *child=qlcg_region_add_plant(reg,member->species,qlcg_plant_species_category(member->species),
                    0,x,z,y,yaw,2*g->radius*size,g->height*size,visual,variant,pose);
                if (!child) return;
                child->hanging=1;(*plants)++;*tris+=g->triangle_count;wanted--;
            }
        }
    }
}

bool qlcg_guild_place(RegionData *reg, const MacroWorld *w, const GameSettings *settings,
                      const QlcgClimateSeason *cs, const QlcgGuildDef *def,
                      const QlcgGuildShape *shape, int season, int *plants, int *tris) {
    int first=reg->tree_count;
    PropInstance *wood=NULL;
    if (def->rock >= 0) {
        float x = shape->cx - reg->center_x, z = shape->cz - reg->center_z, bed;
        if (!guild_rock_ground(reg, w, x, z, &bed)) return false;
        if (chunk_owns(x,z)) {
            wood=qlcg_region_add_rock(reg,def->rock,x,z,bed,shape->seed,season);
            if (!wood) return false;
        }
        layout_grass(reg, shape, def->grass_tufts, def->grass_scale, shape->r * 0.38f, WATER_KIND_DRY);
    } else {
        if (def->mono) layout_mono(reg, cs, settings, def, shape, shape->seed, plants, tris);
        else layout_members(reg, cs, settings, def, shape, shape->seed, plants, tris);
        if (def->grass_tufts)
            layout_grass(reg, shape, def->grass_tufts, def->grass_scale, 0.0f, WATER_KIND_DRY);
    }
    attach_members(reg,w,def,settings,wood,first,season,shape->seed,plants,tris);
    return true;
}

int qlcg_guild_query(const MacroWorld *w, int cx, int cz, QlcgGuildShape *out) {
    if (!w || !out) return -1;
    float ccx = ((float)cx + 0.5f) * QLCG_GUILD_CELL;
    float ccz = ((float)cz + 0.5f) * QLCG_GUILD_CELL;
    float rate_roll = unit_of(cell_seed(w, cx, cz, 0x11u));
    if (rate_roll >= QLCG_GUILD_RATE) return -1;
    if (guild_noise(ccx, ccz, w->opt.seed) < QLCG_GUILD_NOISE) return -1;
    int zone = qlcg_climate_zone_at(w, ccx, ccz);
    if (zone < 0 || zone >= QLCG_ZONE_COUNT) return -1;
    if (zone == QLCG_ZONE_ICE_CAP && rate_roll >= 0.30f) return -1;

    int mono_pick = qlcg_pg_pick(zone, 1, cell_seed(w, cx, cz, 0x21u));
    int mixed_pick = qlcg_pg_pick(zone, 0, cell_seed(w, cx, cz, 0x22u));
    int use_mono;
    if (mono_pick < 0) use_mono = 0;
    else if (mixed_pick < 0) use_mono = 1;
    else use_mono = unit_of(cell_seed(w, cx, cz, 0x23u)) < QLCG_GUILD_MONO_SHARE;
    int idx = use_mono ? mono_pick : mixed_pick;
    if (idx < 0) return -1;
    const QlcgGuildDef *def = qlcg_pg_guild(idx);
    if (!def) return -1;

    uint32_t hpos = cell_seed(w, cx, cz, 0x32u);
    float gx = (float)cx * QLCG_GUILD_CELL + 0.5f
             + unit_of(hpos) * (QLCG_GUILD_CELL - 1.0f);
    float gz = (float)cz * QLCG_GUILD_CELL + 0.5f
             + unit_of(hpos >> 16) * (QLCG_GUILD_CELL - 1.0f);
    out->cx = gx;
    out->cz = gz;
    out->r = qlcg_guild_radius(def, cell_seed(w, cx, cz, 0x31u));
    out->seed = cell_seed(w, cx, cz, 0x33u);
    out->species = def->species;
    out->mono = def->mono;
    out->ambient = def->grass_tufts != 0;

    return idx;
}

int qlcg_guild_scatter(RegionData *reg, const MacroWorld *w,
                       int season_idx, const GameSettings *settings,
                       const QlcgClimateSeason *cs,
                       QlcgGuildShape *shapes, int shape_cap) {
    if (!reg || !w) return 0;
    if (qlcg_plant_species_count() <= 0 || qlcg_pg_count() <= 0) return 0;

    float span = (float)(reg->width - 1) * reg->step;
    float x0 = reg->origin_x, z0 = reg->origin_z;
    float x1 = x0 + span, z1 = z0 + span;
    int cx0 = (int)floorf(x0 / QLCG_GUILD_CELL);
    int cx1 = (int)floorf((x1 - 0.001f) / QLCG_GUILD_CELL);
    int cz0 = (int)floorf(z0 / QLCG_GUILD_CELL);
    int cz1 = (int)floorf((z1 - 0.001f) / QLCG_GUILD_CELL);
    const float margin = 8.0f;
    int first_x = (int)floorf((x0 - margin) / QLCG_GUILD_CELL);
    int last_x = (int)floorf((x1 + margin) / QLCG_GUILD_CELL);
    int first_z = (int)floorf((z0 - margin) / QLCG_GUILD_CELL);
    int last_z = (int)floorf((z1 + margin) / QLCG_GUILD_CELL);

    int used = 0;
    int plants = 0, tris = 0;
    for (int cz = first_z; cz <= last_z; cz++) {
        for (int cx = first_x; cx <= last_x; cx++) {
            QlcgGuildShape shape;
            int idx = qlcg_guild_query(w,cx,cz,&shape);
            const QlcgGuildDef *def = qlcg_pg_guild(idx);
            if (!def) continue;
            if (!def->grass_tufts && (cx < cx0 || cx > cx1 || cz < cz0 || cz > cz1)) continue;
            if (shape.cx + shape.r < x0 || shape.cx - shape.r > x1) continue;
            if (shape.cz + shape.r < z0 || shape.cz - shape.r > z1) continue;

            if (cs && !qlcg_guild_place(reg, w, settings, cs, def, &shape,
                                        season_idx, &plants, &tris)) continue;
            if (shapes && used < shape_cap) shapes[used] = shape;
            if (used < shape_cap) used++;
        }
    }
    return used;
}

int qlcg_guild_inside(const QlcgGuildShape *shapes, int count,
                      float wx, float wz, float pad) {
    if (!shapes) return 0;
    for (int i = 0; i < count; i++) {
        float dx = wx - shapes[i].cx;
        float dz = wz - shapes[i].cz;
        if (dx * dx + dz * dz > (shapes[i].r + pad) * (shapes[i].r + pad)) continue;
        float rr = shape_radius(&shapes[i], atan2f(dz, dx)) + pad;
        if (dx * dx + dz * dz <= rr * rr) return 1;
    }
    return 0;
}
