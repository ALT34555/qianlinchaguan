#include "world_lot.h"
#include "world_civ_economy.h"
#include "world_claims.h"
#include "world_settlement.h"
#include "world_farm_surface.h"
#include "../dolls/doll_life.h"
#include "world_infinite.h"
#include "climate_season.h"
#include "model/plant_def.h"
#include "../calendar/calendar.h"
#include "../civilization/civ_content.h"
#include "core/binary.h"
#include "core/i18n.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int active,cx,cz,age,last_harvest,stage,season;
    char farm_type_id[QLCG_CIV_TYPE_ID],crop_id[QLCG_CIV_TYPE_ID];
    uint32_t surface_revision,delta_version;
    bool dry_sampled,dry;
} CivFarm;
typedef struct {
    char uid[17];
    int initialized,population,food,wood,fiber,last_day,starvation,growth_days,work_days,expanded,harvests,cycle_days;
    CivFarm farms[QLCG_CIV_FARMS];
} CivHousehold;
struct CivEconomy { uint32_t revision,farm_revision;CivHousehold factions[MAX_FACTIONS]; };

static void farm_appearance_changed(MacroWorld *w) {
    uint32_t prior=w->surface_revision++;
    qlcg_world_surface_appearance(w,prior);
}
static void farm_changed(MacroWorld *w,bool land) {
    w->civ_economy->farm_revision++;
    if (land) farm_appearance_changed(w);
}
void qlcg_civ_economy_invalidate_farms(MacroWorld *w,bool land) {
    if (!w) return;
    if (w->civ_economy) farm_changed(w,land);
    else if (land) farm_appearance_changed(w);
}

void qlcg_civ_economy_free(MacroWorld *w) {
    if (w) {free(w->civ_economy);w->civ_economy=NULL;}
}

static bool valid_flag(const MacroWorld *w,const TerritoryFaction *f) {
    if(w&&w->chunk_claims&&w->opt.artificial_enabled&&f&&f->active) {
        const QlcgChunkClaim *claim=qlcg_claims_at(w,f->cx,f->cz);
        return claim&&claim->faction==f->id&&claim->type==QLCG_FLAG_CHUNK;
    }
    int cell=f?f->flag_cell:-1;
    return w && f && f->active && w->opt.artificial_enabled && cell>=0 && cell<w->cell_count &&
        w->claim_faction && w->water && w->claim_faction[cell]==f->id && !w->water[cell] &&
        (!w->claim_type || w->claim_type[cell]==2);
}
float qlcg_civ_labor_efficiency(const MacroWorld *w,const TerritoryFaction *f) {
    if (!valid_flag(w,f)) return 0;
    int cell=f->flag_cell;
    float moisture=w->moisture?w->moisture[cell]:.5f;
    float forest=w->forest_bonus?w->forest_bonus[cell]:.5f;
    if (!isfinite(moisture) || !isfinite(forest)) return 0;
    moisture=fmaxf(0,fminf(1,moisture));forest=fmaxf(0,fminf(1,forest));
    return fminf(1.25f,.5f+.75f*moisture+.25f*forest);
}
static CivHousehold *household(const MacroWorld *w,const TerritoryFaction *f) {
    if (!w || !f || !w->civ_economy || f->id<0 || f->id>=MAX_FACTIONS) return NULL;
    CivHousehold *h=&w->civ_economy->factions[f->id];
    return h->initialized && !memcmp(h->uid,f->uid,17)?h:NULL;
}
static int farms_count(const CivHousehold *h) {
    return h->farms[0].active+h->farms[1].active;
}
static bool owned_chunk(const MacroWorld *w,const TerritoryFaction *f,int cx,int cz);
static bool plot_dry(const MacroWorld *w,int cx,int cz,float yaw,const QlcgFarmlandType *type) {
    if (!w->decor_ready) return !w->chunk_claims || qlcg_claims_land(w,cx,cz);
    if (!type || !qlcg_claims_canonical(w,&cx,cz)) return false;
    QlcgFarmPlot plot={.width=type->width,.depth=type->depth,.yaw=yaw};
    float x=(cx+.5f)*CHUNK_SIZE,z=(cz+.5f)*CHUNK_SIZE;
    float points[QLCG_FARM_FOOTPRINT_POINTS][2];
    qlcg_farm_surface_footprint(&plot,points);
    bool cutoff=false;
    for (int k=0;k<QLCG_FARM_FOOTPRINT_POINTS;k++) {
        float ground,water;
        SurfaceWater site;
        qlcg_sample_surface_water(w,x+points[k][0],z+points[k][1],0,&ground,&water,NULL,NULL,NULL,NULL,
            NULL,NULL,k==0?&site:NULL,NULL);
        if (k==0) cutoff=site.form==WATER_FORM_CUTOFF;
        if (!qlcg_farm_surface_dry(ground,cutoff?-1e9f:water)) return false;
    }
    return true;
}
static bool farm_dry(const MacroWorld *w,const TerritoryFaction *f,const CivFarm *farm) {
    if (w->decor_ready && farm->dry_sampled && farm->surface_revision==w->surface_revision &&
        farm->delta_version==w->delta_version) return farm->dry;
    return plot_dry(w,farm->cx,farm->cz,qlcg_settlement_yaw(f),qlcg_farmland_type_find(farm->farm_type_id));
}
static const QlcgCropType *farm_crop(const CivFarm *farm) {
    if (!farm->active || !qlcg_farmland_type_find(farm->farm_type_id)) return NULL;
    const QlcgCropType *crop=qlcg_crop_type_find(farm->crop_id);
    return crop && !strcmp(crop->farmland_id,farm->farm_type_id)?crop:NULL;
}
static int crop_cycle_for(const MacroWorld *w,const TerritoryFaction *f) {
    int cell=f->flag_cell;
    if(cell<0 || cell>=w->cell_count) return 365;
    float mean=w->temperature?w->temperature[cell]:15,amp=w->season_amp?w->season_amp[cell]:0;
    if(!isfinite(mean) || !isfinite(amp) || mean<8) return 365;
    amp=fmaxf(0,amp);
    bool perennial=amp<.5f || mean-amp>15 || (mean-amp>=8 && mean+amp<=15);
    return perennial?90:365;
}
static const CivFarm *grain_farm_for(const CivHousehold *h,const QlcgCropType **definition) {
    for (int p=0;p<QLCG_CIV_FARMS;p++) {
        const QlcgCropType *crop=farm_crop(h->farms+p);
        if (crop && crop->annual_food>0) {
            if (definition) *definition=crop;
            return h->farms+p;
        }
    }
    return NULL;
}
static int food_cycle_for(const CivHousehold *h,bool perennial) {
    const QlcgCropType *crop=NULL;grain_farm_for(h,&crop);
    return crop && perennial?crop->continuous_days:365;
}
static int capacity_for(const MacroWorld *w,const TerritoryFaction *f,const CivHousehold *h,
                        bool flag_valid,const bool *farm_owned) {
    if (!flag_valid) return 1;
    int cell=f->flag_cell;
    float mean=w->temperature?w->temperature[cell]:15,amp=w->season_amp?w->season_amp[cell]:0;
    float wet=w->moisture?w->moisture[cell]:.5f;
    if (!isfinite(mean) || !isfinite(amp) || !isfinite(wet)) return 1;
    int annual_food=365;bool perennial=crop_cycle_for(w,f)==90;
    for(int p=0;p<QLCG_CIV_FARMS;p++) {
        const QlcgCropType *crop=farm_crop(h->farms+p);
        if (crop && mean+amp>=crop->temperature.min && mean-amp<=crop->temperature.max &&
            wet>=crop->moisture.min && wet<=crop->moisture.max &&
            (perennial || crop->maturity_days<=365) &&
            (farm_owned?farm_owned[p]:(owned_chunk(w,f,h->farms[p].cx,h->farms[p].cz) && farm_dry(w,f,h->farms+p))))
            annual_food+=crop->annual_food;
    }
    int capacity=annual_food*85/100/365;
    return capacity<1?1:capacity>QLCG_CIV_POP_MAX?QLCG_CIV_POP_MAX:capacity;
}
static int food_reserve_for(const CivHousehold *h,int population) {
    int remaining=h->cycle_days-h->work_days;
    const CivFarm *grain=grain_farm_for(h,NULL);
    if (!grain) remaining=365;
    else if(h->harvests) {
        int64_t elapsed=(int64_t)h->last_day-grain->last_harvest;
        if (elapsed<=h->work_days) remaining=elapsed<h->cycle_days?h->cycle_days-(int)elapsed:30;
    }
    if(remaining<30)remaining=30;
    return (population>1?population-1:0)*remaining+population*30;
}
static void add_resource(int *value,int amount) {
    int64_t total=(int64_t)*value+amount;
    *value=total>QLCG_CIV_RESOURCE_MAX?QLCG_CIV_RESOURCE_MAX:total<0?0:(int)total;
}
bool qlcg_civ_economy_ration(const MacroWorld *w,const TerritoryFaction *f,int utc_day) {
    const CivHousehold *h=household(w,f);
    return h && w->opt.artificial_enabled && f->active && h->last_day>=utc_day && !h->starvation;
}
bool qlcg_civ_economy_credit_harvest(MacroWorld *w,const TerritoryFaction *f,int food,int wood,int fiber) {
    CivHousehold *h=household(w,f);
    if (!h || !w->opt.artificial_enabled || !f->active || food<0 || wood<0 || fiber<0) return false;
    add_resource(&h->food,food);add_resource(&h->wood,wood);add_resource(&h->fiber,fiber);
    w->civ_economy->revision++;
    return true;
}
static bool owned_chunk(const MacroWorld *w,const TerritoryFaction *f,int cx,int cz) {
    if(w->chunk_claims) {
        const QlcgChunkClaim *claim=qlcg_claims_at(w,cx,cz);
        return claim&&claim->faction==f->id;
    }
    if (w->columns<=0 || w->rows<=0 || w->circumference<=0) return false;
    float x=((float)cx+.5f)*CHUNK_SIZE,z=((float)cz+.5f)*CHUNK_SIZE;
    if (qlcg_world_flat(w)) {
        double lx,lz;
        if (qlcg_infinite_resolve(w,x,z,&lx,&lz)!=w) return false;
    } else if (fabsf(z)>w->circumference*.25f) return false;
    int cell=qlcg_world_cell_at_wxz(w,x,z);
    return cell>=0 && cell<w->cell_count && w->claim_faction[cell]==f->id && !w->water[cell];
}
static void open_farm(MacroWorld *w,const TerritoryFaction *f,CivHousehold *h,int slot,int day) {
    CivFarm *farm=h->farms+slot;
    if (farm->active || h->food<3 || h->wood<6 || h->fiber<2) return;
    const QlcgCropType *crop=farm->crop_id[0]?qlcg_crop_type_find(farm->crop_id):
        qlcg_crop_type_select(w,f->flag_cell,slot?"fiber":"food");
    const QlcgFarmlandType *type=crop?qlcg_farmland_type_find(crop->farmland_id):NULL;
    if (!type) return;
    if (farm->farm_type_id[0] && strcmp(farm->farm_type_id,crop->farmland_id)) return;
    float yaw=qlcg_settlement_yaw(f);
    int c=(int)lroundf(cosf(yaw)),s=(int)lroundf(sinf(yaw));
    int dx=slot?0:1,dz=slot?1:0;
    int cx=f->cx+dx*c-dz*s,cz=f->cz+dx*s+dz*c;
    if (!valid_flag(w,f) || !owned_chunk(w,f,cx,cz) || !plot_dry(w,cx,cz,yaw,type)) return;
    memset(farm,0,sizeof(*farm));farm->active=1;farm->cx=cx;farm->cz=cz;
    farm->last_harvest=day>INT_MIN+400?day-400:INT_MIN;farm->stage=PD_STATE_PLANTED;farm->season=1;
    memcpy(farm->farm_type_id,crop->farmland_id,QLCG_CIV_TYPE_ID);
    memcpy(farm->crop_id,crop->id,QLCG_CIV_TYPE_ID);
    h->food-=3;h->wood-=6;h->fiber-=2;
    w->civ_economy->revision++;
    farm_changed(w,true);
}
static bool prepare(MacroWorld *w,int day) {
    if (!w->civ_economy) w->civ_economy=calloc(1,sizeof(*w->civ_economy));
    if (!w->civ_economy) return false;
    for (int f=0;f<w->faction_count && f<MAX_FACTIONS;f++) {
        const TerritoryFaction *tf=w->factions+f;
        if (!tf->active || tf->id!=f) continue;
        CivHousehold *h=&w->civ_economy->factions[f];
        if (!h->initialized || memcmp(h->uid,tf->uid,17)) {
            memset(h,0,sizeof(*h));memcpy(h->uid,tf->uid,17);
            h->initialized=1;h->population=QLCG_CIV_INITIAL_POPULATION;h->food=900;h->wood=48;h->fiber=24;h->last_day=day;h->cycle_days=crop_cycle_for(w,tf);
            open_farm(w,tf,h,0,day);
            h->cycle_days=food_cycle_for(h,h->cycle_days==90);
            w->civ_economy->revision++;
        }
    }
    return true;
}

static void farm_day(MacroWorld *w,const TerritoryFaction *f,CivHousehold *h,CivFarm *farm,int plot,int day,
                     const QlcgClimateSeason *climate,bool owned,bool perennial) {
    if (!farm->active) return;
    const QlcgCropType *crop=farm_crop(farm);if (!crop) return;
    int old_stage=farm->stage,old_season=farm->season;
    farm->season=climate->season;
    bool viable=owned && isfinite(climate->temperature) && isfinite(climate->moisture) &&
        climate->temperature>=crop->temperature.min && climate->temperature<=crop->temperature.max &&
        climate->moisture>=crop->moisture.min && climate->moisture<=crop->moisture.max;
    if (farm->age<0) {
        if (!viable || h->food<3) {
            if (farm->season!=old_season) farm_changed(w,false);
            return;
        }
        h->food-=3;farm->age=0;farm->stage=PD_STATE_PLANTED;
        farm_changed(w,false);return;
    }
    bool harvest=false;
    if (!viable) {farm->age=0;farm->stage=PD_STATE_WITHERED;}
    else if (perennial) {
        farm->age++;
        farm->stage=farm->age<crop->continuous_days/4?PD_STATE_SEEDLING:
            farm->age<crop->continuous_days/2?PD_STATE_SUBADULT:
            farm->age<(crop->continuous_days*3+3)/4?PD_STATE_FLOWERING:PD_STATE_FRUITING;
        harvest=farm->age>=crop->continuous_days && day>farm->last_harvest;
    } else {
        if (farm->age<3650) farm->age++;
        if (climate->season==0) farm->stage=PD_STATE_SEEDLING;
        else if (climate->season==1) farm->stage=climate->since_days<climate->until_days?PD_STATE_SUBADULT:PD_STATE_FLOWERING;
        else if (climate->season==2 && (int64_t)day-farm->last_harvest<180) {farm->age=0;farm->stage=PD_STATE_PLANTED;}
        else if (climate->season==2 && farm->age>=crop->maturity_days) {
            farm->stage=PD_STATE_FRUITING;
            harvest=old_stage==PD_STATE_FRUITING && (int64_t)day-farm->last_harvest>=180;
        } else farm->stage=PD_STATE_PLANTED;
    }
    if (harvest) {
        int64_t scale=perennial?crop->continuous_days:360;
        int food=(int)((int64_t)crop->annual_food*scale/360),wood=(int)((int64_t)crop->annual_wood*scale/360);
        int fiber=(int)((int64_t)crop->annual_fiber*scale/360);
        if (!qlcg_doll_life_offer_harvest(w,f,plot,day,food,wood,fiber)) {
            add_resource(&h->food,food);add_resource(&h->wood,wood);add_resource(&h->fiber,fiber);
        }
        farm->age=h->food>=3?0:-1;if (farm->age==0) h->food-=3;
        farm->last_harvest=day;farm->stage=PD_STATE_PLANTED;
        if (h->harvests<1000000) h->harvests++;
        w->civ_economy->revision++;
    }
    if (farm->stage!=old_stage || farm->season!=old_season) farm_changed(w,false);
}

bool qlcg_civ_economy_update(MacroWorld *w,int previous_day,int current_day,bool simulate) {
    if (!w || current_day==INT_MIN || current_day>INT_MAX-2440588 || current_day<previous_day) return false;
    int baseline=previous_day==INT_MIN?current_day:previous_day;
    if (!prepare(w,baseline)) return false;
    for (int64_t d=(int64_t)baseline+1;simulate && d<=current_day;d++) {
        CalendarSnapshot date=w->plant_calendar?*w->plant_calendar:(CalendarSnapshot){0};
        date.jdn=(int)(d+2440588);date.day_frac=.5;date.utc_offset_min=0;
        for (int f=0;f<w->faction_count && f<MAX_FACTIONS;f++) {
            TerritoryFaction *tf=w->factions+f;CivHousehold *h=household(w,tf);
            if (!h || !tf->active || d<=h->last_day) continue;
            h->last_day=(int)d;
            bool flag_valid=valid_flag(w,tf),farm_owned[QLCG_CIV_FARMS]={false};
            if (flag_valid) {
                h->work_days=h->work_days<1000000?h->work_days+1:h->work_days;
                open_farm(w,tf,h,0,(int)d);
                if (h->work_days>=6 && h->population>=3) open_farm(w,tf,h,1,(int)d);
                if (farms_count(h)) {
                    QlcgClimateSeason climate;qlcg_climate_season_at(w,tf->flag_cell,&date,&climate);
                    bool perennial=crop_cycle_for(w,tf)==90;
                    h->cycle_days=food_cycle_for(h,perennial);
                    for (int p=0;p<QLCG_CIV_FARMS;p++) if (h->farms[p].active) {
                        CivFarm *farm=h->farms+p;
                        farm->dry=farm_dry(w,tf,farm);
                        farm->dry_sampled=true;
                        farm->surface_revision=w->surface_revision;
                        farm->delta_version=w->delta_version;
                        farm_owned[p]=farm->dry && owned_chunk(w,tf,farm->cx,farm->cz);
                        farm_day(w,tf,h,farm,p,(int)d,&climate,farm_owned[p],perennial);
                    }
                }
                add_resource(&h->wood,(h->population+2)/3);
                if (d%2==0) add_resource(&h->fiber,1);
                add_resource(&h->food,1);
            }
            if (h->food>=h->population) {h->food-=h->population;h->starvation=0;}
            else {
                h->food=0;h->starvation=h->starvation<1000000?h->starvation+1:h->starvation;
                if (h->starvation%7==0 && h->population>1) h->population--;
            }
            h->growth_days=h->growth_days<30?h->growth_days+1:30;
            if (h->harvests && h->growth_days>=30 && h->population<capacity_for(w,tf,h,flag_valid,farm_owned) &&
                h->food>=food_reserve_for(h,h->population+1)+24 && h->wood>=6 && h->fiber>=4 && !h->starvation) {
                h->food-=24;h->wood-=6;h->fiber-=4;h->population++;h->growth_days=0;
            }
            w->civ_economy->revision++;
        }
    }
    for (int f=0;!simulate && f<w->faction_count && f<MAX_FACTIONS;f++) {
        CivHousehold *h=household(w,w->factions+f);
        if (h && current_day>h->last_day) h->last_day=current_day;
    }
    return true;
}

QlcgCivCost qlcg_civ_expand_cost(void) {return (QlcgCivCost){12,6,2,3};}
QlcgCivCost qlcg_civ_upgrade_cost(int level) {
    level=level<1?1:level>5?5:level;
    return (QlcgCivCost){30*level,20*level,10*level,3+(level+1)/2};
}
static int cost_block_checked(const TerritoryFaction *f,const CivHousehold *h,bool flag_valid,
                              QlcgCivCost cost,bool expansion,int reserve) {
    if (!flag_valid || !h) return CIV_BLOCK_FLAG;
    if (!expansion && f->level>=5) return CIV_BLOCK_LEVEL_LIMIT;
    if (expansion && h->expanded>=h->population*4+f->level*8) return CIV_BLOCK_LAND_LIMIT;
    if (h->population<cost.population || h->starvation) return CIV_BLOCK_POPULATION;
    if (h->food<cost.food+reserve) return CIV_BLOCK_FOOD;
    if (h->wood<cost.wood) return CIV_BLOCK_WOOD;
    if (h->fiber<cost.fiber) return CIV_BLOCK_FIBER;
    return CIV_BLOCK_NONE;
}
static int cost_block(const MacroWorld *w,const TerritoryFaction *f,QlcgCivCost cost,bool expansion) {
    const CivHousehold *h=household(w,f);
    return cost_block_checked(f,h,valid_flag(w,f),cost,expansion,h?food_reserve_for(h,h->population):0);
}
int qlcg_civ_economy_expand_block(const MacroWorld *w,const TerritoryFaction *f) {
    return cost_block(w,f,qlcg_civ_expand_cost(),true);
}
int qlcg_civ_economy_upgrade_block(const MacroWorld *w,const TerritoryFaction *f) {
    return cost_block(w,f,qlcg_civ_upgrade_cost(f?f->level:1),false);
}
static bool pay(MacroWorld *w,const TerritoryFaction *f,QlcgCivCost cost,bool expansion) {
    CivHousehold *h=household(w,f);
    if (cost_block_checked(f,h,valid_flag(w,f),cost,expansion,h?food_reserve_for(h,h->population):0)!=CIV_BLOCK_NONE) return false;
    h->food-=cost.food;h->wood-=cost.wood;h->fiber-=cost.fiber;
    if (expansion) h->expanded++;
    w->civ_economy->revision++;return true;
}
bool qlcg_civ_economy_pay_expand(MacroWorld *w,const TerritoryFaction *f) {return pay(w,f,qlcg_civ_expand_cost(),true);}
bool qlcg_civ_economy_pay_upgrade(MacroWorld *w,const TerritoryFaction *f) {return pay(w,f,qlcg_civ_upgrade_cost(f?f->level:1),false);}
int qlcg_civ_expansion_budget_used(const MacroWorld *w,const TerritoryFaction *f) {
    const CivHousehold *h=household(w,f);
    return h?h->expanded:0;
}
void qlcg_civ_restore_expansion_budget(MacroWorld *w,const TerritoryFaction *f,int used) {
    CivHousehold *h=household(w,f);
    if(h) h->expanded=used;
}
int qlcg_civ_npc_count(const MacroWorld *w,const TerritoryFaction *f) {
    const CivHousehold *h=household(w,f);
    return (h?h->population:QLCG_CIV_INITIAL_POPULATION)-(f->is_player?1:0);
}

float qlcg_civ_economy_labor_scale(const MacroWorld *w,const TerritoryFaction *f) {
    const CivHousehold *h=household(w,f);
    return h?(h->starvation?0.f:fminf(2,(float)h->population/3)):1;
}
bool qlcg_civ_economy_snapshot(const MacroWorld *w,const TerritoryFaction *f,QlcgCivEconomySnapshot *out) {
    const CivHousehold *h=household(w,f);if (!h || !out) return false;
    bool flag_valid=valid_flag(w,f);int reserve=food_reserve_for(h,h->population);
    *out=(QlcgCivEconomySnapshot){h->population,capacity_for(w,f,h,flag_valid,NULL),h->food,h->wood,h->fiber,farms_count(h),
        h->expanded,h->population*4+f->level*8,qlcg_claims_count(w,f->id),f->level,h->starvation,
        cost_block_checked(f,h,flag_valid,qlcg_civ_expand_cost(),true,reserve),
        cost_block_checked(f,h,flag_valid,qlcg_civ_upgrade_cost(f->level),false,reserve),reserve,w->civ_economy->revision};
    return true;
}
static const MacroWorld *owner_at(const MacroWorld *w,int cx,int cz,int *local_cx,int *local_cz,int *cell) {
    if (!w) return NULL;
    double x=((double)cx+.5)*CHUNK_SIZE,z=((double)cz+.5)*CHUNK_SIZE,lx=x,lz=z;
    const MacroWorld *owner=qlcg_world_flat(w)?qlcg_infinite_resolve(w,x,z,&lx,&lz):w;
    if (!owner || owner->columns<=0 || owner->rows<=0 || owner->circumference<=0) return NULL;
    *local_cx=(int)floor(lx/CHUNK_SIZE);*local_cz=(int)floor(lz/CHUNK_SIZE);
    *cell=qlcg_world_cell_at_wxz(owner,(float)lx,(float)lz);return owner;
}
bool qlcg_civ_territory_at(const MacroWorld *w,int cx,int cz,QlcgCivTerritory *out) {
    if (!out) return false;
    memset(out,0,sizeof(*out));
    int lx,lz,cell;const MacroWorld *v=owner_at(w,cx,cz,&lx,&lz,&cell);
    if (!v || cell<0 || cell>=v->cell_count) return false;
    const QlcgChunkClaim *chunk=qlcg_claims_at(v,lx,lz);
    int id=-1,type=0;
    if(chunk) {id=chunk->faction;type=chunk->type;}
    else if(!v->chunk_claims&&v->claim_faction) {id=v->claim_faction[cell];type=v->claim_type?v->claim_type[cell]:0;}
    if (id<0 || id>=v->faction_count) return false;
    const TerritoryFaction *f=v->factions+id;if (!f->active) return false;
    *out=(QlcgCivTerritory){v,f,lx,lz,cell,type};return true;
}
bool qlcg_civ_economy_at(const MacroWorld *w,int cx,int cz,const MacroWorld **owner,
                       const TerritoryFaction **f,QlcgCivEconomySnapshot *out) {
    if (owner) *owner=NULL;if (f) *f=NULL;
    QlcgCivTerritory territory;
    if (!qlcg_civ_territory_at(w,cx,cz,&territory) || !qlcg_civ_economy_snapshot(territory.owner,territory.faction,out)) return false;
    if (owner) *owner=territory.owner;if (f) *f=territory.faction;return true;
}
static bool farm_plot(const TerritoryFaction *f,const CivFarm *farm,int slot,float yaw,float c,float s,QlcgFarmPlot *out) {
    const QlcgCropType *crop=farm_crop(farm);
    const QlcgFarmlandType *type=crop?qlcg_farmland_type_find(farm->farm_type_id):NULL;
    if (!type) return false;
    float edge_x=fmaxf(0,type->width*.5f-.4f),edge_z=fmaxf(0,type->depth*.5f-.4f);
    float wx=slot?fminf(.8f,edge_x):-edge_x,wz=slot?-edge_z:fminf(.8f,edge_z);
    *out=(QlcgFarmPlot){slot,f->id,farm->cx,farm->cz,0,0,type->width,type->depth,
        wx*c-wz*s,wx*s+wz*c,yaw,farm->stage,farm->season,farm->crop_id,farm->farm_type_id,crop->plant_id};
    return true;
}
int qlcg_civ_farms(const MacroWorld *w,const TerritoryFaction *f,QlcgFarmPlot *out,int capacity) {
    const CivHousehold *h=household(w,f);if (!h || !out || capacity<=0 || !valid_flag(w,f)) return 0;
    float yaw=qlcg_settlement_yaw(f),c=cosf(yaw),s=sinf(yaw);int count=0;
    for (int p=0;p<QLCG_CIV_FARMS && count<capacity;p++) {
        const CivFarm *farm=h->farms+p;if (!farm->active || !owned_chunk(w,f,farm->cx,farm->cz)) continue;
        if (!qlcg_lot_local(w,farm->cx,farm->cz) && farm_plot(f,farm,p,yaw,c,s,out+count)) count++;
    }
    return count;
}
static const CivFarm *farm_at_territory(const QlcgCivTerritory *territory,int *slot) {
    const MacroWorld *owner=territory->owner;const TerritoryFaction *f=territory->faction;
    const CivHousehold *h=household(owner,f);
    if (!h || !valid_flag(owner,f) || (!owner->chunk_claims&&(!owner->water||owner->water[territory->cell]))) return NULL;
    if (!qlcg_world_flat(owner) && fabs(((double)territory->cz+.5)*CHUNK_SIZE)>owner->circumference*.25) return NULL;
    int period=owner->circumference>0?(int)lround(owner->circumference/CHUNK_SIZE):0;
    for (int p=0;p<QLCG_CIV_FARMS;p++) {
        const CivFarm *farm=h->farms+p;if (!farm->active) continue;
        int64_t dx=(int64_t)territory->cx-farm->cx;
        if (territory->cz==farm->cz && (dx==0 || (!qlcg_world_flat(owner) && period>0 && dx%period==0))) {
            if (slot) *slot=p;
            return farm;
        }
    }
    return NULL;
}
static int farm_type_at_territory(const QlcgCivTerritory *territory) {
    const CivFarm *farm=farm_at_territory(territory,NULL);
    const QlcgFarmlandType *type=farm?qlcg_farmland_type_find(farm->farm_type_id):NULL;
    const QlcgCropType *crop=type?qlcg_crop_type_find(farm->crop_id):NULL;
    if (!crop || strcmp(crop->farmland_id,farm->farm_type_id)) return 0;
    return type && type->chunk_type>=QLCG_CHUNK_FARM_FIRST && type->chunk_type<=QLCG_CHUNK_FARM_LAST?type->chunk_type:0;
}
bool qlcg_civ_territory_farm(const QlcgCivTerritory *territory,QlcgFarmPlot *out) {
    const QlcgLot *lot=qlcg_lot_local(territory->owner,territory->cx,territory->cz);
    if(lot) return qlcg_lot_farm(lot,territory,out);
    int slot=0;
    const CivFarm *farm=farm_at_territory(territory,&slot);
    if(!farm) return false;
    float yaw=qlcg_settlement_yaw(territory->faction);
    return farm_plot(territory->faction,farm,slot,yaw,cosf(yaw),sinf(yaw),out);
}
bool qlcg_civ_farm_at(const MacroWorld *w,int cx,int cz,QlcgFarmPlot *out) {
    QlcgCivTerritory territory;
    return out && w && w->opt.artificial_enabled && qlcg_civ_territory_at(w,cx,cz,&territory) &&
        qlcg_civ_territory_farm(&territory,out);
}
int qlcg_civ_farm_chunk_type(const MacroWorld *w,int cx,int cz) {
    QlcgCivTerritory territory;
    if (!w || !w->opt.artificial_enabled || !qlcg_civ_territory_at(w,cx,cz,&territory)) return 0;
    const QlcgLot *lot=qlcg_lot_local(territory.owner,territory.cx,territory.cz);
    if(lot) return lot->type>=9010?lot->type:0;
    return farm_type_at_territory(&territory);
}
int qlcg_civ_chunk_type(const MacroWorld *w,int cx,int cz,int natural_type,int farm_type) {
    if (!w || !w->opt.artificial_enabled) return natural_type;
    if (farm_type>=QLCG_CHUNK_FARM_FIRST && farm_type<=QLCG_CHUNK_FARM_LAST) return farm_type;
    QlcgCivTerritory territory;
    if (!qlcg_civ_territory_at(w,cx,cz,&territory) || !territory.owner->opt.artificial_enabled) return natural_type;
    const QlcgLot *lot=qlcg_lot_local(territory.owner,territory.cx,territory.cz);
    if(lot) return lot->type==LOT_LAND && territory.type==2?9001:lot->type;
    if (farm_type<0) {
        int type=farm_type_at_territory(&territory);if (type) return type;
    }
    int claim=territory.type;
    return claim==2?9001:claim==1?9000:natural_type;
}
uint32_t qlcg_civ_farms_revision(const MacroWorld *w) {return w && w->civ_economy?w->civ_economy->farm_revision:0;}
const char *qlcg_civ_economy_block_name(int block) {
    static const char *keys[]={
        [CIV_BLOCK_NONE] = "civ.block.ready",
        [CIV_BLOCK_FLAG] = "civ.block.noFlag",
        [CIV_BLOCK_FOOD] = "civ.block.food",
        [CIV_BLOCK_WOOD] = "civ.block.wood",
        [CIV_BLOCK_FIBER] = "civ.block.fiber",
        [CIV_BLOCK_POPULATION] = "civ.block.population",
        [CIV_BLOCK_LAND_LIMIT] = "civ.block.expansion",
        [CIV_BLOCK_LEVEL_LIMIT] = "civ.block.maximum",
    };
    return tr(block >= 0 && block < (int)(sizeof(keys) / sizeof(*keys)) ? keys[block] : "civ.block.unknown");
}

static void fields_household(QlCodec *io,CivHousehold *h,uint32_t version) {
    qlcg_io_bytes(io,h->uid,17);
    h->initialized=qlcg_io_i32(io,h->initialized);h->population=qlcg_io_i32(io,h->population);
    h->food=qlcg_io_i32(io,h->food);h->wood=qlcg_io_i32(io,h->wood);h->fiber=qlcg_io_i32(io,h->fiber);
    h->last_day=qlcg_io_i32(io,h->last_day);h->starvation=qlcg_io_i32(io,h->starvation);
    h->growth_days=qlcg_io_i32(io,h->growth_days);h->work_days=qlcg_io_i32(io,h->work_days);h->expanded=qlcg_io_i32(io,h->expanded);
    h->harvests=qlcg_io_i32(io,h->harvests);
    h->cycle_days=qlcg_io_i32(io,h->cycle_days);
    for (int p=0;p<QLCG_CIV_FARMS;p++) {
        CivFarm *farm=h->farms+p;
        farm->active=qlcg_io_i32(io,farm->active);farm->cx=qlcg_io_i32(io,farm->cx);farm->cz=qlcg_io_i32(io,farm->cz);
        farm->age=qlcg_io_i32(io,farm->age);farm->last_harvest=qlcg_io_i32(io,farm->last_harvest);
        farm->stage=qlcg_io_i32(io,farm->stage);farm->season=qlcg_io_i32(io,farm->season);
        if (version==1) {
            int crop=qlcg_io_i32(io,0);
            if (crop<0 || crop>1 || (farm->active && crop!=p)) io->ok=false;
            strcpy(farm->farm_type_id,"qlcg.legacy.field");
            strcpy(farm->crop_id,p?"qlcg.legacy.ma":"qlcg.legacy.su");
        } else {
            qlcg_io_string(io,farm->farm_type_id,QLCG_CIV_TYPE_ID);
            qlcg_io_string(io,farm->crop_id,QLCG_CIV_TYPE_ID);
        }
    }
}
static bool type_id_valid(const char *id) {
    size_t length=0;
    for (;length<QLCG_CIV_TYPE_ID && id[length];length++)
        if ((unsigned char)id[length]<33 || (unsigned char)id[length]>126) return false;
    return length>0 && length<QLCG_CIV_TYPE_ID;
}
static bool valid_household(const CivHousehold *h,uint32_t version) {
    if (h->uid[16] || h->initialized!=1 || h->population<1 || h->population>QLCG_CIV_POP_MAX ||
        h->food<0 || h->food>QLCG_CIV_RESOURCE_MAX || h->wood<0 || h->wood>QLCG_CIV_RESOURCE_MAX ||
        h->fiber<0 || h->fiber>QLCG_CIV_RESOURCE_MAX || h->starvation<0 || h->starvation>1000000 ||
        h->last_day==INT_MIN || h->last_day>INT_MAX-2440588 ||
        h->growth_days<0 || h->growth_days>30 || h->work_days<0 || h->work_days>1000000 || h->expanded<0 || h->expanded>1000000 ||
        h->harvests<0 || h->harvests>1000000 || h->cycle_days<1 || h->cycle_days>3650 ||
        (version==1 && h->cycle_days!=90 && h->cycle_days!=365)) return false;
    for (int p=0;p<QLCG_CIV_FARMS;p++) {
        const CivFarm *farm=h->farms+p;
        if (farm->active<0 || farm->active>1 || farm->age<(version==1?0:-1) || farm->age>(version==1?90:3650) ||
            farm->stage<0 || farm->stage>PD_STATE_WITHERED || farm->season<0 || farm->season>3 ||
            (farm->age<0 && farm->stage!=PD_STATE_PLANTED) ||
            (farm->active && (farm->last_harvest>h->last_day || !type_id_valid(farm->farm_type_id) || !type_id_valid(farm->crop_id)))) return false;
    }
    return true;
}
bool qlcg_civ_economy_write(FILE *file,const MacroWorld *w) {
    uint32_t count=0;for (int f=0;w && f<w->faction_count && f<MAX_FACTIONS;f++) if (household(w,w->factions+f)) count++;
    QlCodec measure=qlcg_codec(NULL,false,QLCG_FILE_LIMIT);qlcg_io_uint(&measure,count,4);
    for (int f=0;w && f<w->faction_count && f<MAX_FACTIONS;f++) {
        CivHousehold *h=household(w,w->factions+f);if (!h) continue;
        CivHousehold copy=*h;qlcg_io_uint(&measure,f,4);fields_household(&measure,&copy,2);
    }
    size_t size=(size_t)measure.bytes;uint8_t *buf=malloc(size?size:1);if (!buf) return false;
    QlCodec io=qlcg_codec_mem(buf,false,size);qlcg_io_uint(&io,count,4);
    for (int f=0;w && f<w->faction_count && f<MAX_FACTIONS;f++) {
        CivHousehold *h=household(w,w->factions+f);if (!h) continue;
        CivHousehold copy=*h;qlcg_io_uint(&io,f,4);fields_household(&io,&copy,2);
    }
    bool ok=io.ok && qlcg_section_write_buf(file,"ECON",2,buf,size);free(buf);return ok;
}
bool qlcg_civ_economy_read_mem_ver(const void *data,size_t size,MacroWorld *w,uint32_t version) {
    if (version<1 || version>2) return false;
    QlCodec io=qlcg_codec_mem((void *)data,true,size);uint32_t count=(uint32_t)qlcg_io_uint(&io,0,4);
    if (!io.ok || count>MAX_FACTIONS) return false;
    struct CivEconomy *made=calloc(1,sizeof(*made));if (!made) return false;
    int previous=-1;
    for (uint32_t n=0;n<count && io.ok;n++) {
        uint32_t id=(uint32_t)qlcg_io_uint(&io,0,4);
        if (id>=MAX_FACTIONS || (int)id<=previous) {io.ok=false;break;}
        previous=(int)id;CivHousehold *h=&made->factions[id];fields_household(&io,h,version);
        if (!valid_household(h,version) || (w && (id>=(uint32_t)w->faction_count || memcmp(h->uid,w->factions[id].uid,17) ||
            h->last_day>w->last_civ_day))) io.ok=false;
        if (io.ok && w) {
            const TerritoryFaction *f=w->factions+id;float yaw=qlcg_settlement_yaw(f);
            int c=(int)lroundf(cosf(yaw)),s=(int)lroundf(sinf(yaw));
            for (int p=0;p<QLCG_CIV_FARMS;p++) if (h->farms[p].active) {
                int dx=p?0:1,dz=p?1:0;
                if ((int64_t)h->farms[p].cx!=(int64_t)f->cx+dx*c-dz*s ||
                    (int64_t)h->farms[p].cz!=(int64_t)f->cz+dx*s+dz*c) io.ok=false;
            }
        }
    }
    bool ok=io.ok && io.bytes==size;
    if (ok && w) {
        made->farm_revision=qlcg_civ_farms_revision(w)+1;made->revision=w->civ_economy?w->civ_economy->revision+1:1;
        qlcg_civ_economy_free(w);w->civ_economy=made;made=NULL;farm_appearance_changed(w);
        if (w->last_civ_day!=INT_MIN) prepare(w,w->last_civ_day);
    }
    free(made);return ok;
}
bool qlcg_civ_economy_read_mem(const void *data,size_t size,MacroWorld *w) {
    return qlcg_civ_economy_read_mem_ver(data,size,w,2);
}
