#include "hydrology_oxbow.h"
/* planet_fields */
#include "planet_macro.h"
#include "world_internal.h"
#include "world_surface_dirty.h"
#include "hydrology_lakes.h"
#include "hydrology_river.h"
#include "faction_naming.h"
#include "gen/gen_palette.h"
#include "gen/hydrology_dimensions.h"
#include "../../core/math_util.h"
#include "platform/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct { int cell; uint8_t level; } RiverCandidate;
struct MacroRiverCandidates {
    uint8_t *levels;
    RiverCandidate *candidates;
    int count;
};

void qlcg_world_atlas_rivers_free(MacroWorld *w) {
    if (!w->atlas_rivers) return;
    free(w->atlas_rivers->levels);
    free(w->atlas_rivers->candidates);
    free(w->atlas_rivers);
    w->atlas_rivers = NULL;
}

static int rect_columns(const MacroWorld *w, int x0, int x1, int z0, int z1,
                        MacroCellRect rects[2]) {
    if (z0 >= z1 || x0 >= x1) return 0;
    if (qlcg_world_flat(w)) {
        x0=qlcg_clampi(x0,0,w->columns); x1=qlcg_clampi(x1,0,w->columns);
        if (x0 >= x1) return 0;
    } else {
        int span=x1-x0;
        x0=span>=w->columns?0:wrap_col(x0,w->columns);
        x1=x0+(span<w->columns?span:w->columns);
        if (x1>w->columns) {
            rects[0]=(MacroCellRect){0,z0,x1-w->columns,z1};
            rects[1]=(MacroCellRect){x0,z0,w->columns,z1};
            return 2;
        }
    }
    rects[0]=(MacroCellRect){x0,z0,x1,z1};
    return 1;
}

int qlcg_macro_rects(const MacroWorld *w, double x0, double z0, double x1, double z1,
                     MacroCellRect rects[2]) {
    double C=w->circumference, depth=qlcg_world_depth(w);
    if (C<=0 || z0>depth*.5 || z1< -depth*.5 || x1<x0 || z1<z0) return 0;
    double scale=w->columns/C;
    int iz0=(int)floor(fmax(0,(z0+depth*.5)*scale));
    int iz1=(int)ceil(fmin(w->rows,(z1+depth*.5)*scale))+1;
    if (iz1>w->rows) iz1=w->rows;
    if (!qlcg_world_flat(w)) {
        if (x1-x0>=C) return rect_columns(w,0,w->columns,iz0,iz1,rects);
        double start=fmod(x0+C*.5,C);
        if (start<0) start+=C;
        x1=start+(x1-x0); x0=start;
    } else {
        if (x0>C*.5 || x1< -C*.5) return 0;
        x0=fmax(0,x0+C*.5); x1=fmin(C,x1+C*.5);
    }
    return rect_columns(w,(int)floor(x0*scale),(int)ceil(x1*scale)+1,iz0,iz1,rects);
}

void qlcg_world_macro_surface_dirty(MacroWorld *w, uint32_t before_delta, uint32_t before_surface,
                                     double x0, double z0, double x1, double z1) {
    if (!qlcg_world_flat(w) && before_surface!=w->surface_revision) {
        double C=w->circumference,near_pole=C*.25-C/w->columns;
        if (z0<= -near_pole || z1>=near_pole) { x0=-C*.5; x1=C*.5; }
    }
    qlcg_world_surface_dirty(w,before_delta,before_surface,x0,z0,x1,z1,true);
}

/* 地表主河宽度含下游延续段 */
static float atlas_river_width(const MacroWorld *w, int i) {
    float half = w->river_width[i];
    if (w->main_river_width) half = fmaxf(half, w->main_river_width[i]);
    return half * 2;
}

/* 湖内隐去，保留溢出边 */
static void push_river_seg(MacroWorld *w, int i, uint8_t lvl) {
    if (w->river_count >= w->river_cap || w->river_count >= MAX_RIVERS) return;
    if (!(atlas_river_width(w, i) > 0.0f)) return;
    bool wet = (w->water[i] == 1) || (w->water[i] == 2);
    if (wet) {
        int lake=w->water[i]==2 && w->lake_id?w->lake_id[i]:-1;
        if (lake<0 || lake>=w->lake_count || w->lakes[lake].exit_cell!=i ||
            w->lakes[lake].outlet!=w->receiver[i] || !(w->lakes[lake].outflow>0)) return;
    }
    int rec = w->receiver[i];
    if (rec < 0 || rec == i) return;
    float ax, az, bx, bz;
    qlcg_world_cell_center(w, i, &ax, &az);
    qlcg_world_cell_center(w, rec, &bx, &bz);
    RiverSegment *seg = &w->rivers[w->river_count++];
    seg->ax = ax; seg->az = az;
    seg->bx = bx; seg->bz = bz;
    seg->mx = 0.5f * (ax + bx);
    seg->mz = 0.5f * (az + bz);
    seg->width_m = atlas_river_width(w, i);
    seg->flow = (float)w->flow[i];
    seg->min_level = lvl;
}

/* 河段分级：按流量比取 4~8 级 */
static void build_river_segments(MacroWorld *w, bool rebuild) {
    int N = w->cell_count;
    if (N <= 0 || !w->rivers) return;
    w->river_count=0;
    w->atlas_authored_river_count=0;
    struct MacroRiverCandidates *cache=w->atlas_rivers;
    if (!cache) {
        cache=calloc(1,sizeof(*cache));
        if (cache) cache->levels=malloc((size_t)N);
        w->atlas_rivers=cache;
        rebuild=true;
    }
    if (!cache || !cache->levels) {
        qlcg_world_atlas_rivers_free(w);
        for (int i = 0; i < N && w->river_count < w->river_cap && w->river_count < MAX_RIVERS; i++)
            push_river_seg(w, i, 8);
        return;
    }
    uint8_t *lv=cache->levels;
    if (rebuild) {
        double units = (double)w->circumference / (double)w->columns / 64.0;
        double base = qlcg_macro_river_threshold(units, w->opt.precipitation);
        double inv = 1.0 / (base > 1e-9 ? base : 1e-9);
        int count=0;
        memset(lv,0,(size_t)N);
        for (int i = 0; i < N; i++) {
            if (!(atlas_river_width(w, i) > 0.0f)) continue;
            double q = w->flow[i] * inv;
            lv[i] = (uint8_t)(q >= 16.0 ? 4 : q >= 8.0 ? 5
                            : q >= 4.0 ? 6 : q >= 2.0 ? 7 : 8);
            count++;
        }
        /* 下游继承更粗级，防主干断线 */
        for (int i = 0; i < N; i++) {
            if (!lv[i]) continue;
            int lv0 = lv[i], cell = w->receiver[i], guard = 0;
            while (cell >= 0 && cell < N && lv[cell] > lv0 && guard++ < 4096) {
                lv[cell] = (uint8_t)lv0;
                cell = w->receiver[cell];
            }
        }
        RiverCandidate *candidates=count?realloc(cache->candidates,(size_t)count*sizeof(*candidates)):NULL;
        if (!candidates) free(cache->candidates);
        cache->candidates=candidates;
        cache->count=count && !candidates ? -1 : 0;
        if (candidates) {
            for (int i=0;i<N;i++) if (lv[i] && lv[i]<=6) candidates[cache->count++]=(RiverCandidate){i,lv[i]};
            for (int i=0;i<N;i++) if (lv[i]>6) candidates[cache->count++]=(RiverCandidate){i,lv[i]};
        }
    }
    int limit=w->river_cap<MAX_RIVERS?w->river_cap:MAX_RIVERS;
    if (cache->candidates) {
        for (int i=0;i<cache->count && w->river_count<limit;i++)
            push_river_seg(w,cache->candidates[i].cell,cache->candidates[i].level);
    } else if (cache->count<0) {
        for (int i=0;i<N && w->river_count<limit;i++) if (lv[i] && lv[i]<=6) push_river_seg(w,i,lv[i]);
        for (int i=0;i<N && w->river_count<limit;i++) if (lv[i]>6) push_river_seg(w,i,lv[i]);
    }
}

static void rebuild_cells(MacroWorld *w, MacroCellRect rect) {
    int cols = w->columns, rows = w->rows;
    float cell_w = w->circumference / (float)cols;

    for (int y = rect.z0; y < rect.z1; ++y) {
        int yu=qlcg_clampi(y-1,0,rows-1),yd=qlcg_clampi(y+1,0,rows-1);
        const float *heights[3]={w->height+yu*cols,w->height+y*cols,w->height+yd*cols};
        for (int x = rect.x0; x < rect.x1; ++x) {
            int i = y * cols + x;
            if(w->islands_ready&&!w->decor_ready) qlcg_oxbow_generate(w,i);
            float h  = w->height[i];
            float wl = w->level[i];
            float t  = w->temperature[i];
            /* water: 1海/2湖/3河；河非湖海 */
            LakeShoreField lake=qlcg_lake_field_cell(w,i);
            if (lake.lake) h=qlcg_lake_height(&lake,h);
            if (lake.lake || w->water[i]==2)
                wl=lake.lake && lake.d<0 && lake.level>h+.02f ? lake.level : -1e9f;
            bool is_lake = lake.lake && wl>h+.02f;
            bool is_river_cell = (w->water[i] == 3);
            bool is_wat = (w->water[i] == 1) || is_lake || (wl > h + 0.5f) || (h <= 0.0f);
            bool is_riv = is_river_cell || (!is_wat && w->river_width[i] >= 7.0f);

            int xs[3]={x?x-1:qlcg_world_flat(w)?0:cols-1,x,
                       x+1<cols?x+1:qlcg_world_flat(w)?cols-1:0};
            float dx = (heights[1][xs[2]] - heights[1][xs[0]]) / (2.0f * cell_w);
            float dz = (heights[2][x] - heights[0][x]) / (2.0f * cell_w);
            float slope = hypotf(dx, dz);

            /* 九点极差与相对高度，按 32m 区块尺度折算 */
            float hmin = 1e9f, hmax = -1e9f, hsum = 0.0f;
            for (int oz=0;oz<3;oz++) for(int ox=0;ox<3;ox++) {
                float hv=heights[oz][xs[ox]];
                if(hv<hmin) hmin=hv;
                if(hv>hmax) hmax=hv;
                hsum+=hv;
            }
            float k32 = 32.0f / cell_w;
            float relief   = (hmax - hmin) * k32;
            float relative = (h - (hsum - h) * 0.125f) * k32;

            /* 分类阈值按干燥度，与降水的 mm 量纲解耦 */
            float aridity = w->rainfall[i] / fmaxf(150.0f, w->evaporation[i]);
            int claim = w->claim_type[i];
            int owner = w->claim_faction[i];
            bool needs_name = claim == 2 && owner >= 0 && owner < w->faction_count &&
                              !w->factions[owner].auto_name[0];
            int type = qlcg_classify_chunk(
                is_wat, is_riv, false, w->wetland_flag[i] != 0,
                w->saline[i] != 0, h, t, aridity, relief, slope, relative, w->chunk_claims||needs_name ? 0 : claim
            );
            w->chunk_type[i] = (int16_t)(needs_name&&!w->chunk_claims ? 9001 : type);
            if (needs_name) qlcg_faction_generate_name(&w->factions[owner], type, &w->opt);

            float sh = qlcg_clampf(0.76f + (-dx * 16.0f - dz * 12.0f), 0.46f, 1.24f);
            w->shade[i] = is_wat ? 1.0f : 0.88f + 0.12f * sh;

            /* 1. 高程色带：陆地对数高程+雪，水深 */
            {
                uint8_t col[3];
                if (is_wat) {
                    float full = (w->water[i] == 2) ? 60.0f : 1200.0f;
                    float lvl = (wl > h) ? wl : h;
                    qlcg_water_color(qlcg_depth_value(lvl, h, full), col);
                } else {
                    qlcg_land_color(qlcg_elevation_ramp(h), qlcg_snow_value(t), col);
                }
                /* 明暗统一在纹理上传时应用 */
                memcpy(w->base_rgb + i * 3, col, sizeof(col));
            }

            /* 2. 卫星图连续色（对齐卫星图） */
            {
                uint8_t sat[3];
                if (is_wat) {
                    float full = (w->water[i] == 2) ? 60.0f : 1200.0f;
                    float lvl = (wl > h) ? wl : h;
                    qlcg_water_color(qlcg_depth_value(lvl, h, full), sat);
                } else {
                    qlcg_satellite_color(t, w->rainfall[i], w->evaporation[i],
                                         relief, slope, relative, sat);
                }
                w->sat_rgb[i * 3 + 0] = sat[0];
                w->sat_rgb[i * 3 + 1] = sat[1];
                w->sat_rgb[i * 3 + 2] = sat[2];
            }
        }
    }
}

static MacroAtlasChange *begin_rebuild(MacroWorld *w) {
    w->surface_revision++;
    uint32_t rev=++w->atlas_revision;
    MacroAtlasChange *change=&w->atlas_changes[rev%QLCG_ATLAS_CHANGE_HISTORY];
    *change=(MacroAtlasChange){.revision=rev,.count=-1};
    return change;
}

void qlcg_world_rebuild_atlas_fields(MacroWorld *w) {
    double t0=qlcg_now_ms();
    begin_rebuild(w);
    rebuild_cells(w,(MacroCellRect){0,0,w->columns,w->rows});
    build_river_segments(w,true);
    qlcg_eng_atlas_rivers(w);
    w->prep_ms = qlcg_now_ms() - t0;
}

void qlcg_world_rebuild_atlas_region(MacroWorld *w, const MacroCellRect *rects, int count,
                                    MacroRiverUpdate rivers) {
    double t0=qlcg_now_ms();
    MacroAtlasChange *change=begin_rebuild(w);
    change->count=0;
    for (int i=0;i<count;i++) {
        const MacroCellRect *r=rects+i;
        MacroCellRect halo[2];
        int n=rect_columns(w,r->x0-1,r->x1+1,qlcg_clampi(r->z0-1,0,w->rows),
                           qlcg_clampi(r->z1+1,0,w->rows),halo);
        for (int k=0;k<n;k++) {
            rebuild_cells(w,halo[k]);
            change->rects[change->count++]=halo[k];
        }
    }
    if (rivers!=MACRO_RIVERS_UNCHANGED) {
        build_river_segments(w,rivers==MACRO_RIVERS_FLOW);
        qlcg_eng_atlas_rivers(w);
    }
    w->prep_ms=qlcg_now_ms()-t0;
}

int qlcg_world_atlas_changes(const MacroWorld *w, uint32_t since,
                            MacroCellRect *out, int capacity) {
    uint32_t n=w->atlas_revision-since;
    if (n>QLCG_ATLAS_CHANGE_HISTORY) return -1;
    int copied=0;
    for (uint32_t i=1;i<=n;i++) {
        uint32_t rev=since+i;
        const MacroAtlasChange *change=&w->atlas_changes[rev%QLCG_ATLAS_CHANGE_HISTORY];
        if (change->revision!=rev || change->count<0 || change->count>capacity-copied) return -1;
        if (change->count) memcpy(out+copied,change->rects,(size_t)change->count*sizeof(*out));
        copied+=change->count;
    }
    return copied;
}

/* 生成完整宏观星球与人工文明骨架 */
