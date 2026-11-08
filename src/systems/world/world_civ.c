/* planet_civ */
#include "planet_macro.h"
#include "world_internal.h"
#include "faction_naming.h"
#include "../../core/math_util.h"
#include "platform/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void build_initial_civilizations(MacroWorld *w) {
    int N = w->cell_count;
    for (int i = 0; i < N; ++i) {
        w->claim_faction[i] = -1;
        w->claim_type[i] = 0;
    }
    w->faction_count = 0;

    /* 选出高分宜居出生点 */
    float best_sc = -1e9f;
    bool home=qlcg_world_flat(w) && !w->opt.plane_x && !w->opt.plane_z;
    bool choose_spawn=!qlcg_world_flat(w) || home;
    int best_spawn=choose_spawn?w->columns*(w->rows/2)+w->columns/2:-1;
    int fallback=-1;
    for (int i = 0; choose_spawn && i < N; ++i) {
        if (w->water[i]) continue;
        float h = w->height[i];
        float t = w->temperature[i];
        if(home) {
            if(t<8 || t>=24) continue;
            if(fallback<0 || h<w->height[fallback]) fallback=i;
        }
        if (h < 12.0f || h > 360.0f) continue;
        float m = w->moisture[i];
        float sc = 100.0f - fabsf(t - 20.0f) * 2.4f - fabsf(m - 0.62f) * 45.0f +
                   (w->river_width[i] > 6.0f ? 28.0f : 0.0f);
        if (sc > best_sc) { best_sc = sc; best_spawn = i; }
    }
    if(best_sc==-1e9f && fallback>=0) best_spawn=fallback;
    w->spawn_cell = best_spawn;
    w->spawn_wx=w->spawn_wz=0;w->spawn_cx=w->spawn_cz=0;
    if(best_spawn>=0) {
        qlcg_world_cell_center(w, best_spawn, &w->spawn_wx, &w->spawn_wz);
        w->spawn_cx = (int)floorf(w->spawn_wx / (float)CHUNK_SIZE);
        w->spawn_cz = (int)floorf(w->spawn_wz / (float)CHUNK_SIZE);
    }

    if (!w->opt.artificial_enabled) return;

    /* 1. 己方置旗地（9001） */
    if (w->opt.spawn_as_flag && (!qlcg_world_flat(w) || (!w->opt.plane_x && !w->opt.plane_z)) && w->faction_count < MAX_FACTIONS) {
        TerritoryFaction *tf = &w->factions[w->faction_count];
        tf->active = true;
        tf->id = 0;
        tf->is_player = true;
        tf->main_color = w->opt.faction_main_color;
        tf->trim_color = w->opt.faction_trim_color;
        tf->flag_cell  = best_spawn;
        tf->cx = w->spawn_cx;
        tf->cz = w->spawn_cz;
        qlcg_faction_init_identity(tf, w->opt.seed, "ren", w->opt.faction_name);
        tf->wx = w->spawn_wx;
        tf->wz = w->spawn_wz;
        tf->level = 1;
        tf->claim_radius = 2;
        tf->born_year = 0;
        w->claim_faction[best_spawn] = 0;
        w->claim_type[best_spawn] = 2;
        for (int k = 0; k < 8; ++k) {
            int nb = w->neighbors[best_spawn * 8 + k];
            if (!w->water[nb]) {
                w->claim_faction[nb] = 0;
                w->claim_type[nb] = 1;
            }
        }
        w->faction_count++;
    }

    /* 自然文明置旗地 */
    static const uint32_t CIV_COLORS[16] = {
        0x3B7A57FFu, 0x4B6C8CFFu, 0x8A5A96FFu, 0xB87333FFu,
        0x568203FFu, 0x9E4747FFu, 0x2E6F78FFu, 0xA67B3BFFu,
        0x685286FFu, 0x3F704DFFu, 0x8C4A32FFu, 0x4A6B82FFu,
        0x9B7643FFu, 0x5C7858FFu, 0x7A546EFFu, 0x3D647AFFu
    };

    uint32_t rng = w->opt.seed ^ 0xA5A55A5Au;
    int target_civs = qlcg_clampi(w->opt.initial_civ_limit, 0, 48);
    int spawned_civs = 0;
    for (int attempt = 0; attempt < target_civs * 24 && spawned_civs < target_civs && w->faction_count < MAX_FACTIONS; ++attempt) {
        int cell = (int)(qlcg_rand01(&rng) * (float)N) % N;
        if (w->water[cell] || w->claim_faction[cell] >= 0) continue;
        if (w->height[cell] < 8.0f || w->height[cell] > 420.0f) continue;
        if (w->moisture[cell] < 0.22f || w->temperature[cell] < -4.0f) continue;

        float wx, wz;
        qlcg_world_cell_center(w, cell, &wx, &wz);
        bool too_close = false;
        for (int f = 0; f < w->faction_count; ++f) {
            float dx = fabsf(wx - w->factions[f].wx);
            if (dx > w->circumference * 0.5f) dx = w->circumference - dx;
            float dz = wz - w->factions[f].wz;
            if (hypotf(dx, dz) < w->circumference * 0.045f) { too_close = true; break; }
        }
        if (too_close) continue;

        int fid = w->faction_count++;
        TerritoryFaction *tf = &w->factions[fid];
        tf->active = true;
        tf->id = fid;
        tf->is_player = false;
        tf->main_color = CIV_COLORS[spawned_civs % 16];
        tf->trim_color = 0xE8D8B8FFu;
        tf->flag_cell  = cell;
        tf->wx = wx;
        tf->wz = wz;
        tf->cx = (int)floorf(wx / (float)CHUNK_SIZE);
        tf->cz = (int)floorf(wz / (float)CHUNK_SIZE);
        qlcg_faction_init_identity(tf, w->opt.seed, "initial", NULL);
        tf->level = 1 + (spawned_civs % 3);
        tf->claim_radius = 1;
        tf->born_year = 0;
        w->claim_faction[cell] = (int16_t)fid;
        w->claim_type[cell] = 2;
        for (int k = 0; k < 4; ++k) {
            int nb = w->neighbors[cell * 8 + k];
            if (!w->water[nb] && w->claim_faction[nb] < 0) {
                w->claim_faction[nb] = (int16_t)fid;
                w->claim_type[nb] = 1;
            }
        }
        spawned_civs++;
    }
}

/* 重建舆图底色、模拟卫星图、坡度阴影与主河矢量 */
