/* planet_artificial */
#include "planet_macro.h"
#include "world_internal.h"
#include "world_civ_growth.h"
#include "world_civ_economy.h"
#include "world_claims.h"
#include "../dolls/doll_life.h"
#include "world_infinite.h"
#include "event_state.h"
#include <limits.h>
#include "faction_naming.h"
#include "../../core/math_util.h"
#include "platform/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

enum { CIV_DAYS_PER_UPDATE=32 };

static void grow_tile(MacroWorld *w, int previous_day, int cur_day, int player_cx, int player_cz) {
    if (!w->neighbors || !w->claim_faction || !w->claim_type || !w->water || !w->chunk_type) return;
    if (qlcg_civ_growth_step(w,previous_day,cur_day)) w->territory_revision++;
    /* 置旗自立 */
    if (w->opt.flag_independence && w->faction_count < MAX_FACTIONS && (int64_t)floor((double)cur_day/30)>(int64_t)floor((double)previous_day/30)) {
        if(w->chunk_claims) {
            for(int step=0;step<4;step++) {
                int direction=((unsigned)cur_day+step)%4;
                int cx=player_cx+qlcg_claim_directions[direction][0];
                int cz=player_cz+qlcg_claim_directions[direction][1];
                if(!qlcg_claims_canonical(w,&cx,cz)||qlcg_claims_at(w,cx,cz)||!qlcg_claims_land(w,cx,cz)) continue;
                float x=(cx+.5f)*CHUNK_SIZE,z=(cz+.5f)*CHUNK_SIZE;
                int cell=qlcg_world_cell_at_wxz(w,x,z),id=w->faction_count;
                TerritoryFaction f={.active=true,.id=id,
                    .flag_cell=cell,.cx=cx,.cz=cz,.wx=x,.wz=z,.level=1,.claim_radius=1};
                char salt[32];
                snprintf(salt,sizeof(salt),"day:%d",cur_day);
                qlcg_faction_init_identity(&f,w->opt.seed,salt,NULL);
                qlcg_faction_generate_name(&f,w->chunk_type[cell],&w->opt);
                if(!qlcg_claims_seed(w,&f)) break;
                w->factions[w->faction_count++]=f;
                w->territory_revision++;
                qlcg_civ_economy_update(w,cur_day,cur_day,false);
                break;
            }
            return;
        }
        float pwx = (float)player_cx * (float)CHUNK_SIZE;
        float pwz = (float)player_cz * (float)CHUNK_SIZE;
        int pcell = qlcg_world_cell_at_wxz(w, pwx, pwz);
        if (pcell<0 || pcell>=w->cell_count) return;
        int target = w->neighbors[pcell * 8 + (unsigned)cur_day % 8];
        if (target>=0 && target<w->cell_count && !w->water[target] && w->claim_faction[target] < 0) {
            int fid = w->faction_count++;
            TerritoryFaction *nf = &w->factions[fid];
            nf->active = true;
            nf->id = fid;
            nf->is_player = false;
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
            qlcg_civ_economy_update(w,cur_day,cur_day,false);
        }
    }
}

static void update_tile(MacroWorld *w,int cur_day,int player_cx,int player_cz,bool jump,bool schedule) {
    int previous_day=w->last_civ_day;
    bool enabled=w->opt.artificial_enabled;
    if (cur_day<previous_day) return;
    if (previous_day==INT_MIN || cur_day==previous_day || jump || (!enabled && (!schedule || !w->auto_events))) {
        if (schedule) qlcg_schedule_natural_event(w,cur_day,!jump);
        if (cur_day==previous_day && enabled==w->last_civ_enabled && w->civ_economy) return;
        if (qlcg_civ_economy_update(w,previous_day,cur_day,false)) {
            w->last_civ_enabled=enabled;
            w->last_civ_day=cur_day;
        }
        return;
    }
    int64_t stop=(int64_t)previous_day+CIV_DAYS_PER_UPDATE;
    if (stop>cur_day) stop=cur_day;
    for (int day=previous_day+1;day<=stop;day++) {
        if (schedule) qlcg_schedule_natural_event(w,day,true);
        if (!qlcg_civ_economy_update(w,day-1,day,enabled)) break;
        w->last_civ_enabled=enabled;
        w->last_civ_day=day;
        if (enabled) grow_tile(w,day-1,day,player_cx,player_cz);
    }
}

static void update_artificial(MacroWorld *w,double unix_ms,int player_cx,int player_cz,bool jump) {
    if (!w || !isfinite(unix_ms)) return;
    double day=floor(unix_ms/86400000.0);
    if (day<=INT_MIN || day>INT_MAX-2440588) return;
    int cur_day=(int)day;
    double x=((double)player_cx+.5)*CHUNK_SIZE,z=((double)player_cz+.5)*CHUNK_SIZE;
    const MacroWorld *player_tile=qlcg_world_flat(w)?qlcg_infinite_resolve(w,x,z,&x,&z):w;
    update_tile(w,cur_day,player_tile==w?player_cx:w->spawn_cx,player_tile==w?player_cz:w->spawn_cz,jump,true);
    if (!w || !w->planes || w->planes->count<2) return;
    static const MacroWorld *last_root;
    static size_t cursor;
    if (last_root!=w) {last_root=w;cursor=0;}
    size_t count=w->planes->count,steps=jump||count<8?count:8;
    for (size_t step=0;step<steps;step++) {
        MacroWorld *tile=w->planes->items[(cursor++)%count];
        if (tile==w) continue;
        uint32_t surface=tile->surface_revision,farms=qlcg_civ_farms_revision(tile),territory=tile->territory_revision;
        tile->plant_calendar=w->plant_calendar;
        update_tile(tile,cur_day,player_tile==tile?(int)floor(x/CHUNK_SIZE):tile->spawn_cx,
                    player_tile==tile?(int)floor(z/CHUNK_SIZE):tile->spawn_cz,jump,false);
        if (tile->surface_revision!=surface || qlcg_civ_farms_revision(tile)!=farms)
            qlcg_civ_economy_invalidate_farms(w,tile->surface_revision!=surface);
        if (tile->territory_revision!=territory) w->territory_revision++;
    }
}

void qlcg_world_update_artificial(MacroWorld *w,double unix_ms,int player_cx,int player_cz) {
    update_artificial(w,unix_ms,player_cx,player_cz,false);
}

void qlcg_world_jump_artificial_date(MacroWorld *w,double unix_ms,int player_cx,int player_cz) {
    update_artificial(w,unix_ms,player_cx,player_cz,true);
    if (!w || !isfinite(unix_ms)) return;
    if (!w->planes) qlcg_doll_life_jump(w,unix_ms);
    else for (size_t i=0;i<w->planes->count;i++) qlcg_doll_life_jump(w->planes->items[i],unix_ms);
}
