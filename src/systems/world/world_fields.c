/* planet_fields */
#include "planet_macro.h"
#include "world_internal.h"
#include "surface_lakes.h"
#include "surface_rivers.h"
#include "faction_naming.h"
#include "gen/gen_palette.h"
#include "gen/gen_river.h"
#include "../../core/math_util.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* 地表主河宽度含下游延续段 */
static float atlas_river_width(const MacroWorld *w, int i) {
    float half = w->river_width[i];
    if (w->main_river_width) half = fmaxf(half, w->main_river_width[i]);
    return half * 2;
}

/* 湖内隐去，保留溢出边 */
static void push_river_seg(MacroWorld *w, int i, uint8_t lvl) {
    if (w->river_count >= w->river_cap) return;
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

/* 河段分级：按流量比取 4~8 级，对齐 base LOD */
static void build_river_segments(MacroWorld *w) {
    int N = w->cell_count;
    if (N <= 0 || !w->rivers) return;
    uint8_t *lv = (uint8_t *)calloc((size_t)N, 1);
    if (!lv) {
        for (int i = 0; i < N && w->river_count < w->river_cap; i++)
            push_river_seg(w, i, 8);
        return;
    }
    double units = (double)w->circumference / (double)w->columns / 64.0;
    double base = qlcg_macro_river_threshold(units, w->opt.precipitation);
    double inv = 1.0 / (base > 1e-9 ? base : 1e-9);
    for (int i = 0; i < N; i++) {
        if (!(atlas_river_width(w, i) > 0.0f)) continue;
        double q = w->flow[i] * inv;
        lv[i] = (uint8_t)(q >= 16.0 ? 4 : q >= 8.0 ? 5
                        : q >= 4.0 ? 6 : q >= 2.0 ? 7 : 8);
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
    /* 两遍提取：主干先占位，撞上限先丢支流 */
    for (int i = 0; i < N && w->river_count < w->river_cap; i++)
        if (lv[i] && lv[i] <= 6) push_river_seg(w, i, lv[i]);
    for (int i = 0; i < N && w->river_count < w->river_cap; i++)
        if (lv[i] && lv[i] > 6) push_river_seg(w, i, lv[i]);
    free(lv);
}

void qlcg_world_rebuild_atlas_fields(MacroWorld *w) {
    w->surface_revision++;
    double t0 = qlcg_now_ms();
    int cols = w->columns, rows = w->rows, N = w->cell_count;
    w->river_count = 0;
    float cell_w = w->circumference / (float)cols;

    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            int i = y * cols + x;
            if(w->islands_ready&&!w->decor_ready) qlcg_oxbow_generate(w,i);
            float h  = w->height[i];
            float wl = w->level[i];
            float t  = w->temperature[i];
            float m  = qlcg_clampf(w->moisture[i] - w->desert_factor[i] * 0.65f + w->forest_bonus[i] * 0.45f, 0.0f, 1.0f);
            /* water: 1 海 / 2 湖 / 3 河；河不算湖海 */
            float wx,wz;
            qlcg_world_cell_center(w,i,&wx,&wz);
            LakeShoreField lake=qlcg_lake_field(w,wx,wz);
            if (lake.lake) h=qlcg_lake_height(&lake,h);
            if (lake.lake || w->water[i]==2)
                wl=lake.lake && lake.d<0 && lake.level>h+.02f ? lake.level : -1e9f;
            bool is_lake = lake.lake && wl>h+.02f;
            bool is_river_cell = (w->water[i] == 3);
            bool is_wat = (w->water[i] == 1) || is_lake || (wl > h + 0.5f) || (h <= 0.0f);
            bool is_riv = is_river_cell || (!is_wat && w->river_width[i] >= 7.0f);

            int xl = wrap_col(x - 1, cols), xr = wrap_col(x + 1, cols);
            int yu = qlcg_clampi(y - 1, 0, rows - 1), yd = qlcg_clampi(y + 1, 0, rows - 1);
            float dx = (w->height[y * cols + xr] - w->height[y * cols + xl]) / (2.0f * cell_w);
            float dz = (w->height[yd * cols + x] - w->height[yu * cols + x]) / (2.0f * cell_w);
            float slope = hypotf(dx, dz);

            /* 九点极差与相对高度，按 32m 区块尺度折算 */
            float hmin = 1e9f, hmax = -1e9f, hsum = 0.0f;
            for (int oz = -1; oz <= 1; oz++) {
                int yy = qlcg_clampi(y + oz, 0, rows - 1);
                for (int ox = -1; ox <= 1; ox++) {
                    float hv = w->height[yy * cols + wrap_col(x + ox, cols)];
                    if (hv < hmin) hmin = hv;
                    if (hv > hmax) hmax = hv;
                    hsum += hv;
                }
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
                w->saline[i] != 0, h, t, aridity, relief, slope, relative, needs_name ? 0 : claim
            );
            w->chunk_type[i] = (int16_t)(needs_name ? 9001 : type);
            if (needs_name) qlcg_faction_generate_name(&w->factions[owner], type, &w->opt);

            float sh = qlcg_clampf(0.76f + (-dx * 16.0f - dz * 12.0f), 0.46f, 1.24f);
            w->shade[i] = is_wat ? 1.0f : 0.88f + 0.12f * sh;

            /* 1. base 高程色带：陆地对数高程+积雪，水体深度 */
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

            /* 2. 卫星图连续色（对齐 satellite.mjs） */
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
    /* 3. 河流段按 LOD 分级提取 */
    build_river_segments(w);
    w->prep_ms = qlcg_now_ms() - t0;
}

/* 生成完整宏观星球与人工文明骨架 */
