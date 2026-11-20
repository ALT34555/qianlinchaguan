#include "world_lot.h"
/* 区域着色与植被散布 */
#include "planet_macro.h"
#include "world_settlement.h"
#include "world_civ_economy.h"
#include "world_farm_surface.h"
#include "world_delta.h"
#include "world_prop_edit.h"
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
#include "model/shore_models.h"
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
static void recolor_row(RegionData *reg,const MacroWorld *w,int season_idx,int row,const QlcgFarmSurface *farm) {
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
            if(farm->type) {
                uint8_t cultivated[4];
                qlcg_farm_surface_sample(farm,x*reg->step-half,z*reg->step-half,h,reg->water_form==WATER_FORM_CUTOFF?-1e9f:wl,cultivated);
                qlcg_farm_surface_color(cultivated,col);
            }
            reg->colors[idx * 3 + 0] = col[0];
            reg->colors[idx * 3 + 1] = col[1];
            reg->colors[idx * 3 + 2] = col[2];
        }
    }
}
void qlcg_region_recolor(RegionData *reg,const MacroWorld *w,int season_idx) {
    if(reg->stream){RegionStream *st=reg->stream;for(int i=0;i<st->count;i++)qlcg_region_recolor(st->slots[st->active[i]].data,w,season_idx);st->revision++;return;}
    QlcgFarmSurface farm;qlcg_farm_surface_at(&farm,w,reg->center_cx,reg->center_cz);
    for(int row=0;row<reg->height;row++)recolor_row(reg,w,season_idx,row,&farm);
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
    p->edit_x = reg->center_x + lx;
    p->edit_z = reg->center_z + lz;
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
    qlcg_region_add_rock(reg, rock, x, z, ground, seed, season);
}

static PropInstance *scatter_coral(RegionData *reg, int idx, float x, float z, float slope, uint32_t seed) {
    if (reg->rock_count >= reg->rock_cap || !reg->water_kind || slope > 1.06f) return NULL;
    int kind = reg->water_kind[idx];
    bool beach = kind == WATER_KIND_DRY && reg->coast_material && reg->shore_depth &&
                 reg->coast_material[3*idx+1] >= 46 && reg->shore_depth[idx] >= -2.6f;
    if (!beach && kind != WATER_KIND_SEA) return NULL;
    uint32_t roll = qlcg_hash_u32(seed ^ 0x636f7261u);
    if ((roll & 255u) >= (beach ? 33u : 83u)) return NULL;
    float jitter = reg->step * .70f;
    x += (((roll >> 8) & 255u) / 255.0f - .5f) * jitter;
    z += (((roll >> 16) & 255u) / 255.0f - .5f) * jitter;
    float bed = qlcg_region_height_at(reg, x, z);
    float depth = qlcg_region_water_at(reg, x, z) - bed;
    if (!isfinite(bed) || !isfinite(depth) || (beach ? depth > .04f : depth < .55f || depth > 14.0f)) return NULL;
    uint32_t shape = qlcg_hash_u32(roll ^ 0x51e8c247u);
    int size = CORAL_SMALL;
    if (depth > 4.5f && (shape & 7u) < 2u) size = CORAL_LARGE;
    else if ((depth > 1.75f || beach) && (shape & 7u) < (beach ? 2u : 5u)) size = CORAL_MEDIUM;
    int model = (CORAL_LARGE - size)*8 + (int)((shape >> 3) % 8u);
    float height = qlcg_coral_height(model);
    if (height <= 0) return NULL;
    float scale = .86f + ((shape >> 15) & 255u) * (.27f / 255.0f);
    if (beach && size == CORAL_MEDIUM) scale *= .78f;
    if (!beach) scale = fminf(scale, (depth - .12f) / height);
    PropInstance *p = &reg->rocks[reg->rock_count++];
    memset(p, 0, sizeof(*p));
    p->x = x; p->y = bed - .018f; p->z = z;
    p->archetype = CORAL_ARCH_BASE + model;
    p->scale_y = height * scale;
    p->rot_y = (roll >> 20) * (6.2831853f / 4096.0f);
    p->visual_season = beach ? CORAL_WITHERED : (uint8_t)((shape >> 9) % CORAL_PALETTE_COUNT);
    p->shape_variant = (uint8_t)((shape >> 25) % 3u);
    p->collider.kind = COLLISION_NONE;
    snprintf(p->label, sizeof(p->label), "%s", qlcg_coral_model(model)->name);
    return p;
}

PropInstance *qlcg_region_add_rock(RegionData *reg, int rock, float x, float z,
                                   float ground, uint32_t seed, int season) {
    if (reg->rock_count >= reg->rock_cap) return NULL;
    const RockGeometry *g = qlcg_rock_geometry(rock, ROCK_FORM_OUTCROP, season, 1);
    uint32_t shape = qlcg_hash_u32(seed ^ 0xc2b2ae35u);
    float u = (shape & 0xffffu) / 65536.0f, v = (shape >> 16) / 65536.0f;
    PropInstance *rk = &reg->rocks[reg->rock_count++];
    memset(rk, 0, sizeof(*rk));
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
    return rk;
}

/* 旗帜直接读取本块领地 */
static void scatter_flag(RegionData *reg,const MacroWorld *w) {
    const TerritoryFaction *territory=qlcg_settlement_chunk(w,reg->center_cx,reg->center_cz);
    if(!territory||reg->flag_count>=32) return;
    PropInstance *fl=&reg->flags[reg->flag_count++];
    fl->x=fl->z=0;
    QlcgSettlementPart parts[QLCG_SETTLEMENT_PARTS];
    if(qlcg_settlement_layout(territory,parts)>0 && parts[0].scale>1) {
        float yaw=qlcg_settlement_yaw(territory);
        fl->x=QLCG_SETTLEMENT_PLAZA_X*cosf(yaw);
        fl->z=QLCG_SETTLEMENT_PLAZA_X*sinf(yaw);
    }
    const QlcgLot *lot=qlcg_lot_at(w,reg->center_cx,reg->center_cz);
    if(lot && lot->flag_moved) {fl->x=lot->flag_x;fl->z=lot->flag_z;}
    fl->y=qlcg_region_height_at(reg,fl->x,fl->z);
    fl->scale_x=fl->scale_y=fl->scale_z=1;
    fl->r=(float)((territory->main_color>>16)&0xFF)/255;
    fl->g=(float)((territory->main_color>>8)&0xFF)/255;
    fl->b=(float)(territory->main_color&0xFF)/255;
    fl->trim_color=territory->trim_color;
    fl->faction_id=(int16_t)territory->id;
    snprintf(fl->label,sizeof(fl->label),"%s · %s",qlcg_faction_display_name(territory),
             qlcg_settlement_claim_type(w,reg->center_cx,reg->center_cz)==2?"置旗地主旗":"有主界标");
}

/* 散布植被、岩石与旗帜 */
typedef struct {
    QlcgClimateSeason cs;
    const TerritoryFaction *settlement;
    const QlcgLot *lot;
    QlcgFarmSurface farm;
    QlcgGuildShape guild_shapes[QLCG_GUILD_BOX_MAX];
    int guild_shape_n,zone_cached,scatter_pool[96],scatter_pool_n;
    int passes,stride,rock_material,row;
    float half;
    VegetationDensity density;
} ScatterJob;
static bool farm_cleared(const ScatterJob *job,float x,float z,float padding) {
    return job->farm.type&&(qlcg_farm_surface_inside(&job->farm,x,z,padding)||fabsf(x)<1+padding||fabsf(z)<1+padding);
}
static bool structure_cleared(const ScatterJob *job,float x,float z,float pad) {
    return job->lot?qlcg_lot_pick(job->lot,x,z,pad)>=0:qlcg_settlement_cleared(job->settlement,x,z,pad);
}
static bool plant_cleared(const ScatterJob *job,float x,float z) {
    return farm_cleared(job,x,z,1)||structure_cleared(job,x,z,1.5f);
}
static void clear_guild_plants(const ScatterJob *job,RegionData *reg) {
    if(!job->farm.type&&!job->settlement&&!job->lot)return;
    int count=0;
    for(int i=0;i<reg->tree_count;i++)if(!plant_cleared(job,reg->trees[i].x,reg->trees[i].z))reg->trees[count++]=reg->trees[i];
    reg->tree_count=count;
}
static void clear_guild_props(const ScatterJob *job,RegionData *reg) {
    if(!job->farm.type&&!job->settlement&&!job->lot)return;
    clear_guild_plants(job,reg);
    int count=0;
    for(int i=0;i<reg->rock_count;i++) {
        PropInstance *r=reg->rocks+i;
        if(!farm_cleared(job,r->x,r->z,2)&&!structure_cleared(job,r->x,r->z,1.5f))
            reg->rocks[count++]=*r;
    }
    reg->rock_count=count;
    count=0;
    for(int i=0;i<reg->grass_count;i++) {
        GrassTuft *p=reg->grass+i;
        if(!plant_cleared(job,p->x,p->z)) reg->grass[count++]=*p;
    }
    reg->grass_count=count;
}
static void refresh_site_type(RegionData *reg,const MacroWorld *w,const QlcgFarmSurface *farm) {
    int type=farm && farm->type?farm->type->chunk_type:0;
    int middle=(reg->height/2)*reg->width+reg->width/2;
    if (reg->water_form!=WATER_FORM_CUTOFF && reg->heights && reg->water_levels &&
        reg->water_levels[middle]>reg->heights[middle]+.02f) type=0;
    reg->site_chunk_type=qlcg_civ_chunk_type(w,reg->center_cx,reg->center_cz,reg->site_natural_chunk_type,type);
}
static uint64_t appearance_hash(uint64_t key,const void *data,size_t size) {
    const uint8_t *bytes=data;
    for(size_t i=0;i<size;i++) key=(key^bytes[i])*1099511628211ull;
    return key;
}
uint64_t qlcg_chunk_territory_appearance(RegionData *reg,const MacroWorld *w) {
    uint64_t key=14695981039346656037ull;
    QlcgFarmSurface farm;
    if(qlcg_farm_surface_at(&farm,w,reg->center_cx,reg->center_cz)) {
        float shape[]={farm.plot.x,farm.plot.z,farm.plot.width,farm.plot.depth,farm.plot.yaw};
        key=appearance_hash(key,shape,sizeof(shape));
        key=appearance_hash(key,&farm.type,sizeof(farm.type));
    }
    const TerritoryFaction *flag=qlcg_settlement_chunk(w,reg->center_cx,reg->center_cz);
    if(flag) {
        const char *name=qlcg_faction_display_name(flag);
        key=appearance_hash(key,name,strlen(name)+1);
        key=appearance_hash(key,flag->uid,sizeof(flag->uid));
        key=appearance_hash(key,&flag->id,sizeof(flag->id));
        key=appearance_hash(key,&flag->main_color,sizeof(flag->main_color));
        key=appearance_hash(key,&flag->trim_color,sizeof(flag->trim_color));
    }
    const QlcgLot *lot=qlcg_lot_at(w,reg->center_cx,reg->center_cz);
    if(lot) key=appearance_hash(key,&lot->revision,sizeof(lot->revision));
    refresh_site_type(reg,w,&farm);
    return key;
}
static void scatter_begin(ScatterJob *job,RegionData *reg,const MacroWorld *w,int season_idx,const GameSettings *settings,bool markers,const QlcgFarmSurface *prepared_farm) {
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
    reg->grass_count = 0;
    reg->flag_count = 0;
    job->lot=qlcg_lot_at(w,reg->center_cx,reg->center_cz);
    job->settlement=qlcg_settlement_chunk(w,reg->center_cx,reg->center_cz);
    if(prepared_farm)job->farm=*prepared_farm;
    else qlcg_farm_surface_at(&job->farm,w,reg->center_cx,reg->center_cz);
    refresh_site_type(reg,w,&job->farm);

    int guild_shape_n=qlcg_guild_scatter(reg,w,season_idx,settings,markers?NULL:&cs,
        guild_shapes,QLCG_GUILD_BOX_MAX);
    clear_guild_props(job,reg);
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

static PropInstance *water_guild_plant(RegionData *reg, const MacroWorld *w, const ScatterJob *job,
                                      const GameSettings *settings, float x, float z, int kind,
                                      uint32_t seed) {
    float bed, depth;
    int index = qlcg_aquatic_guild_site(reg, x, z, kind, &bed, &depth);
    if (index < 0 || (reg->ice_coverage && reg->ice_coverage[index] >= 0.5f) ||
        plant_cleared(job, x, z)) return NULL;
    float wx = reg->center_x + x, wz = reg->center_z + z;
    int cell = qlcg_world_cell_at_wxz(w, wx, wz);
    PlantSite site;
    fill_site(&site, reg, w, index, cell, 0, kind);
    site.water_depth = depth;
    int species = qlcg_plant_pick_shore(&site, seed);
    if (species < 0 && kind != WATER_KIND_SEA) {
        int pool[96];
        int n = qlcg_pg_zone_pool(qlcg_climate_zone_cell(w, cell), pool, 96);
        species = qlcg_plant_pick_from(&site, seed, pool, n);
    }
    const PdSpecies *sp = qlcg_pd_species(species);
    if (!sp || (w->chunk_deltas && qlcg_chd_prop_state(w, wx, wz,
        CHD_PROP_PLANT_BASE + species) == CHD_PROP_REMOVED)) return NULL;
    int season = qlcg_plant_visual_season(&job->cs, site.temperature, seed);
    unsigned variant = settings->vegetation_form == VEG_FORM_RICH &&
        qlcg_plant_category_has_variants(PLANT_CAT_SHUI) ? (seed >> 22) % 3u : 0;
    int form = qlcg_plant_model_form(settings->vegetation_form, variant);
    const LpGeometry *geo = qlcg_plant_geometry(species, form, season);
    if (!geo || geo->height <= 0.01) return NULL;
    bool floating = qlcg_pd_has_tag(sp, "fushui");
    float scale = 0.85f + ((seed >> 16) & 255u) * (0.30f / 255.0f);
    if (qlcg_shore_plant_index(sp->base.id) >= 0)
        scale = fminf(scale, (depth - 0.08f) / (float)geo->height);
    float y = floating ? bed + depth - 0.01f : bed - 0.06f;
    return qlcg_region_add_plant(reg, species, PLANT_CAT_SHUI, 1, x, z, y,
        (seed & 65535u) * (2.0f * QLCG_PI / 65536.0f),
        (float)(2 * geo->radius) * scale, (float)geo->height * scale, season, variant, seed);
}

static void scatter_water_guild(RegionData *reg, const MacroWorld *w, const ScatterJob *job,
                                const GameSettings *settings, float x, float z, int kind,
                                const PropInstance *coral, float patch, uint32_t seed) {
    if (kind == WATER_KIND_DRY || kind == WATER_KIND_FALL) return;
    uint32_t roll = qlcg_hash_u32(seed ^ 0x77617472u);
    float clearance;
    if (coral) {
        x = coral->x;
        z = coral->z;
        clearance = qlcg_clampf(coral->scale_y * 0.65f, 0.50f, 1.22f);
        float bed, depth;
        if (qlcg_aquatic_guild_site(reg, x, z, kind, &bed, &depth) < 0) return;
    } else {
        float chance = kind == WATER_KIND_SEA ? 0.18f : 0.30f;
        if ((roll & 65535u) / 65536.0f >= chance * patch) return;
        x += (((roll >> 16) & 255u) / 255.0f - 0.5f) * 1.4f;
        z += ((roll >> 24) / 255.0f - 0.5f) * 1.4f;
        if (!water_guild_plant(reg, w, job, settings, x, z, kind,
                              qlcg_hash_u32(roll ^ 0x706c616eu))) return;
        clearance = kind == WATER_KIND_SEA ? 0.44f : 0.34f;
    }
    int zone = qlcg_climate_zone_at(w, reg->center_x + x, reg->center_z + z);
    if (qlcg_aquatic_guild_grass(reg, x, z, kind, zone, clearance, roll) < 12) return;
    uint32_t companion = qlcg_hash_u32(roll ^ 0x636f6d70u);
    float angle = (companion & 65535u) * (2.0f * QLCG_PI / 65536.0f);
    float offset = 1.15f + (companion >> 24) * (0.6f / 255.0f);
    water_guild_plant(reg, w, job, settings,
                      x + cosf(angle) * offset, z + sinf(angle) * offset, kind, companion);
}

static bool scatter_step(ScatterJob *job,RegionData *reg,const MacroWorld *w,int season_idx,const GameSettings *settings,bool markers) {
    QlcgClimateSeason cs=job->cs;
    QlcgGuildShape *guild_shapes=job->guild_shapes;
    int guild_shape_n=job->guild_shape_n,zone_cached=job->zone_cached;
    int *scatter_pool=job->scatter_pool,scatter_pool_n=job->scatter_pool_n;
    int passes=job->passes,stride=job->stride,rock_material=job->rock_material;
    float half=job->half;VegetationDensity density=job->density;const float base_density=1.35f;
    const TerritoryFaction *settlement=job->settlement;
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

            bool farm_rock=farm_cleared(job,lx,lz,2);
            if(!markers && !farm_rock && !structure_cleared(job,lx,lz,1.5f) &&
               !qlcg_headwater_excludes(w,reg->center_x+lx,reg->center_z+lz,4)) {
                scatter_rock(reg, idx, lx, lz, stride * reg->step, slope,
                             rock_material, patch.rocks, hsh, season_idx);
                const PropInstance *coral = scatter_coral(reg, idx, lx, lz, slope, hsh);
                int kind = reg->water_kind ? reg->water_kind[idx] : WATER_KIND_DRY;
                scatter_water_guild(reg, w, job, settings, lx, lz, kind, coral, patch.density, hsh);
            }

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
                int kind = reg->water_kind ? reg->water_kind[sample] : WATER_KIND_DRY;
                float depth_limit = kind == WATER_KIND_SEA ? 8.0f : 2.2f;
                float depth = water - bed;
                bool aquatic = water > bed + 0.15f;
                if (aquatic) {
                    if (depth >= depth_limit || u >= 0.08f * base_density * patch.density ||
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
                    if (aquatic ? !(depth > 0.15f && depth < depth_limit) : water > bed)
                        continue;
                }
                if(plant_cleared(job,px,pz)) continue;
                int pzone = qlcg_climate_zone_at(w, reg->center_x + px, reg->center_z + pz);
                if (pzone != zone_cached) {
                    zone_cached = pzone;
                    scatter_pool_n = (pzone >= 0 && pzone < QLCG_ZONE_COUNT)
                        ? qlcg_pg_zone_pool(pzone, scatter_pool, 96) : 0;
                }
                if (qlcg_guild_inside(guild_shapes, guild_shape_n,
                                      reg->center_x + px, reg->center_z + pz, 0.4f))
                    continue;
                float site_x = pass ? px : lx;
                float site_z = pass ? pz : lz;
                int cell = qlcg_world_cell_at_wxz(w, reg->center_x + site_x, reg->center_z + site_z);
                float site_slope = pass && !aquatic ? qlcg_region_slope_at(reg, px, pz) : slope;
                if(!aquatic && site_slope>1.85f && reg->coast_material &&
                   reg->coast_material[sample*3+2]>48) continue;
                PlantSite site;
                fill_site(&site, reg, w, sample, cell, site_slope, kind);
                site.water_depth = aquatic ? depth : 0.0f;
                int sid = aquatic && (kind == WATER_KIND_SEA || v < .37f)
                    ? qlcg_plant_pick_shore(&site, species_roll) : -1;
                if (sid < 0) sid = qlcg_plant_pick_from(&site, species_roll, scatter_pool, scatter_pool_n);
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
                if (aquatic && qlcg_shore_plant_index(species->base.id) >= 0)
                    scale = fminf(scale, (depth - .08f) / (float)geometry->height);
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
typedef struct {
    RegionData *reg;
    int season, form;
} PlantedSaplings;

static void scatter_sapling(float wx, float wz, int kind, int state, void *context) {
    PlantedSaplings *planted = context;
    RegionData *reg = planted->reg;
    int species = kind - CHD_PROP_PLANT_BASE;
    if(kind>=CHD_PROP_ROCK_BASE) {
        int rock=kind-CHD_PROP_ROCK_BASE;
        if(rock>=qlcg_rock_count() || reg->rock_count>=reg->rock_cap) return;
        float x=wx-reg->center_x,z=wz-reg->center_z;
        qlcg_region_add_rock(reg,rock,x,z,qlcg_region_height_at(reg,x,z),(uint32_t)rock+37,planted->season);
        return;
    }
    int category=qlcg_plant_species_category(species);
    if(category<0) return;
    float lx = wx - reg->center_x, lz = wz - reg->center_z;
    float y = qlcg_region_height_at(reg, lx, lz);
    if (!isfinite(y)) return;
    bool withered = state == CHD_PROP_PLANTED_WITHERED;
    int season = withered ? PD_SEASON_WINTER : planted->season;
    int form = qlcg_plant_model_form(planted->form, 0);
    const LpGeometry *g = qlcg_plant_geometry(species, form, season);
    if (!g || g->height <= 0) return;
    float height = withered ? .84f : 1.2f;
    float width = (float)(2 * g->radius / g->height) * height;
    PropInstance *p = qlcg_region_add_plant(reg, species, category, 0, lx, lz, y,
                                             0, width, height, season, 0, 0);
    if (p) {
        p->edit_x = wx;
        p->edit_z = wz;
        p->model_state = withered ? PLANT_STATE_PLANTED_WITHERED : PLANT_STATE_PLANTED;
    }
}

static void apply_prop_edits(RegionData *reg, const MacroWorld *w, int season_idx, const GameSettings *settings) {
    if (!w->chunk_deltas) return;
    int count = 0;
    for (int i = 0; i < reg->tree_count; ++i) {
        PropInstance plant = reg->trees[i];
        int state = qlcg_chd_prop_state(w, plant.edit_x, plant.edit_z,
                                        CHD_PROP_PLANT_BASE + plant.species);
        if (state == CHD_PROP_REMOVED) continue;
        if (state == CHD_PROP_WITHERED) {
            plant.model_state = PLANT_STATE_WITHERED;
            plant.visual_season = PD_SEASON_WINTER;
            plant.scale_x *= 0.7f;
            plant.scale_y *= 0.7f;
            plant.scale_z *= 0.7f;
        }
        reg->trees[count++] = plant;
    }
    reg->tree_count = count;
    count = 0;
    for (int i = 0; i < reg->rock_count; ++i) {
        PropInstance rock = reg->rocks[i];
        if (qlcg_coral_index(rock.archetype) >= 0 ||
            qlcg_chd_prop_state(w, reg->center_x + rock.x, reg->center_z + rock.z,
                                CHD_PROP_ROCK_BASE + rock.archetype) != CHD_PROP_REMOVED)
            reg->rocks[count++] = rock;
    }
    reg->rock_count = count;
    PlantedSaplings planted = {reg, season_idx, settings->vegetation_form};
    qlcg_chd_visit_planted(w, reg->center_cx, reg->center_cz, scatter_sapling, &planted);
}

static void scatter_end(RegionData *reg,const MacroWorld *w,int season_idx,const GameSettings *settings) {
    scatter_flag(reg,w);
    qlcg_source_rocks(reg,w);
    apply_prop_edits(reg, w, season_idx, settings);
    qlcg_region_build_colliders(reg, season_idx, settings->vegetation_form);
}
void qlcg_region_scatter_props(RegionData *reg,const MacroWorld *w,int season_idx,const GameSettings *settings) {
    if(reg->stream){RegionStream *st=reg->stream;for(int i=0;i<st->count;i++)qlcg_region_scatter_props(st->slots[st->active[i]].data,w,season_idx,settings);st->revision++;return;}
    ScatterJob job;scatter_begin(&job,reg,w,season_idx,settings,false,NULL);
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
    QlcgFarmSurface farm;
    float inspect_heights[9];
    SurfaceWater center_water;
    bool is_fall,effects_allocation_failed,markers,artificial_enabled;
    uint32_t delta,surface,territory;
};
RegionBuildJob *qlcg_chunk_build_begin(const MacroWorld *w,int cx,int cz,int season,const GameSettings *settings) {
    RegionBuildJob *job=calloc(1,sizeof(*job));if(!job)return NULL;
    job->data=chunk_allocate(w,cx,cz,season,settings);
    if(!job->data){free(job);return NULL;}
    job->world=w;job->settings=*settings;job->season=season;
    job->artificial_enabled=w->opt.artificial_enabled;
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
        j->artificial_enabled==w->opt.artificial_enabled&&
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
    reg->site_cell       = qlcg_world_cell_at_wxz(w,reg->center_x,reg->center_z);
    reg->climate_season  = qlcg_clampi(season_idx, 0, 3);
    reg->site_chunk_type = ci.type_id;
    reg->site_natural_chunk_type = ci.natural_type_id;
    reg->site_height     = ci.elevation;

    /* 水体兼容统计复用中心样本 */
    reg->water_form   = (WaterFormState)job->center_water.form;
    reg->water_freeze = (WaterFreezeState)job->center_water.freeze;


        qlcg_farm_surface_at(&job->farm,w,reg->center_cx,reg->center_cz);
        job->phase=job->markers?3:2;
    } else if(job->phase==2) {
        recolor_row(reg,w,season_idx,job->color_row,&job->farm);if(++job->color_row==reg->height)job->phase=3;
    } else if(job->phase==3) {
        scatter_begin(&job->scatter,reg,w,season_idx,settings,job->markers,&job->farm);job->phase=4;
    } else if(job->phase==4) {
        if(scatter_step(&job->scatter,reg,w,season_idx,settings,job->markers))job->phase=5;
    } else if(job->phase==5) {
        if(!job->markers) scatter_end(reg,w,season_idx,settings);job->phase=6;
    }
    return job->phase==6;
}
RegionData *qlcg_chunk_build_take(RegionBuildJob *job) { RegionData *data=job->data;job->data=NULL;free(job);return data; }
bool qlcg_chunk_build_needs_surface(const RegionBuildJob *job) { return job->phase==0; }
void qlcg_chunk_build_surface(RegionBuildJob *job,const MacroWorld *snapshot,double deadline) {
    const MacroWorld *world=job->world;
    job->world=snapshot;
    while(job->phase==0&&qlcg_time_ms()<deadline) qlcg_chunk_build_step(job);
    job->world=world;
}
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
