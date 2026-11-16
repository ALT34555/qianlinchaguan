/* 丛格判定、圆盘轮廓与丛内布局 */
#include "surface_guild.h"
#include "surface_internal.h"
#include "plant_distribution.h"
#include "climate_zone.h"
#include "model/plant_guild.h"
#include "model/plant_material.h"
#include "model/plant_forms.h"
#include "model/plant_def.h"
#include "model/plant_phenology.h"
#include "gen/gen_noise.h"
#include "../../core/math_util.h"

#include <math.h>
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
static int try_place(RegionData *reg, const QlcgClimateSeason *cs,
                     const GameSettings *settings, int species,
                     float wx, float wz, uint32_t roll,
                     float scale_lo, float scale_hi, int *plants, int *tris) {
    if (*plants >= QLCG_GUILD_PLANTS_MAX) return 0;
    const PdSpecies *sp = qlcg_pd_species(species);
    if (!sp) return 0;
    float span = (float)(reg->width - 1) * reg->step;
    float half = span * 0.5f;
    float lx = wx - reg->center_x;
    float lz = wz - reg->center_z;
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

/* 混生丛：单乔木主种 + 共生灌木 */
static int layout_members(RegionData *reg, const QlcgClimateSeason *cs,
                          const GameSettings *settings, const QlcgGuildDef *def,
                          const QlcgGuildShape *shape, uint32_t seed,
                          int *plants, int *tris) {
    int placed = 0;
    for (int m = 0; m < def->member_count; m++) {
        const QlcgGuildMember *mem = &def->members[m];
        if (mem->species < 0) continue;
        const PdSpecies *sp = qlcg_pd_species(mem->species);
        if (!sp) continue;
        int span = (int)mem->n_max - (int)mem->n_min + 1;
        if (span < 1) span = 1;
        uint32_t hm = qlcg_hash_u32(seed ^ ((uint32_t)(m + 1) * 0x9E3779B9u));
        int n = (int)mem->n_min + (int)(hm % (uint32_t)span);
        if (n <= 0) continue;
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
                                    1.0f, 1.35f, plants, tris);
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
                                    plants, tris);
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
                                plants, tris);
        }
    }
    return placed;
}

int qlcg_guild_query(const MacroWorld *w, int cx, int cz, QlcgGuildShape *out) {
    if (!w || !out) return -1;
    float ccx = ((float)cx + 0.5f) * QLCG_GUILD_CELL;
    float ccz = ((float)cz + 0.5f) * QLCG_GUILD_CELL;
    if (guild_noise(ccx, ccz, w->opt.seed) < QLCG_GUILD_NOISE) return -1;
    if (unit_of(cell_seed(w, cx, cz, 0x11u)) >= QLCG_GUILD_RATE) return -1;
    int zone = qlcg_climate_zone_at(w, ccx, ccz);
    if (zone < 0 || zone >= QLCG_ZONE_COUNT) return -1;

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

    float lo, hi;
    tier_range(def->tier, &lo, &hi);
    float t = unit_of(cell_seed(w, cx, cz, 0x31u));
    float size = lo + (hi - lo) * (def->mono ? (0.55f + 0.45f * t) : t);
    size = floorf(size * 2.0f + 0.5f) * 0.5f;
    float radius = size * 0.5f;

    uint32_t hpos = cell_seed(w, cx, cz, 0x32u);
    float gx = (float)cx * QLCG_GUILD_CELL + 0.5f
             + unit_of(hpos) * (QLCG_GUILD_CELL - 1.0f);
    float gz = (float)cz * QLCG_GUILD_CELL + 0.5f
             + unit_of(hpos >> 16) * (QLCG_GUILD_CELL - 1.0f);
    out->cx = gx;
    out->cz = gz;
    out->r = radius;
    out->seed = cell_seed(w, cx, cz, 0x33u);
    out->species = def->species;
    out->mono = def->mono;

    return idx;
}

int qlcg_guild_scatter(RegionData *reg, const MacroWorld *w,
                       int season_idx, const GameSettings *settings,
                       const QlcgClimateSeason *cs,
                       QlcgGuildShape *shapes, int shape_cap) {
    if (!reg || !w || !cs) return 0;
    if (qlcg_plant_species_count() <= 0 || qlcg_pg_count() <= 0) return 0;

    float span = (float)(reg->width - 1) * reg->step;
    float x0 = reg->origin_x, z0 = reg->origin_z;
    float x1 = x0 + span, z1 = z0 + span;
    int cx0 = (int)floorf(x0 / QLCG_GUILD_CELL);
    int cx1 = (int)floorf((x1 - 0.001f) / QLCG_GUILD_CELL);
    int cz0 = (int)floorf(z0 / QLCG_GUILD_CELL);
    int cz1 = (int)floorf((z1 - 0.001f) / QLCG_GUILD_CELL);

    int used = 0;
    int plants = 0, tris = 0;
    for (int cz = cz0; cz <= cz1; cz++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            QlcgGuildShape shape;
            int idx = qlcg_guild_query(w,cx,cz,&shape);
            const QlcgGuildDef *def = qlcg_pg_guild(idx);
            if (!def) continue;
            if (shape.cx + shape.r < x0 || shape.cx - shape.r > x1) continue;
            if (shape.cz + shape.r < z0 || shape.cz - shape.r > z1) continue;

            if (shapes && used < shape_cap) shapes[used] = shape;
            if (used < shape_cap) used++;

            uint32_t hseed = shape.seed;
            if (def->mono)
                layout_mono(reg, cs, settings, def, &shape, hseed, &plants, &tris);
            else
                layout_members(reg, cs, settings, def, &shape, hseed, &plants, &tris);

            (void)season_idx;
            if (plants >= QLCG_GUILD_PLANTS_MAX || tris >= QLCG_GUILD_TRIS_MAX)
                return used;
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
