#include "doll_life_save.h"
#include "core/binary.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int64_t minute_field(QlCodec *io,int64_t value) {
    uint64_t raw=qlcg_io_uint(io,(uint64_t)value,8);
    int64_t minute=raw<=INT64_MAX?(int64_t)raw:-1-(int64_t)(UINT64_MAX-raw);
    if (minute<(int64_t)INT32_MIN*1440 || minute>(int64_t)INT32_MAX*1440) io->ok=false;
    return minute;
}

static void resident_fields(QlCodec *io,QlcgDollLifeResident *r) {
    r->id=(uint32_t)qlcg_io_uint(io,r->id,4);
    r->partner_id=(uint32_t)qlcg_io_uint(io,r->partner_id,4);
    r->x=qlcg_io_f32(io,r->x);
    r->z=qlcg_io_f32(io,r->z);
    r->yaw=qlcg_io_f32(io,r->yaw);
    r->until_minute=minute_field(io,r->until_minute);
    r->meal_day=qlcg_io_i32(io,r->meal_day);
    r->tea_day=qlcg_io_i32(io,r->tea_day);
    r->satiety=(uint16_t)qlcg_io_uint(io,r->satiety,2);
    r->energy=(uint16_t)qlcg_io_uint(io,r->energy,2);
    r->social=(uint16_t)qlcg_io_uint(io,r->social,2);
    r->closeness=(uint16_t)qlcg_io_uint(io,r->closeness,2);
    r->slot=(uint8_t)qlcg_io_uint(io,r->slot,1);
    r->role=(uint8_t)qlcg_io_uint(io,r->role,1);
    r->home=(uint8_t)qlcg_io_uint(io,r->home,1);
    r->task=(uint8_t)qlcg_io_uint(io,r->task,1);
    r->resume_task=(uint8_t)qlcg_io_uint(io,r->resume_task,1);
    r->reason=(uint8_t)qlcg_io_uint(io,r->reason,1);
    r->target=(uint8_t)qlcg_io_uint(io,r->target,1);
    r->routine_state=(uint8_t)qlcg_io_uint(io,r->routine_state,1);
    r->moving=qlcg_io_bool(io,r->moving!=0);
    r->indoor=qlcg_io_bool(io,r->indoor!=0);
    r->work_plot=(uint8_t)qlcg_io_uint(io,r->work_plot,1);
}

static bool resident_exists(const QlcgDollLifeCamp *camp,uint32_t id,int count) {
    for (int i=0;i<count;i++) if (camp->residents[i].id==id) return true;
    return false;
}

static bool residents_valid(const QlcgDollLifeCamp *c) {
    for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) {
        const QlcgDollLifeResident *r=c->residents+i;
        if (!r->id || r->slot!=i || r->role>DOLL_ROLE_PATROL || r->home>=QLCG_SETTLEMENT_PARTS ||
            r->task>=DOLL_LIFE_TASK_COUNT || r->resume_task>=DOLL_LIFE_TASK_COUNT ||
            r->reason>=DOLL_LIFE_REASON_COUNT || r->target>=DOLL_LIFE_TARGET_COUNT ||
            r->routine_state>DOLL_STATE_PATROL || r->satiety>QLCG_LIFE_NEED_MAX ||
            r->energy>QLCG_LIFE_NEED_MAX || r->social>QLCG_LIFE_NEED_MAX ||
            r->closeness>QLCG_LIFE_NEED_MAX || r->work_plot>=QLCG_CIV_FARMS ||
            resident_exists(c,r->id,i) || r->id==r->partner_id) return false;
        if ((r->target==DOLL_LIFE_CHAIR)!=(r->id==c->chair_owner)) return false;
        if (r->target==DOLL_LIFE_CHAIR && r->task!=DOLL_LIFE_TEA) return false;
        if (r->partner_id && !resident_exists(c,r->partner_id,QLCG_LIFE_RESIDENTS)) return false;
    }
    return (!c->chair_owner || resident_exists(c,c->chair_owner,QLCG_LIFE_RESIDENTS)) &&
           (!c->guard_owner || resident_exists(c,c->guard_owner,QLCG_LIFE_RESIDENTS));
}

static void camp_fields(QlCodec *io,QlcgDollLifeCamp *c) {
    c->faction_id=qlcg_io_i32(io,c->faction_id);
    qlcg_io_string(io,c->faction_uid,sizeof(c->faction_uid));
    c->last_minute=minute_field(io,c->last_minute);
    c->tier=(uint8_t)qlcg_io_uint(io,c->tier,1);
    c->chair_owner=(uint32_t)qlcg_io_uint(io,c->chair_owner,4);
    c->guard_owner=(uint32_t)qlcg_io_uint(io,c->guard_owner,4);
    c->guard_day=qlcg_io_i32(io,c->guard_day);
    c->chair_yaw=qlcg_io_f32(io,c->chair_yaw);
    c->chair_x=qlcg_io_f32(io,c->chair_x);
    c->chair_z=qlcg_io_f32(io,c->chair_z);
    c->revision=(uint32_t)qlcg_io_uint(io,c->revision,4);
    c->meals=(uint32_t)qlcg_io_uint(io,c->meals,4);
    c->teas=(uint32_t)qlcg_io_uint(io,c->teas,4);
    c->shifts=(uint32_t)qlcg_io_uint(io,c->shifts,4);
    c->harvests=(uint32_t)qlcg_io_uint(io,c->harvests,4);
    if (c->faction_id<0 || c->faction_id>=MAX_FACTIONS || !c->faction_uid[0] || c->tier>DOLL_LIFE_DORMANT) io->ok=false;
    for (int i=0;i<QLCG_CIV_FARMS;i++) {
        QlcgDollLifeHarvest *crop=c->crops+i;
        crop->day=qlcg_io_i32(io,crop->day);
        crop->food=qlcg_io_i32(io,crop->food);
        crop->wood=qlcg_io_i32(io,crop->wood);
        crop->fiber=qlcg_io_i32(io,crop->fiber);
        crop->pending=qlcg_io_bool(io,crop->pending);
        if (crop->food<0 || crop->wood<0 || crop->fiber<0 ||
            crop->food>QLCG_CIV_RESOURCE_MAX || crop->wood>QLCG_CIV_RESOURCE_MAX ||
            crop->fiber>QLCG_CIV_RESOURCE_MAX) io->ok=false;
    }
}

static bool named_owner(const QlcgDollLifeCamp *camp,uint32_t id) {
    if (id) for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) {
        const QlcgDollLifeResident *r=camp->residents+i;
        if (r->id==id) return r->name[0]!=0;
    }
    return false;
}

static void named_fields(QlCodec *io,QlcgDollLifeResident *resident) {
    resident_fields(io,resident);
    qlcg_io_string(io,resident->name,sizeof(resident->name));
    qlcg_io_bytes(io,&resident->inventory,sizeof(resident->inventory));
    if (!resident->name[0] || !inventory_validate(&resident->inventory)) io->ok=false;
    if (io->reading && io->ok) inventory_recover_legacy_hook(&resident->inventory);
}

static void named_write(QlCodec *io,QlcgDollLifeCamp *c) {
    unsigned count=0;
    for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) if (c->residents[i].name[0]) count++;
    qlcg_io_uint(io,count,1);
    for (int i=0;i<QLCG_LIFE_RESIDENTS && io->ok;i++) {
        QlcgDollLifeResident *r=c->residents+i;
        if (r->name[0]) named_fields(io,r);
    }
}

static void named_read(QlCodec *io,QlcgDollLifeCamp *c,MacroWorld *w) {
    TerritoryFaction identity={0};
    memcpy(identity.uid,c->faction_uid,sizeof(identity.uid));
    if (w && c->faction_id<w->faction_count) {
        qlcg_doll_life_seed_residents(w,w->factions+c->faction_id,c);
    } else {
        for (int i=0;i<QLCG_LIFE_RESIDENTS;i++) {
            c->residents[i].id=qlcg_doll_resident_id(&identity,i);
            c->residents[i].slot=(uint8_t)i;
        }
    }
    unsigned count=(unsigned)qlcg_io_uint(io,0,1);
    if (count>QLCG_LIFE_RESIDENTS) { io->ok=false; return; }
    int previous=-1;
    for (unsigned i=0;i<count && io->ok;i++) {
        QlcgDollLifeResident r={0};
        named_fields(io,&r);
        if (!io->ok || r.slot>=QLCG_LIFE_RESIDENTS || r.slot<=previous ||
            r.id!=qlcg_doll_resident_id(&identity,r.slot)) { io->ok=false; break; }
        c->residents[r.slot]=r;
        previous=r.slot;
    }
}

bool qlcg_doll_life_write(FILE *file,const MacroWorld *w) {
    unsigned count=0;
    for (int i=0;i<w->faction_count;i++) if (qlcg_doll_life_camp(w,w->factions+i)) count++;
    long begin=qlcg_section_begin(file,"DLL2");
    if (begin<0) return false;
    QlCodec io=qlcg_codec(file,false,QLCG_FILE_LIMIT);
    qlcg_io_uint(&io,count,2);
    for (int i=0;i<w->faction_count && io.ok;i++) {
        const QlcgDollLifeCamp *camp=qlcg_doll_life_camp(w,w->factions+i);
        if (camp) {
            if (!residents_valid(camp)) return false;
            QlcgDollLifeCamp copy=*camp;
            if (!named_owner(camp,copy.chair_owner)) copy.chair_owner=0;
            if (!named_owner(camp,copy.guard_owner)) copy.guard_owner=0;
            camp_fields(&io,&copy);
            named_write(&io,&copy);
        }
    }
    return io.ok && qlcg_section_finish(file,begin);
}

bool qlcg_doll_life_read(FILE *file,long limit,MacroWorld *w,const char owners[MAX_FACTIONS][17]) {
    long at=ftell(file),end;
    char tag[4];
    if (at<0 || fread(tag,1,sizeof(tag),file)!=sizeof(tag) || fseek(file,at,SEEK_SET)) return false;
    bool legacy=!memcmp(tag,"DLL1",4);
    if (!qlcg_section_open(file,legacy?"DLL1":"DLL2",limit,&end)) return false;
    QlCodec io=qlcg_codec(file,true,(uint64_t)(end-ftell(file)));
    unsigned count=(unsigned)qlcg_io_uint(&io,0,2);
    if (!io.ok || count>MAX_FACTIONS) return false;
    MacroWorld *staging=NULL;
    if (count && w) {
        staging=calloc(1,sizeof(*staging));
        if (!staging) return false;
        staging->opt=w->opt;
        staging->civ_economy=w->civ_economy;
        staging->faction_count=w->faction_count;
        memcpy(staging->factions,w->factions,(size_t)w->faction_count*sizeof(w->factions[0]));
    }
    int previous=-1;
    for (unsigned i=0;i<count && io.ok;i++) {
        QlcgDollLifeCamp c={0};
        camp_fields(&io,&c);
        if (!io.ok || previous>=c.faction_id ||
            (owners && strcmp(c.faction_uid,owners[c.faction_id]))) { io.ok=false; break; }
        if (legacy) {
            for (int j=0;j<QLCG_LIFE_RESIDENTS && io.ok;j++) resident_fields(&io,c.residents+j);
            if (w && c.faction_id<w->faction_count && !w->factions[c.faction_id].is_player)
                memcpy(c.residents[0].name,w->factions[c.faction_id].leader_name,sizeof(c.residents[0].name));
        } else named_read(&io,&c,w);
        if (!residents_valid(&c) || (staging && io.ok && !qlcg_doll_life_attach(staging,&c))) io.ok=false;
        previous=c.faction_id;
    }
    bool ok=io.ok && ftell(file)==end;
    if (ok && w) {
        qlcg_doll_life_free(w);
        if (staging) {
            w->doll_life=staging->doll_life;
            staging->doll_life=NULL;
        }
    }
    if (staging) { qlcg_doll_life_free(staging); free(staging); }
    return ok;
}
