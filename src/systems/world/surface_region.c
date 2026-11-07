/* 区域着色与植被散布 */
#include "planet_macro.h"
#include "surface_internal.h"
#include "surface_stream.h"
#include "source_rivers.h"
#include "ice_runtime.h"
#include "faction_naming.h"
#include "gen/gen_palette.h"
#include "gen/gen_cover.h"
#include "../../core/math_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "model/plant_material.h"
#include "model/plant_def.h"
#include "model/plant_phenology.h"
#include "model/rock_material.h"
#include "model/rock_def.h"
#include "plant_distribution.h"
#include "rock_distribution.h"

/* 林漠修正后的有效干燥度 */
static float effective_aridity(const RegionData *reg, int idx) {
    float arid = reg->aridities ? reg->aridities[idx] : 0.65f;
    float wet  = reg->wetness ? reg->wetness[idx] : 0.0f;
    float des  = reg->desert ? reg->desert[idx] : 0.0f;
    float frs  = reg->forest ? reg->forest[idx] : 0.0f;
    arid *= 1.0f - .35f * frs - .15f * wet + .50f * des;
    return fmaxf(.05f, arid);
}

/* 区块类型→站点基质；无效按水陆回退 */
static int site_material(const MacroWorld *w, int cell, int water_kind, int saline) {
    if (water_kind == WATER_KIND_SEA) return 4;
    int type = 0;
    if (w && w->chunk_type && cell >= 0 && cell < w->cell_count)
        type = w->chunk_type[cell];
    int mat = qlcg_chunk_material(type);
    if (mat >= 1 && mat <= 5) return mat;
    if (water_kind == WATER_KIND_LAKE) return saline ? 3 : 2;
    return 2;
}

/* 站点盐碱：基质、宏观盐湖、海水、旱地积盐 */
static int site_saline(const MacroWorld *w, int cell, int material,
                       int water_kind, float arid, float wet) {
    if (material == 3) return 1;
    if (water_kind == WATER_KIND_SEA) return 1;
    if (w && w->saline && cell >= 0 && cell < w->cell_count && w->saline[cell])
        return 1;
    float a = qlcg_clampf((arid - .9f) / .4f, 0.0f, 1.0f);
    float b = qlcg_clampf((.5f - wet) / .35f, 0.0f, 1.0f);
    return a * b > .5f;
}

/* 填站点快照；水深由调用方追加 */
static void fill_site(PlantSite *site, const RegionData *reg, const MacroWorld *w,
                      int idx, int cell, float slope, int water_kind) {
    memset(site, 0, sizeof(*site));
    site->temperature = reg->temperatures ? reg->temperatures[idx] : 15.0f;
    site->wetness     = reg->wetness ? reg->wetness[idx] : 0.5f;
    site->desert      = reg->desert ? reg->desert[idx] : 0.0f;
    site->forest      = reg->forest ? reg->forest[idx] : 0.0f;
    site->aridity     = effective_aridity(reg, idx);
    site->slope       = slope;
    site->water_kind  = water_kind;
    site->flow = (reg->flow_x && reg->flow_z)
               ? hypotf(reg->flow_x[idx], reg->flow_z[idx]) : 0.0f;
    int salt = (water_kind == WATER_KIND_SEA) ||
               (w && w->saline && cell >= 0 && cell < w->cell_count &&
                w->saline[cell] != 0);
    int mat = site_material(w, cell, water_kind, salt);
    int fresh=reg->water_fresh && reg->water_fresh[idx];
    int source_salt=reg->source_effects?reg->source_effects[idx].salt:0;
    if (fresh && (mat==3 || water_kind==WATER_KIND_LAKE)) mat=2;
    if (reg->river_sediment && reg->river_sediment[idx]>=128) mat=4;
    salt = !fresh && site_saline(w, cell, mat, water_kind, site->aridity, site->wetness);
    if(source_salt>=48&&!fresh) { mat=3; salt=1; }
    site->material = mat;
    site->saline = salt ? 1.0f : 0.0f;
}
void qlcg_region_recolor(RegionData *reg, const MacroWorld *w, int season_idx) {
    if (reg->stream) {
        RegionStream *st = reg->stream;
        for (int i = 0; i < st->count; i++)
            qlcg_region_recolor(st->slots[st->active[i]].data, w, season_idx);
        st->revision++;
        return;
    }
    reg->mesh_revision++;

    int gw = reg->width;
    int gh = reg->height;
    float span = (float)(reg->width - 1) * reg->step;
    float half = span * 0.5f;
    int season = (season_idx >= 0 && season_idx < 4) ? season_idx : -1;

    for (int z = 0; z < gh; z++) {
        for (int x = 0; x < gw; x++) {
            int idx = z * gw + x;
            float h   = reg->heights[idx];
            float wl  = reg->water_levels[idx];
            float tmp = reg->temperatures[idx];
            /* 冰不随重着色重算，交由统一冰刷新 */

            int xl = qlcg_maxf(0, x - 1), xr = qlcg_minf(gw - 1, x + 1);
            int zd = qlcg_maxf(0, z - 1), zu = qlcg_minf(gh - 1, z + 1);
            float dx = (reg->heights[z * gw + xr] - reg->heights[z * gw + xl]) / (2.0f * reg->step);
            float dz = (reg->heights[zu * gw + x] - reg->heights[zd * gw + x]) / (2.0f * reg->step);
            float slope = hypotf(dx, dz);

            /* 与舆图同一套高程色带 */
            uint8_t col[3];
            if (h < wl - 0.02f) {
                /* base finishCover 的湿泥色 */
                col[0]=117; col[1]=109; col[2]=81;
            } else {
                /* 球面坐标 → 材质色；林地更湿、荒漠更干 */
                float lx = (float)x * reg->step - half;
                float lz = (float)z * reg->step - half;
                float lon = (reg->center_x + lx) / w->circumference * 6.2831853f;
                float lat = -(reg->center_z + lz) / qlcg_world_depth(w) * 3.1415926f;
                float clat = cosf(lat);
                float arid = effective_aridity(reg, idx);
                float uplift = reg->uplifts ? reg->uplifts[idx] : 0.2f;
                CoverSeason cs = { season, lat };
                qlcg_cover_color(w, clat * cosf(lon), sinf(lat), clat * sinf(lon),
                                 tmp, arid, uplift, slope, &cs, col, NULL);
            }
            if(reg->coast_material) {
                const uint8_t *weights=reg->coast_material+idx*3;
                const unsigned total=weights[0]+weights[1]+weights[2];
                static const uint8_t coast_colors[3][3]={{153,134,94},{225,209,156},{139,148,135}};
                float exposure=qlcg_smoothf(45,145,weights[2])*qlcg_smoothf(.70f,1.55f,slope);
                static const uint8_t stone[3]={115,109,100};
                for(int c=0;total && c<3;c++) {
                    unsigned value=col[c]*(255-total);
                    for(int m=0;m<3;m++) value+=weights[m]*coast_colors[m][c];
                    col[c]=(uint8_t)qlcg_lerpf(value/255.0f,stone[c],exposure);
                }
            }
            if (reg->river_sediment && reg->river_sediment[idx]) {
                static const uint8_t sand[3]={193,171,125};
                unsigned amount=reg->river_sediment[idx];
                for (int c=0;c<3;c++) col[c]=(uint8_t)((col[c]*(255-amount)+sand[c]*amount)/255);
            }
            if(reg->source_effects && reg->source_effects[idx].salt) {
                static const uint8_t crust[3]={220,212,182};
                unsigned amount=reg->source_effects[idx].salt;
                if(amount==255) amount=122;
                for(int c=0;c<3;c++) col[c]=(uint8_t)((col[c]*(255-amount)+crust[c]*amount)/255);
            }
            reg->colors[idx * 3 + 0] = col[0];
            reg->colors[idx * 3 + 1] = col[1];
            reg->colors[idx * 3 + 2] = col[2];
        }
    }
}

static PropInstance *append_plant(RegionData *reg) {
    if (reg->tree_count == reg->tree_cap) {
        int cap = reg->tree_cap ? reg->tree_cap * 2 : 32;
        PropInstance *trees = realloc(reg->trees, (size_t)cap * sizeof(*trees));
        if (!trees) return NULL;
        reg->trees = trees;
        reg->tree_cap = cap;
    }
    return &reg->trees[reg->tree_count++];
}

/* 岩石落点复用已有网格 */
static void scatter_rock(RegionData *reg, int idx, float lx, float lz, float spacing,
                         float slope, int material, float patch, uint32_t seed, int season) {
    if (reg->rock_count >= reg->rock_cap || reg->heights[idx] <= reg->water_levels[idx] + .2f)
        return;
    RockDistribution d = qlcg_rock_distribution(material,
        reg->wetness ? reg->wetness[idx] : .5f, reg->desert ? reg->desert[idx] : 0,
        reg->temperatures ? reg->temperatures[idx] : 15, patch);
    float chance_roll = (qlcg_hash_u32(seed ^ 0x9e3779b9u) >> 8) / 16777216.0f;
    if (chance_roll >= d.chance * qlcg_rock_slope_factor(slope)) return;
    uint32_t position = qlcg_hash_u32(seed ^ 0x726f636bu);
    float x = lx + ((position & 0xffffu) / 65536.0f - .5f) * spacing * .9f;
    float z = lz + ((position >> 16) / 65536.0f - .5f) * spacing * .9f;
    float local_slope = qlcg_region_slope_at(reg, x, z);
    if (chance_roll >= d.chance * qlcg_rock_slope_factor(local_slope)) return;
    float ground = qlcg_region_height_at(reg, x, z);
    float water = qlcg_region_water_at(reg, x, z);
    if (!isfinite(ground) || !isfinite(water) || ground <= water + .2f) return;
    uint32_t size_roll = qlcg_hash_u32(seed ^ 0xd1b54a35u);
    int size = qlcg_rock_distribution_size(d.weathering, size_roll);
    int rock = qlcg_rock_pick(size, qlcg_hash_u32(seed ^ 0x85ebca6bu));
    const RockGeometry *g = qlcg_rock_geometry(rock, ROCK_FORM_OUTCROP, season, 1);
    uint32_t shape = qlcg_hash_u32(seed ^ 0xc2b2ae35u);
    float u = (shape & 0xffffu) / 65536.0f, v = (shape >> 16) / 65536.0f;
    PropInstance *rk = &reg->rocks[reg->rock_count++];
    rk->x = x; rk->z = z;
    if (g) {
        float scale = .88f + v * .24f;
        rk->y = ground;
        rk->scale_x = rk->scale_z = (float)(2.0 * g->radius) * scale;
        rk->scale_y = (float)g->height * scale;
    } else {
        rock = (int)(seed % ROCK_LEGACY_COUNT);
        rk->y = ground - .15f;
        rk->scale_x = 1.2f + u * 3.4f;
        rk->scale_y = .9f + v * 2.8f;
        rk->scale_z = 1.1f + (u + v) * 1.55f;
    }
    rk->rot_y = u * 6.28318f;
    rk->archetype = (uint8_t)rock;
    rk->r = .50f + (v - .5f) * .08f;
    rk->g = .48f + (v - .5f) * .08f;
    rk->b = .45f + (v - .5f) * .08f;
    const char *name = qlcg_rock_name(rock);
    snprintf(rk->label, sizeof(rk->label), "%s", name ? name : "山岩");
}

/* 散布植被、岩石与旗帜 */
void qlcg_region_scatter_props(RegionData *reg, const MacroWorld *w,
                               int season_idx, const GameSettings *settings) {
    if (reg->stream) {
        RegionStream *st = reg->stream;
        for (int i = 0; i < st->count; i++)
            qlcg_region_scatter_props(st->slots[st->active[i]].data, w, season_idx, settings);
        st->revision++;
        return;
    }
    /* 散布前必须已有物种表，否则物种号全为未定 */
    qlcg_plant_material_init();
    qlcg_rock_material_init();
    qlcg_plant_habitat_prepare();
    QlcgClimateSeason cs;
    if (w->plant_season_forced) {
        memset(&cs, 0, sizeof(cs));
        cs.season = qlcg_clampi(season_idx, 0, 3);
        cs.seasonless = 1;
    } else {
        qlcg_climate_season_at(w, reg->site_cell, w->plant_calendar, &cs);
    }
    reg->tree_count = 0;
    reg->rock_count = 0;
    reg->flag_count = 0;

    float span = (float)(reg->width - 1) * reg->step;
    float half = span * 0.5f;
    VegetationDensity density = qlcg_vegetation_density(settings->dense_vegetation);
    int passes = (int)ceilf(density.understory);
    const float base_density = 1.35f;
    /* 采样间距对齐 base 的 8m 区块网格 */
    int stride = qlcg_clampi((int)roundf(8.0f / reg->step), 1, 8);
    int rock_material = qlcg_chunk_material(reg->site_chunk_type);
    if (!rock_material)
        rock_material = site_material(w, reg->site_cell, WATER_KIND_DRY, 0);

    for (int gz = 3; gz < reg->height - 1; gz += stride) {
        for (int gx = 3; gx < reg->width - 1; gx += stride) {
            int idx = gz * reg->width + gx;
            float h   = reg->heights[idx];

            float lx = (float)gx * reg->step - half;
            float lz = (float)gz * reg->step - half;
            PlantPatch patch;
            qlcg_plant_patch(reg->center_x + lx, reg->center_z + lz, w->opt.seed, &patch);
            /* 世界网格确定性哈希 */
            int sx = (int)floorf((reg->center_x + lx) / 8.0f);
            int sz = (int)floorf((reg->center_z + lz) / 8.0f);
            uint32_t hsh = qlcg_hash_u32((uint32_t)sx * 73856093u ^
                                         (uint32_t)sz * 19349663u ^ w->opt.seed);

            /* 四向坡度（米/米） */
            float sl = fabsf(reg->heights[idx - 1] - h);
            float sr = fabsf(reg->heights[idx + 1] - h);
            float su = fabsf(reg->heights[idx - reg->width] - h);
            float sd = fabsf(reg->heights[idx + reg->width] - h);
            float slope = fmaxf(fmaxf(sl, sr), fmaxf(su, sd)) / reg->step;

            scatter_rock(reg, idx, lx, lz, stride * reg->step, slope,
                         rock_material, patch.rocks, hsh, season_idx);

            /* 加密复用格内地形与选种 */
            for (int pass = 0; pass < passes; pass++) {
                uint32_t roll = pass ? qlcg_hash_u32(hsh ^ ((uint32_t)pass * 0x9E3779B9u)) : hsh;
                uint32_t species_roll = qlcg_hash_u32(roll ^ 0x85EBCA6Bu);
                float pass_roll = (float)(species_roll >> 16) / 65536.0f;
                if (pass && pass_roll >= density.understory - pass)
                    continue;
                float u = (float)(roll & 0xFFFFu) / 65535.0f;
                float v = (float)(roll >> 16) / 65535.0f;
                float px = lx, pz = lz;
                int sample = idx;
                if (pass) {
                    uint32_t position_roll = qlcg_hash_u32(roll ^ 0xC2B2AE35u);
                    float offset_x = ((float)(position_roll & 0xFFFFu) / 65536.0f - 0.5f) * stride;
                    float offset_z = ((float)(position_roll >> 16) / 65536.0f - 0.5f) * stride;
                    px += offset_x * reg->step;
                    pz += offset_z * reg->step;
                    int x = qlcg_clampi(gx + (int)roundf(offset_x), 1, reg->width - 2);
                    int z = qlcg_clampi(gz + (int)roundf(offset_z), 1, reg->height - 2);
                    sample = z * reg->width + x;
                }
                float bed = reg->heights[sample];
                float water = reg->water_levels[sample];
                float depth = water - bed;
                bool aquatic = water > bed + 0.15f;
                if (aquatic) {
                    if (depth >= 2.2f || u >= 0.08f * base_density * patch.density ||
                        (reg->ice_coverage && reg->ice_coverage[sample] >= 0.5f))
                        continue;
                } else {
                    float lush = qlcg_clampf(reg->wetness[sample] * 0.75f +
                        reg->forest[sample] * 0.5f - reg->desert[sample] * 0.6f, 0.0f, 1.0f);
                    if (bed <= water + 0.2f || v >= (0.06f + lush * 0.30f) * base_density * patch.density)
                        continue;
                }
                if (!pass) {
                    px += ((aquatic ? v : u) - 0.5f) * reg->step * (aquatic ? 1.0f : 0.8f);
                    pz += ((aquatic ? u : v) - 0.5f) * reg->step * (aquatic ? 1.0f : 0.8f);
                }
                if (aquatic || pass) {
                    bed = qlcg_region_height_at(reg, px, pz);
                    water = qlcg_region_water_at(reg, px, pz);
                    depth = water - bed;
                    if (aquatic ? !(depth > 0.15f && depth < 2.2f) : water > bed)
                        continue;
                }
                float site_x = pass ? px : lx;
                float site_z = pass ? pz : lz;
                int cell = qlcg_world_cell_at_wxz(w, reg->center_x + site_x, reg->center_z + site_z);
                int kind = reg->water_kind ? reg->water_kind[sample] : WATER_KIND_DRY;
                float site_slope = pass && !aquatic ? qlcg_region_slope_at(reg, px, pz) : slope;
                if(!aquatic && site_slope>1.85f && reg->coast_material &&
                   reg->coast_material[sample*3+2]>48) continue;
                PlantSite site;
                fill_site(&site, reg, w, sample, cell, site_slope, kind);
                site.water_depth = aquatic ? depth : 0.0f;
                int sid = qlcg_plant_pick_patch(&site, species_roll,
                                               reg->center_x + px, reg->center_z + pz, &patch);
                const PdSpecies *species = qlcg_pd_species(sid);
                if (!species) continue;
                int cat = qlcg_plant_species_category(sid);
                if (pass && !aquatic && cat != PLANT_CAT_CAO && cat != PLANT_CAT_GUAN) {
                    if (cat != PLANT_CAT_SHU || pass_roll >= density.trees - pass) continue;
                }
                bool floating = aquatic && qlcg_pd_has_tag(species, "fushui");
                int visual_season = qlcg_plant_visual_season(&cs, site.temperature, roll);
                unsigned variant = 0;
                if (settings->vegetation_form == VEG_FORM_RICH && qlcg_plant_category_has_variants(cat)) {
                    uint32_t bucket = qlcg_hash_u32(roll ^ 0x73686170u) % 5;
                    variant = bucket < 3 ? 0 : bucket - 2;
                }
                int model_form = qlcg_plant_model_form(settings->vegetation_form, variant);
                const LpGeometry *geometry = qlcg_plant_geometry(sid, model_form, visual_season);
                if (!geometry || geometry->height <= 0.01) continue;
                float scale = 0.85f + v * 0.3f;
                PropInstance *p = append_plant(reg);
                if (!p) continue;
                p->x = px;
                p->z = pz;
                p->y = aquatic ? (floating ? water - 0.01f : bed - 0.06f) : bed;
                p->rot_y = (aquatic ? v : u) * 6.28318f;
                p->scale_x = p->scale_z = (float)geometry->radius * 2.0f * scale;
                p->scale_y = (float)geometry->height * scale;
                p->r = 0.24f; p->g = 0.56f; p->b = 0.30f;
                p->archetype = (uint8_t)(aquatic ? roll % 3 : (roll >> 8) % 5);
                p->model_state = PLANT_STATE_NORMAL;
                snprintf(p->label, sizeof(p->label), "%s", species->base.name ? species->base.name : species->base.id);
                p->species = (uint8_t)sid;
                p->category = cat >= 0 ? (uint8_t)cat : (aquatic ? PLANT_CAT_SHUI : PLANT_CAT_CAO);
                p->visual_season = (uint8_t)visual_season;
                p->shape_variant = (uint8_t)variant;
            }
        }
    }

    /* 散布置旗地旗帜，配色取自势力 */
    int rad_chunks = 0;
    for (int dz = -rad_chunks; dz <= rad_chunks; dz++) {
        for (int dx = -rad_chunks; dx <= rad_chunks; dx++) {
            int cx = reg->center_cx + dx;
            int cz = reg->center_cz + dz;
            ChunkInspectInfo info;
            qlcg_world_inspect_chunk(w, cx, cz, &info);
            if (info.territory && info.territory->cx == cx && info.territory->cz == cz &&
                reg->flag_count < 32) {
                float lx = (float)(dx * QLCG_CHUNK_SIZE_M);
                float lz = (float)(dz * QLCG_CHUNK_SIZE_M);
                float fy = qlcg_region_height_at(reg, lx, lz);
                PropInstance *fl = &reg->flags[reg->flag_count++];
                fl->x = lx;
                fl->z = lz;
                fl->y = fy;
                fl->scale_x = 1.0f;
                fl->scale_y = 1.0f;   /* 新旗网格已是真实米制 6.5m */
                fl->scale_z = 1.0f;
                fl->r = (float)((info.territory->main_color >> 16) & 0xFF) / 255.0f;
                fl->g = (float)((info.territory->main_color >> 8) & 0xFF) / 255.0f;
                fl->b = (float)(info.territory->main_color & 0xFF) / 255.0f;
                fl->trim_color = info.territory->trim_color;
                fl->faction_id = (int16_t)info.territory->id;
                snprintf(fl->label, sizeof(fl->label), "%s · %s",
                         qlcg_faction_display_name(info.territory),
                         (info.type_id == 9001) ? "置旗地主旗" : "有主界标");
            }
        }
    }
    qlcg_source_rocks(reg,w);
    qlcg_region_build_colliders(reg, season_idx, settings->vegetation_form);
}

/* 采样生成 3D 山川活跃区域（含近景网格 */
RegionData *qlcg_chunk_sample(const MacroWorld *w, int center_cx, int center_cz,
                               int season_idx, const GameSettings *settings) {
    double t0 = qlcg_time_ms();
    RegionData *reg = (RegionData *)calloc(1, sizeof(RegionData));
    if (!reg) return NULL;
    reg->width = reg->height = QLCG_CHUNK_GRID;
    reg->step = (float)TERRAIN_GRID;

    reg->center_cx = center_cx;
    reg->center_cz = center_cz;
    reg->center_x  = ((float)center_cx + 0.5f) * (float)QLCG_CHUNK_SIZE_M;
    reg->center_z  = ((float)center_cz + 0.5f) * (float)QLCG_CHUNK_SIZE_M;

    float span = (float)(reg->width - 1) * reg->step;
    reg->origin_x = reg->center_x - span * 0.5f;
    reg->origin_z = reg->center_z - span * 0.5f;

    int count = reg->width * reg->height;
    reg->heights      = (float *)calloc((size_t)count, sizeof(float));
    reg->coast_material = (uint8_t *)calloc((size_t)count,3);
    reg->shore_depth = (float *)calloc((size_t)count, sizeof(float));
    reg->water_levels = (float *)calloc((size_t)count, sizeof(float));
    reg->ice_bottom   = (float *)calloc((size_t)count, sizeof(float));
    reg->ice_coverage = (float *)calloc((size_t)count, sizeof(float));
    reg->flow_x       = (float *)calloc((size_t)count, sizeof(float));
    reg->flow_z       = (float *)calloc((size_t)count, sizeof(float));
    reg->water_kind   = (uint8_t *)calloc((size_t)count, sizeof(uint8_t));
    reg->river_sediment=(uint8_t *)calloc((size_t)count, sizeof(uint8_t));
    reg->water_fresh=(uint8_t *)calloc((size_t)count, sizeof(uint8_t));
    reg->temperatures = (float *)calloc((size_t)count, sizeof(float));
    reg->wetness      = (float *)calloc((size_t)count, sizeof(float));
    reg->desert       = (float *)calloc((size_t)count, sizeof(float));
    reg->forest       = (float *)calloc((size_t)count, sizeof(float));
    reg->aridities    = (float *)calloc((size_t)count, sizeof(float));
    reg->uplifts      = (float *)calloc((size_t)count, sizeof(float));
    reg->colors       = (uint8_t *)calloc((size_t)count * 3, sizeof(uint8_t));

    reg->tree_cap = reg->rock_cap = 32;
    reg->trees = calloc((size_t)reg->tree_cap, sizeof(PropInstance));
    reg->rocks = calloc((size_t)reg->rock_cap, sizeof(PropInstance));
    if (!reg->heights || !reg->coast_material || !reg->shore_depth || !reg->water_levels || !reg->ice_bottom || !reg->ice_coverage ||
        !reg->flow_x || !reg->flow_z ||
        !reg->water_kind || !reg->river_sediment || !reg->water_fresh || !reg->temperatures || !reg->wetness ||
        !reg->desert || !reg->forest || !reg->aridities || !reg->uplifts ||
        !reg->colors || !reg->trees || !reg->rocks) {
        qlcg_region_free(reg);
        return NULL;
    }
    qlcg_chunk_resample(reg, w, season_idx, settings);

    reg->sample_ms = qlcg_time_ms() - t0;
    return reg;
}

/* 单块重新采样 */
void qlcg_chunk_resample(RegionData *reg, const MacroWorld *w,
                          int season_idx, const GameSettings *settings) {
    qlcg_ice_chunk_free(reg->ice);
    reg->ice = NULL;
    bool effects_allocation_failed=false;
    for (int z = 0; z < reg->height; z++) {
        float wz = reg->origin_z + (float)z * reg->step;
        for (int x = 0; x < reg->width; x++) {
            float wx = reg->origin_x + (float)x * reg->step;
            int idx = z * reg->width + x;
            SurfaceWater sw;
            IceSite site;
            qlcg_sample_surface_water(w, wx, wz, season_idx,
                                      &reg->heights[idx], &reg->water_levels[idx],
                                      &reg->temperatures[idx], &reg->wetness[idx],
                                      &reg->desert[idx], &reg->forest[idx],
                                      &reg->aridities[idx], &reg->uplifts[idx], &sw, &site);
            reg->water_levels[idx] = sw.level;
            reg->shore_depth[idx] = sw.shore_depth;
            memcpy(reg->coast_material+idx*3,sw.coast_material,3);
            reg->flow_x[idx]     = sw.raw_vx;
            reg->flow_z[idx]     = sw.raw_vz;
            reg->water_kind[idx] = sw.kind;
            reg->river_sediment[idx] = sw.river_sediment;
            reg->water_fresh[idx] = sw.fresh;
            if(!reg->source_effects&&!effects_allocation_failed&&
               (sw.source_effects.salt||sw.source_effects.spring)) {
                reg->source_effects=calloc((size_t)reg->width*reg->height,sizeof(*reg->source_effects));
                effects_allocation_failed=!reg->source_effects;
            }
            if(reg->source_effects) reg->source_effects[idx]=sw.source_effects;
            reg->ice_bottom[idx]   = sw.ice_bottom;
            reg->ice_coverage[idx] = sw.ice_coverage;
            /* 只有真水才分配冰缓存，干块不做额外内存 */
            if (sw.kind != WATER_KIND_DRY && sw.depth > .02f) {
                if (!reg->ice) reg->ice = qlcg_ice_chunk_create();
                if (reg->ice) {
                    qlcg_ice_cache_site(reg->ice, idx, &site);
                    reg->ice->h[idx] = sw.ice_thickness;
                    reg->ice->q[idx] = sw.ice_quality;
                }
            }
        }
    }
    if (reg->ice) qlcg_ice_chunk_attach(w, reg);

    ChunkInspectInfo ci;
    qlcg_world_inspect_chunk(w, reg->center_cx, reg->center_cz, &ci);
    reg->site_cell       = ci.cell_id;
    reg->climate_season  = qlcg_clampi(season_idx, 0, 3);
    reg->site_chunk_type = ci.type_id;
    reg->site_height     = ci.elevation;

    /* 水体四态按区块中心点级采样，冻态仅作兼容统计 */
    {
        SurfaceWater cw;
        qlcg_sample_water(w, reg->center_x, reg->center_z, season_idx, &cw, NULL);
        reg->water_form   = (WaterFormState)cw.form;
        reg->water_freeze = (WaterFreezeState)cw.freeze;
    }

    qlcg_region_recolor(reg, w, season_idx);
    qlcg_region_scatter_props(reg, w, season_idx, settings);

}
