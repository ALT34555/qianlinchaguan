/* planet_artificial */
#include "planet_macro.h"
#include "world_internal.h"
#include "faction_naming.h"
#include "../../core/math_util.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void qlcg_world_update_artificial(MacroWorld *w, double unix_ms, int player_cx, int player_cz) {
    if (!w || !w->opt.artificial_enabled) return;
    int cur_day = (int)floor(unix_ms / 86400000.0);
    if (w->last_civ_day == 0) { w->last_civ_day = cur_day; return; }
    if (cur_day <= w->last_civ_day) return;
    w->last_civ_day = cur_day;

    /* 每隔数日按扩张速度向外扩领地并提升等级 */
    if (w->opt.natural_expansion > 0.0f && (cur_day % 12) == 0) {
        for (int f = 0; f < w->faction_count; ++f) {
            TerritoryFaction *tf = &w->factions[f];
            if (!tf->active) continue;
            if (w->opt.natural_evolution > 0.0f && tf->level < 5 && (cur_day % 60) == 0) {
                tf->level++;
            }
            int cell = tf->flag_cell;
            for (int k = 0; k < 8; ++k) {
                int nb = w->neighbors[cell * 8 + k];
                if (!w->water[nb] && w->claim_faction[nb] < 0) {
                    w->claim_faction[nb] = (int16_t)f;
                    w->claim_type[nb] = 1;
                    w->chunk_type[nb] = 9000;
                }
            }
        }
        w->territory_revision++;
    }

    /* 置旗自立（FlagIndependenc */
    if (w->opt.flag_independence && w->faction_count < MAX_FACTIONS && (cur_day % 30) == 0) {
        float pwx = (float)player_cx * (float)CHUNK_SIZE;
        float pwz = (float)player_cz * (float)CHUNK_SIZE;
        int pcell = qlcg_world_cell_at_wxz(w, pwx, pwz);
        int target = w->neighbors[pcell * 8 + (cur_day % 8)];
        if (!w->water[target] && w->claim_faction[target] < 0) {
            int fid = w->faction_count++;
            TerritoryFaction *nf = &w->factions[fid];
            nf->active = true;
            nf->id = fid;
            nf->is_player = false;
            nf->main_color = 0x7A548EFFu;
            nf->trim_color = 0xF0D890FFu;
            nf->flag_cell  = target;
            qlcg_world_cell_center(w, target, &nf->wx, &nf->wz);
            nf->cx = (int)floorf(nf->wx / (float)CHUNK_SIZE);
            nf->cz = (int)floorf(nf->wz / (float)CHUNK_SIZE);
            char salt[32];
            snprintf(salt, sizeof(salt), "day:%d", cur_day);
            qlcg_faction_init_identity(nf, w->opt.seed, salt, NULL);
            qlcg_faction_generate_name(nf, w->chunk_type[target], &w->opt);
            nf->level = 1;
            nf->claim_radius = 1;
            w->claim_faction[target] = (int16_t)fid;
            w->claim_type[target] = 2;
            w->chunk_type[target] = 9001;
            w->territory_revision++;
        }
    }
}

/* 默认舆图 11 层叠加配置（对齐 def */
