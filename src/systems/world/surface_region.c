/* 区域着色与植被散布 */
#include "planet_macro.h"
#include "surface_internal.h"
#include "surface_stream.h"
#include "hydrology_sources.h"
#include "surface_detail.h"
#include "climate_season.h"
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
#include "surface_guild.h"
#include "climate_zone.h"
#include "model/plant_guild.h"

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
static void recolor_row(RegionData *reg,const MacroWorld *w,int season_idx,int row) {
    if(row==0)reg->mesh_revision++;

    int gw = reg->width;
    int gh = reg->height;
    float span = (float)(reg->width - 1) * reg->step;
    float half = span * 0.5f;
    int season = (season_idx >= 0 && season_idx < 4) ? season_idx : -1;
    const HeadwaterPlan *head=qlcg_headwater_plan(w,reg->site_cell);
    SurfaceFrame frame=qlcg_surface_frame(w,reg->site_cell);
    if(head) {
        SurfacePoint center=qlcg_frame_local(&frame,reg->center_x,reg->center_z);
        if(hypot(center.x-head->x,center.z-head->z)>head->radius+span)head=NULL;
    }
    QlcgClimateSeason climate={0};
    if(head) qlcg_climate_season_at(w,reg->site_cell,w->plant_calendar,&climate);

    for (int z = row; z < row+1; z++) {
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
            if(head&&h>=wl-.02f) {
                SurfacePoint p=qlcg_frame_local(&frame,reg->center_x+x*reg->step-half,reg->center_z+z*reg->step-half);
                qlcg_headwater_color(head,p.x,p.z,climate.temperature,col);
            }
            reg->colors[idx * 3 + 0] = col[0];
            reg->colors[idx * 3 + 1] = col[1];
            reg->colors[idx * 3 + 2] = col[2];
        }
    }
}
void qlcg_region_recolor(RegionData *reg,const MacroWorld *w,int season_idx) {
    if(reg->stream){RegionStream *st=reg->stream;for(int i=0;i<st->count;i++)qlcg_region_recolor(st->slots[st->active[i]].data,w,season_idx);st->revision++;return;}
    for(int row=0;row<reg->height;row++)recolor_row(reg,w,season_idx,row);
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

/* 丛与散点共用的植株落地 */
PropInstance *qlcg_region_add_plant(RegionData *reg, int species, int category,
                                    int aquatic, float lx, float lz, float y,
                                    float rot_y, float size_xz, float size_y,
                                    int visual_season, int variant, uint32_t roll) {
    PropInstance *p = append_plant(reg);
    if (!p) return NULL;
    memset(p, 0, sizeof(*p));
    const PdSpecies *sp = qlcg_pd_species(species);
    p->x = lx;
    p->z = lz;
    p->y = y;
    p->rot_y = rot_y;
    p->scale_x = p->scale_z = size_xz;
    p->scale_y = size_y;
    p->r = 0.24f; p->g = 0.56f; p->b = 0.30f;
    p->archetype = (uint8_t)(aquatic ? roll % 3u : (roll >> 8) % 5u);
    p->model_state = PLANT_STATE_NORMAL;
    p->species = (uint8_t)(species < 0 ? 0xFF : species);
    p->category = (uint8_t)(category < 0 ? 0 : category);
    p->visual_season = (uint8_t)visual_season;
    p->shape_variant = (uint8_t)variant;
    if (sp && sp->base.name)
        snprintf(p->label, sizeof(p->label), "%s", sp->base.name);
    return p;
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

/* 旗帜直接读取本块领地 */
static void scatter_flag(RegionData *reg,const MacroWorld *w) {
    int cell=reg->site_cell,fid=w->claim_faction[cell];
    if(fid<0||fid>=w->faction_count||reg->flag_count>=32) return;
    const TerritoryFaction *territory=w->factions+fid;
    if(!territory->active||territory->cx!=reg->center_cx||territory->cz!=reg->center_cz) return;
    PropInstance *fl=&reg->flags[reg->flag_count++];
    fl->x=fl->z=0;
    fl->y=qlcg_region_height_at(reg,0,0);
    fl->scale_x=fl->scale_y=fl->scale_z=1;
    fl->r=(float)((territory->main_color>>16)&0xFF)/255;
    fl->g=(float)((territory->main_color>>8)&0xFF)/255;
    fl->b=(float)(territory->main_color&0xFF)/255;
    fl->trim_color=territory->trim_color;
    fl->faction_id=(int16_t)territory->id;
    snprintf(fl->label,sizeof(fl->label),"%s · %s",qlcg_faction_display_name(territory),
             w->claim_type[cell]==2?"置旗地主旗":"有主界标");
}

/* 散布植被、岩石与旗帜 */
typedef struct {
    QlcgClimateSeason cs;
    QlcgGuildShape guild_shapes[QLCG_GUILD_BOX_MAX];
    int guild_shape_n,zone_cached,scatter_pool[96],scatter_pool_n;
    int passes,stride,rock_material,row;
    float half;
    VegetationDensity density;
} ScatterJob;
static void scatter_begin(ScatterJob *job,RegionData *reg,const MacroWorld *w,int season_idx,const GameSettings *settings,bool markers) {
    QlcgClimateSeason cs;QlcgGuildShape guild_shapes[QLCG_GUILD_BOX_MAX];
    /* 散布前必须已有物种表，否则物种号全为未定 */
    qlcg_plant_material_init();
    qlcg_rock_material_init();
    qlcg_plant_habitat_prepare();

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

    int guild_shape_n = 0;
    if (markers) {
        int x0=(int)floorf(reg->origin_x/QLCG_GUILD_CELL);
        int z0=(int)floorf(reg->origin_z/QLCG_GUILD_CELL);
        float span=(reg->width-1)*reg->step;
        int x1=(int)floorf((reg->origin_x+span-.001f)/QLCG_GUILD_CELL);
        int z1=(int)floorf((reg->origin_z+span-.001f)/QLCG_GUILD_CELL);
        for(int z=z0;z<=z1;z++) for(int x=x0;x<=x1;x++) {
            QlcgGuildShape shape;
            if(qlcg_guild_query(w,x,z,&shape)>=0 && guild_shape_n<QLCG_GUILD_BOX_MAX)
                guild_shapes[guild_shape_n++]=shape;
        }
    } else guild_shape_n=qlcg_guild_scatter(reg,w,season_idx,settings,&cs,guild_shapes,QLCG_GUILD_BOX_MAX);
    int zone_cached = -2;
    int scatter_pool_n = 0;

    float span = (float)(reg->width - 1) * reg->step;
    float half = span * 0.5f;
    VegetationDensity density = qlcg_vegetation_density(settings->dense_vegetation);
    int passes = (int)ceilf(density.understory);
    /* 采样间距对齐 base 的 8m 区块网格 */
    int stride = qlcg_clampi((int)roundf(8.0f / reg->step), 1, 8);
    int rock_material = qlcg_chunk_material(reg->site_chunk_type);
    if (!rock_material)
        rock_material = site_material(w, reg->site_cell, WATER_KIND_DRY, 0);


    job->cs=cs;memcpy(job->guild_shapes,guild_shapes,sizeof(guild_shapes));job->guild_shape_n=guild_shape_n;
    job->zone_cached=zone_cached;job->scatter_pool_n=scatter_pool_n;
    job->passes=passes;job->stride=stride;job->rock_material=rock_material;job->half=half;job->density=density;job->row=3;
}
static bool scatter_step(ScatterJob *job,RegionData *reg,const MacroWorld *w,int season_idx,const GameSettings *settings,bool markers) {
    QlcgClimateSeason cs=job->cs;
    QlcgGuildShape *guild_shapes=job->guild_shapes;
    int guild_shape_n=job->guild_shape_n,zone_cached=job->zone_cached;
    int *scatter_pool=job->scatter_pool,scatter_pool_n=job->scatter_pool_n;
    int passes=job->passes,stride=job->stride,rock_material=job->rock_material;
    float half=job->half;VegetationDensity density=job->density;const float base_density=1.35f;
    for (int gz = job->row; gz < job->row+stride && gz < reg->height-1; gz += stride) {
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

            if(!markers && !qlcg_headwater_excludes(w,reg->center_x+lx,reg->center_z+lz,4))
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
                int pzone = qlcg_climate_zone_at(w, reg->center_x + px, reg->center_z + pz);
                if (pzone != zone_cached) {
                    zone_cached = pzone;
                    scatter_pool_n = (pzone >= 0 && pzone < QLCG_ZONE_COUNT)
                        ? qlcg_pg_zone_pool(pzone, scatter_pool, 96) : 0;
                }
                if (scatter_pool_n <= 0) continue;
                if (qlcg_guild_inside(guild_shapes, guild_shape_n,
                                      reg->center_x + px, reg->center_z + pz, 0.4f))
                    continue;
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
                int sid = qlcg_plant_pick_from(&site, species_roll,
                                               scatter_pool, scatter_pool_n);
                const PdSpecies *species = qlcg_pd_species(sid);
                if (!species) continue;
                int cat = qlcg_plant_species_category(sid);
                if(cat==PLANT_CAT_SHU&&qlcg_headwater_excludes(w,reg->center_x+px,reg->center_z+pz,4)) continue;
                if (pass && !aquatic && cat != PLANT_CAT_CAO && cat != PLANT_CAT_GUAN) {
                    if (cat != PLANT_CAT_SHU || pass_roll >= density.trees - pass) continue;
                }
                if (markers) {
                    if (cat==PLANT_CAT_SHU && !aquatic)
                        qlcg_region_add_plant(reg,sid,cat,0,px,pz,bed,0,1,1,0,0,roll);
                    continue;
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
                qlcg_region_add_plant(reg, sid, cat, aquatic ? 1 : 0, px, pz,
                    aquatic ? (floating ? water - 0.01f : bed - 0.06f) : bed,
                    (aquatic ? v : u) * 6.28318f,
                    (float)geometry->radius * 2.0f * scale,
                    (float)geometry->height * scale,
                    visual_season, (int)variant, roll);
            }
        }
    }


    job->zone_cached=zone_cached;job->scatter_pool_n=scatter_pool_n;job->row+=stride;
    return job->row>=reg->height-1;
}
static void scatter_end(RegionData *reg,const MacroWorld *w,int season_idx,const GameSettings *settings) {
    scatter_flag(reg,w);
    qlcg_source_rocks(reg,w);
    qlcg_region_build_colliders(reg, season_idx, settings->vegetation_form);
}
void qlcg_region_scatter_props(RegionData *reg,const MacroWorld *w,int season_idx,const GameSettings *settings) {
    if(reg->stream){RegionStream *st=reg->stream;for(int i=0;i<st->count;i++)qlcg_region_scatter_props(st->slots[st->active[i]].data,w,season_idx,settings);st->revision++;return;}
    ScatterJob job;scatter_begin(&job,reg,w,season_idx,settings,false);
    while(!scatter_step(&job,reg,w,season_idx,settings,false)){}
    scatter_end(reg,w,season_idx,settings);
}

/* 采样生成 3D 山川活跃区域（含近景网格 */
static RegionData *chunk_allocate(const MacroWorld *w, int center_cx, int center_cz,
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


    reg->sample_ms = qlcg_time_ms() - t0;
    return reg;
}

/* 单块重新采样 */
struct RegionBuildJob {
    RegionData *data;
    const MacroWorld *world;
    GameSettings settings;
    int season,row,phase,color_row;
    ScatterJob scatter;
    float inspect_heights[9];
    SurfaceWater center_water;
    bool is_fall,effects_allocation_failed,markers;
    uint32_t delta,surface,territory;
};
RegionBuildJob *qlcg_chunk_build_begin(const MacroWorld *w,int cx,int cz,int season,const GameSettings *settings) {
    RegionBuildJob *job=calloc(1,sizeof(*job));if(!job)return NULL;
    job->data=chunk_allocate(w,cx,cz,season,settings);
    if(!job->data){free(job);return NULL;}
    job->world=w;job->settings=*settings;job->season=season;
    job->delta=w->delta_version;job->surface=w->surface_revision;job->territory=w->territory_revision;
    return job;
}
RegionBuildJob *qlcg_chunk_plants_begin(const MacroWorld *w,int cx,int cz) {
    GameSettings settings={0};
    RegionBuildJob *job=qlcg_chunk_build_begin(w,cx,cz,1,&settings);
    if(job) job->markers=true;
    return job;
}
bool qlcg_chunk_build_valid(const RegionBuildJob *j,const MacroWorld *w,int season,const GameSettings *s) {
    return j&&j->world==w&&j->season==season&&j->delta==w->delta_version&&
        j->surface==w->surface_revision&&j->territory==w->territory_revision&&
        j->settings.dense_vegetation==s->dense_vegetation&&j->settings.vegetation_form==s->vegetation_form;
}
void qlcg_chunk_build_cancel(RegionBuildJob *job) { if(job){qlcg_region_free(job->data);free(job);} }
bool qlcg_chunk_build_step(RegionBuildJob *job) {
    RegionData *reg=job->data;const MacroWorld *w=job->world;
    int season_idx=job->season;const GameSettings *settings=&job->settings;
    if(job->phase==0) {
    for (int z = job->row; z < job->row+1; z++) {
        float wz = reg->origin_z + (float)z * reg->step;
        int iz=z==1?0:(z==reg->height/2?1:(z==reg->height-2?2:-1));
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
            int ix=x==1?0:(x==reg->width/2?1:(x==reg->width-2?2:-1));
            if(ix>=0&&iz>=0) {
                job->inspect_heights[iz*3+ix]=reg->heights[idx];
                job->is_fall=job->is_fall||sw.kind==WATER_KIND_FALL;
                if(ix==1&&iz==1) job->center_water=sw;
            }
            reg->water_levels[idx] = sw.level;
            reg->shore_depth[idx] = sw.shore_depth;
            memcpy(reg->coast_material+idx*3,sw.coast_material,3);
            reg->flow_x[idx]     = sw.raw_vx;
            reg->flow_z[idx]     = sw.raw_vz;
            reg->water_kind[idx] = sw.kind;
            reg->river_sediment[idx] = sw.river_sediment;
            reg->water_fresh[idx] = sw.fresh;
            if(!reg->source_effects&&!job->effects_allocation_failed&&
               (sw.source_effects.salt||sw.source_effects.spring||sw.source_effects.material||sw.source_effects.foam)) {
                reg->source_effects=calloc((size_t)reg->width*reg->height,sizeof(*reg->source_effects));
                job->effects_allocation_failed=!reg->source_effects;
            }
            if(reg->source_effects) reg->source_effects[idx]=sw.source_effects;
            reg->ice_bottom[idx]   = sw.ice_bottom;
            reg->ice_coverage[idx] = sw.ice_coverage;
            /* 只有真水才分配冰缓存，干块不做额外内存 */
            if (!job->markers && sw.kind != WATER_KIND_DRY && sw.depth > .02f) {
                if (!reg->ice) reg->ice = qlcg_ice_chunk_create();
                if (reg->ice) {
                    qlcg_ice_cache_site(reg->ice, idx, &site);
                    reg->ice->h[idx] = sw.ice_thickness;
                    reg->ice->q[idx] = sw.ice_quality;
                }
            }
        }
    }

        if(++job->row==reg->height)job->phase=1;
    } else if(job->phase==1) {
    if (reg->ice) qlcg_ice_chunk_attach(w, reg);

    ChunkInspectInfo ci;
    qlcg_world_inspect_sampled_chunk(w,reg->center_cx,reg->center_cz,job->inspect_heights,&job->center_water,job->is_fall,&ci);
    reg->site_cell       = ci.cell_id;
    reg->climate_season  = qlcg_clampi(season_idx, 0, 3);
    reg->site_chunk_type = ci.type_id;
    reg->site_height     = ci.elevation;

    /* 水体兼容统计复用中心样本 */
    reg->water_form   = (WaterFormState)job->center_water.form;
    reg->water_freeze = (WaterFreezeState)job->center_water.freeze;


        job->phase=job->markers?3:2;
    } else if(job->phase==2) {
        recolor_row(reg,w,season_idx,job->color_row);if(++job->color_row==reg->height)job->phase=3;
    } else if(job->phase==3) {
        scatter_begin(&job->scatter,reg,w,season_idx,settings,job->markers);job->phase=4;
    } else if(job->phase==4) {
        if(scatter_step(&job->scatter,reg,w,season_idx,settings,job->markers))job->phase=5;
    } else if(job->phase==5) {
        if(!job->markers) scatter_end(reg,w,season_idx,settings);job->phase=6;
    }
    return job->phase==6;
}
RegionData *qlcg_chunk_build_take(RegionBuildJob *job) { RegionData *data=job->data;job->data=NULL;free(job);return data; }
RegionData *qlcg_chunk_sample(const MacroWorld *w,int cx,int cz,int season,const GameSettings *settings) {
    double start=qlcg_time_ms();RegionBuildJob *job=qlcg_chunk_build_begin(w,cx,cz,season,settings);if(!job)return NULL;
    while(!qlcg_chunk_build_step(job)){}
    RegionData *data=qlcg_chunk_build_take(job);data->sample_ms=qlcg_time_ms()-start;return data;
}
void qlcg_chunk_resample(RegionData *reg,const MacroWorld *w,int season_idx,const GameSettings *settings) {
    qlcg_ice_chunk_free(reg->ice);reg->ice=NULL;
    RegionBuildJob job={0};job.data=reg;job.world=w;job.season=season_idx;job.settings=*settings;
    while(!qlcg_chunk_build_step(&job)){}
}
