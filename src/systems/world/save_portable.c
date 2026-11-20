#include "world_lot.h"
#include "hydrology_river.h"
#include "save_portable.h"
#include "surface_internal.h"
#include "world_ice_save.h"
#include "world_precip_save.h"
#include "astronomy_events.h"
#include "hydrology_archive.h"
#include "world_infinite.h"
#include "hydrology_sources.h"
#include "world_civ_economy.h"
#include "world_claims.h"
#include "faction_naming.h"
#include "world_delta.h"
#include "event_state.h"
#include "../dolls/doll_life_save.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static bool options_valid(const WorldCreatorOptions *o) {
    WorldCreatorOptions dims=*o;
    qlcg_sync_creator_dimensions(&dims);
    return o->columns==dims.columns && o->chunks_per_cell==dims.chunks_per_cell &&
        o->equator_chunks==dims.equator_chunks && o->map_mode>=WORLD_MAP_PLANET && o->map_mode<=WORLD_MAP_INFINITE &&
        o->macro_size_idx>=0 && o->macro_size_idx<=2 && o->macro_scale_idx>=0 && o->macro_scale_idx<=6 &&
        o->play_mode>=PLAY_MODE_LEISURE && o->play_mode<=PLAY_MODE_SURVIVAL &&
        (!o->astronomy_enabled || o->astronomy_version==1) && o->columns>=64 && o->columns<=1024 &&
        o->chunks_per_cell>0 && o->chunks_per_cell<=1250;
}
long qlcg_portable_header_size_ver(uint32_t version) {
    SaveSlotInfo s={0}; QlCodec io=qlcg_codec(NULL,false,QLCG_FILE_LIMIT);
    qlcg_fields_slot_ver(&io,&s,version);
    return (long)io.bytes+QLCG_SECTION_HEADER;
}
long qlcg_portable_header_size(void) {
    return qlcg_portable_header_size_ver(QLCG_SAVE_VERSION);
}
bool qlcg_portable_header_write(FILE *file, const SaveSlotInfo *slot) {
    long body_sz=qlcg_portable_header_size()-QLCG_SECTION_HEADER;
    if (body_sz<=0) return false;
    uint8_t *buf=malloc((size_t)body_sz);
    if (!buf) return false;
    SaveSlotInfo copy=*slot; QlCodec io=qlcg_codec_mem(buf,false,(uint64_t)body_sz);
    qlcg_fields_slot(&io,&copy);
    bool ok=io.ok && io.bytes==(uint64_t)body_sz &&
        qlcg_section_write_buf(file,"QSV3",QLCG_SAVE_VERSION-12u,buf,(size_t)body_sz);
    free(buf);
    return ok;
}
bool qlcg_portable_header_read(FILE *file, SaveSlotInfo *slot, long limit) {
    long end; uint32_t ver=0; uint8_t *buf=NULL; size_t sz=0;
    if (!qlcg_section_open_alloc(file,"QSV3",limit,QLCG_SAVE_VERSION-12u,&ver,&buf,&sz,&end)) return false;
    memset(slot,0,sizeof(*slot)); QlCodec io=qlcg_codec_mem(buf,true,(uint64_t)sz);
    qlcg_fields_slot_ver(&io,slot,ver>=2u?12u+ver:14u);
    bool ok=io.ok && io.bytes==(uint64_t)sz && options_valid(&slot->opt) && slot->delta_count>=0 && slot->delta_count<=MAX_MACRO_DELTAS;
    free(buf);
    if (!ok) return false;
    slot->valid=true; slot->save_version=QLCG_SAVE_VERSION;
    slot->disk_version=12u+ver;
    qlcg_normalize_atlas_display(&slot->atlas_display);
    return true;
}
static bool deltas_write(FILE *file, const MacroWorld *world) {
    uint32_t n=world?(uint32_t)world->delta_count:0;
    if (n>MAX_MACRO_DELTAS) return false;
    QlCodec measure=qlcg_codec(NULL,false,QLCG_FILE_LIMIT);
    qlcg_io_uint(&measure,n,4);
    for (uint32_t i=0;i<n && measure.ok;i++) {
        SparseDeltaRecord d=world->deltas[i];
        qlcg_fields_delta(&measure,&d);
        qlcg_io_bool(&measure,world->delta_origin[i]!=QLCG_DELTA_LOCAL);
    }
    if (!measure.ok) return false;
    size_t sz=(size_t)measure.bytes;
    uint8_t *buf=malloc(sz?sz:1); if (!buf) return false;
    QlCodec io=qlcg_codec_mem(buf,false,sz);
    qlcg_io_uint(&io,n,4);
    for (uint32_t i=0;i<n && io.ok;i++) {
        SparseDeltaRecord d=world->deltas[i];
        qlcg_fields_delta(&io,&d);
        qlcg_io_bool(&io,world->delta_origin[i]!=QLCG_DELTA_LOCAL);
    }
    bool ok=io.ok && io.bytes==sz && qlcg_section_write_buf(file,"DELT",2,buf,sz);
    free(buf);
    return ok;
}
static bool deltas_read_mem(const uint8_t *buf, size_t sz, MacroWorld *world, int expected,
                            uint32_t section_version) {
    QlCodec io=qlcg_codec_mem((void *)buf,true,(uint64_t)sz);
    uint32_t n=(uint32_t)qlcg_io_uint(&io,0,4);
    if (!io.ok || n>MAX_MACRO_DELTAS || (expected>=0 && n!=(uint32_t)expected)) return false;
    SparseDeltaRecord *records=world && n?malloc((size_t)n*sizeof(*records)):NULL;
    uint8_t origins[MAX_MACRO_DELTAS];
    if (world && n && !records) return false;
    for (uint32_t i=0;i<n && io.ok;i++) {
        SparseDeltaRecord d={0};
        qlcg_fields_delta(&io,&d);
        bool macro=section_version>=2?qlcg_io_bool(&io,false):qlcg_eng_legacy_delta_origin(&d);
        if (macro && (d.type<=ENG_TOOL_NONE || d.type>=ENG_TOOL_COUNT || d.radius<=0 ||
                      (d.type==ENG_TOOL_RIVER_DIVERT && d.width_m<=0))) io.ok=false;
        if (records) { records[i]=d; origins[i]=macro?QLCG_DELTA_MACRO:QLCG_DELTA_LOCAL; }
    }
    bool ok=io.ok && io.bytes==sz;
    if (world && ok) ok=qlcg_eng_install_saved_deltas(world,records,origins,(int)n);
    free(records);
    return ok;
}
static bool factions_write(FILE *file, const MacroWorld *world) {
    uint32_t cells=world?(uint32_t)world->cell_count:0;
    uint32_t count=world?(uint32_t)world->faction_count:0;
    int day=world?world->last_civ_day:0;
    if (!world || count>MAX_FACTIONS || !cells || cells>1024u*1024u) return false;
    QlCodec measure=qlcg_codec(NULL,false,QLCG_FILE_LIMIT);
    qlcg_io_uint(&measure,cells,4); qlcg_io_uint(&measure,count,4); qlcg_io_i32(&measure,day);
    for (uint32_t i=0;i<count && measure.ok;i++) {
        TerritoryFaction f=world->factions[i];
        qlcg_fields_faction(&measure,&f);
        qlcg_io_string(&measure,f.leader_name,sizeof(f.leader_name));
        if (f.id!=(int)i || f.flag_cell<0 || (uint32_t)f.flag_cell>=cells) measure.ok=false;
    }
    if (!measure.ok) return false;
    if(world->chunk_claims) {
        size_t size;
        const QlcgChunkClaim *claims=qlcg_claims_list(world,&size);
        size_t bytes=(size_t)measure.bytes+5+size*11;
        uint8_t *buffer=malloc(bytes);
        if(!buffer) return false;
        QlCodec io=qlcg_codec_mem(buffer,false,bytes);
        qlcg_io_uint(&io,cells,4);qlcg_io_uint(&io,count,4);qlcg_io_i32(&io,day);
        for(uint32_t i=0;i<count;i++) {
            TerritoryFaction f=world->factions[i];
            qlcg_fields_faction(&io,&f);
            qlcg_io_string(&io,f.leader_name,sizeof(f.leader_name));
        }
        qlcg_io_uint(&io,2,1);qlcg_io_uint(&io,size,4);
        for(size_t i=0;i<size;i++) {
            const QlcgChunkClaim *claim=claims+i;
            qlcg_io_i32(&io,claim->cx);qlcg_io_i32(&io,claim->cz);
            qlcg_io_uint(&io,claim->faction,2);qlcg_io_uint(&io,claim->type,1);
        }
        bool ok=io.ok&&io.bytes==bytes&&qlcg_section_write_buf(file,"FACT",3,buffer,bytes);
        free(buffer);
        return ok;
    }
    size_t nondef=0;
    for (uint32_t i=0;i<cells;i++) if (world->claim_faction[i]!=-1 || world->claim_type[i]!=0) nondef++;
    uint8_t sparse=(nondef*7u < (size_t)cells*3u)?1u:0u;
    size_t body_sz=(size_t)measure.bytes+1u+(sparse?(4u+nondef*7u):((size_t)cells*3u));
    uint8_t *buf=malloc(body_sz); if (!buf) return false;
    QlCodec io=qlcg_codec_mem(buf,false,(uint64_t)body_sz);
    qlcg_io_uint(&io,cells,4); qlcg_io_uint(&io,count,4); qlcg_io_i32(&io,day);
    for (uint32_t i=0;i<count && io.ok;i++) {
        TerritoryFaction f=world->factions[i];
        qlcg_fields_faction(&io,&f);
        qlcg_io_string(&io,f.leader_name,sizeof(f.leader_name));
    }
    qlcg_io_uint(&io,sparse,1);
    if (sparse) {
        qlcg_io_uint(&io,(uint32_t)nondef,4);
        for (uint32_t i=0;i<cells && io.ok;i++) {
            if (world->claim_faction[i]==-1 && world->claim_type[i]==0) continue;
            qlcg_io_uint(&io,i,4);
            qlcg_io_uint(&io,(uint16_t)world->claim_faction[i],2);
            qlcg_io_uint(&io,world->claim_type[i],1);
        }
    } else {
        for (uint32_t i=0;i<cells && io.ok;i++) qlcg_io_uint(&io,(uint16_t)world->claim_faction[i],2);
        for (uint32_t i=0;i<cells && io.ok;i++) qlcg_io_uint(&io,world->claim_type[i],1);
    }
    bool ok=io.ok && io.bytes==body_sz && qlcg_section_write_buf(file,"FACT",3,buf,body_sz);
    free(buf);
    return ok;
}
static bool chunk_claims_read(QlCodec *io,size_t size,MacroWorld *world,const WorldCreatorOptions *dims,
                              const TerritoryFaction *factions,uint32_t count) {
    uint32_t entries=(uint32_t)qlcg_io_uint(io,0,4);
    if(!io->ok||entries>QLCG_MAX_CHUNK_CLAIMS||size-io->bytes!=(uint64_t)entries*11) return false;
    MacroWorld *target=world?world:calloc(1,sizeof(*target));
    if(!target) return false;
    if(!world) {target->opt=*dims;target->equator_chunks=dims->equator_chunks;}
    bool ok=qlcg_claims_reserve(target,entries);
    if(ok) qlcg_claims_clear(target);
    bool flags[MAX_FACTIONS]={0};
    for(uint32_t i=0;i<entries&&ok;i++) {
        int x=qlcg_io_i32(io,0),z=qlcg_io_i32(io,0);
        uint32_t id=(uint32_t)qlcg_io_uint(io,0,2),type=(uint32_t)qlcg_io_uint(io,0,1);
        if(!io->ok||id>=count||!factions[id].active) {ok=false;break;}
        int canonical=x;
        if(!qlcg_claims_canonical(target,&canonical,z)||canonical!=x) {ok=false;break;}
        if(type==QLCG_FLAG_CHUNK) {
            int fx=factions[id].cx;
            if(flags[id]||!qlcg_claims_canonical(target,&fx,factions[id].cz)||fx!=x||factions[id].cz!=z) {
                ok=false;break;
            }
            flags[id]=true;
        }
        ok=qlcg_claims_add(target,x,z,id,type);
    }
    for(uint32_t i=0;i<count&&ok;i++) if(factions[i].active&&!flags[i]) ok=false;
    if(!world) {qlcg_claims_free(target);free(target);}
    return ok&&io->ok&&io->bytes==size;
}

static bool factions_read_mem(const uint8_t *buf, size_t sz, MacroWorld *world, const WorldCreatorOptions *opt,
                              uint32_t save_ver,uint32_t section_version,bool *legacy,
                              char owners[MAX_FACTIONS][17]) {
    QlCodec io=qlcg_codec_mem((void *)buf,true,(uint64_t)sz);
    uint32_t cells=(uint32_t)qlcg_io_uint(&io,0,4);
    uint32_t count=(uint32_t)qlcg_io_uint(&io,0,4);
    int day=qlcg_io_i32(&io,0);
    WorldCreatorOptions dims=*opt; qlcg_sync_creator_dimensions(&dims);
    uint32_t expected=(uint32_t)dims.columns*dims.columns/(dims.map_mode==WORLD_MAP_INFINITE?1:2);
    if (!io.ok || count>MAX_FACTIONS || !cells || cells>1024u*1024u || cells!=expected ||
        (world && cells!=(uint32_t)world->cell_count)) return false;
    TerritoryFaction decoded[MAX_FACTIONS];
    for (uint32_t i=0;i<count && io.ok;i++) {
        TerritoryFaction f={0};
        qlcg_fields_faction(&io,&f);
        if (section_version>=3) qlcg_io_string(&io,f.leader_name,sizeof(f.leader_name));
        else if (world && io.ok) qlcg_faction_name_leader(&f);
        if (f.id!=(int)i || f.flag_cell<0 || (uint32_t)f.flag_cell>=cells) io.ok=false;
        if (world && io.ok) {
            qlcg_faction_migrate_colors(&f);
            world->factions[i]=f;
        }
        decoded[i]=f;
        if (f.active) memcpy(owners[i],f.uid,sizeof(f.uid));
    }
    if (!io.ok) return false;
    if (save_ver>=14u) {
        uint8_t mode=(uint8_t)qlcg_io_uint(&io,0,1);
        if (!io.ok || mode>2u || (section_version<3 && (mode==2u)!=(section_version==2u))) return false;
        if(mode==2u) {
            *legacy=false;
            if(!chunk_claims_read(&io,sz,world,&dims,decoded,count)) return false;
            if(world) {
                for(uint32_t i=0;i<cells;i++) {world->claim_faction[i]=-1;world->claim_type[i]=0;}
                for(uint32_t i=0;i<count;i++) if(decoded[i].active) {
                    int cell=decoded[i].flag_cell;
                    world->claim_faction[cell]=(int16_t)i;world->claim_type[cell]=QLCG_FLAG_CHUNK;
                }
                world->faction_count=(int)count;world->last_civ_day=day;world->territory_revision++;
            }
            return true;
        }
        if (mode==1u) {
            uint32_t nondef=(uint32_t)qlcg_io_uint(&io,0,4);
            if (!io.ok || nondef>cells || (uint64_t)sz-io.bytes!=(uint64_t)nondef*7u) return false;
            if (world) {
                for (uint32_t i=0;i<cells;i++) { world->claim_faction[i]=-1; world->claim_type[i]=0; }
            }
            uint32_t prev=0;
            for (uint32_t k=0;k<nondef && io.ok;k++) {
                uint32_t cell=(uint32_t)qlcg_io_uint(&io,0,4);
                uint16_t bits=(uint16_t)qlcg_io_uint(&io,0,2);
                uint8_t type=(uint8_t)qlcg_io_uint(&io,0,1);
                int faction=bits<=INT16_MAX?(int)bits:-1-(int)(UINT16_MAX-bits);
                if (cell>=cells || (k && cell<=prev) || faction< -1 || faction>=(int)count) { io.ok=false; break; }
                prev=cell;
                if (world) { world->claim_faction[cell]=(int16_t)faction; world->claim_type[cell]=type; }
            }
            if (world && io.ok) { world->faction_count=(int)count; world->last_civ_day=day; world->territory_revision++; }
            return io.ok && io.bytes==sz;
        }
    }
    for (uint32_t i=0;i<cells && io.ok;i++) {
        uint16_t bits=(uint16_t)qlcg_io_uint(&io,0,2);
        int faction=bits<=INT16_MAX?(int)bits:-1-(int)(UINT16_MAX-bits);
        if (faction< -1 || faction>=(int)count) io.ok=false;
        if (world && io.ok) world->claim_faction[i]=(int16_t)faction;
    }
    for (uint32_t i=0;i<cells && io.ok;i++) {
        uint8_t type=(uint8_t)qlcg_io_uint(&io,0,1);
        if (world && io.ok) world->claim_type[i]=type;
    }
    if (world && io.ok) { world->faction_count=(int)count; world->last_civ_day=day; world->territory_revision++; }
    return io.ok && io.bytes==sz;
}
static bool exploration_write(FILE *file, const MacroWorld *world) {
    uint64_t size=world?world->explored_count:0;
    if (size>UINT32_MAX || size>(uint64_t)QLCG_FILE_LIMIT/8u) return false;
    size_t sz=4u+(size_t)size*8u;
    uint8_t *buf=malloc(sz); if (!buf) return false;
    QlCodec io=qlcg_codec_mem(buf,false,(uint64_t)sz);
    qlcg_io_uint(&io,size,4);
    for (uint32_t i=0;i<(uint32_t)size && io.ok;i++) qlcg_io_uint(&io,world->explored_chunks[i],8);
    bool ok=io.ok && io.bytes==sz && qlcg_section_write_buf(file,"EXPL",1,buf,sz);
    free(buf);
    return ok;
}
static bool exploration_read_mem(const uint8_t *buf, size_t sz, MacroWorld *world, const WorldCreatorOptions *opt) {
    QlCodec io=qlcg_codec_mem((void *)buf,true,(uint64_t)sz);
    uint32_t count=(uint32_t)qlcg_io_uint(&io,0,4);
    if (!io.ok || (uint64_t)count>(uint64_t)(sz-4u)/8u) return false;
    uint64_t max=(uint64_t)opt->equator_chunks*(opt->equator_chunks/2), prev=0;
    uint64_t *keys=world && count?malloc((size_t)count*8u):NULL;
    if (world && count && !keys) return false;
    for (uint32_t i=0;i<count && io.ok;i++) {
        uint64_t key=qlcg_io_uint(&io,0,8);
        if ((i && key<=prev) || (opt->map_mode!=WORLD_MAP_INFINITE && key>=max)) io.ok=false;
        if (keys) keys[i]=key;
        prev=key;
    }
    if (!io.ok || io.bytes!=sz) { free(keys); return false; }
    if (world) {
        free(world->explored_chunks); world->explored_chunks=keys;
        world->explored_count=world->explored_capacity=count; keys=NULL;
    }
    free(keys);
    return true;
}
enum { INVENTORY_V18_SLOTS = 12, INVENTORY_V19_BYTES = 561 };
_Static_assert(sizeof(Inventory) == INVENTORY_V19_BYTES, "v19 inventory layout");
_Static_assert(INV_SLOT_TOOL_1 + 1 == INVENTORY_V18_SLOTS, "v18 inventory slots");

static bool inventory_read(FILE *file, long limit, uint32_t version, MacroWorld *world) {
    Inventory inv = {0};
    long end;
    if (version >= 19u) {
        if (!qlcg_section_open(file, "INV2", limit, &end) ||
            end - ftell(file) != INVENTORY_V19_BYTES ||
            fread(&inv, sizeof(inv), 1, file) != 1) return false;
    } else {
        uint8_t grid[INV_MAX_H][INV_MAX_W];
        if (!qlcg_section_open(file, "INV1", limit, &end) ||
            end - ftell(file) != (long)(INVENTORY_V18_SLOTS + sizeof(grid)) ||
            fread(inv.equipped, INVENTORY_V18_SLOTS, 1, file) != 1 ||
            fread(grid, sizeof(grid), 1, file) != 1) return false;
        if (inv.equipped[INV_SLOT_TOOL_1] && inv.equipped[INV_SLOT_LEGS] != INV_ITEM_TOOL_BELT)
            return false;
        if (inv.equipped[INV_SLOT_PACK]) {
            memcpy(inv.pack, grid, sizeof(grid));
        } else {
            for (int y = 0; y < INV_MAX_H; y++) {
                for (int x = 0; x < INV_MAX_W; x++) {
                    if (y < INV_POCKET_H && x < INV_POCKET_W) inv.pocket[y][x] = grid[y][x];
                    else if (grid[y][x]) return false;
                }
            }
        }
    }
    if (!inventory_validate(&inv)) return false;
    if (world) {
        inventory_recover_legacy_hook(&inv);
        world->inventory = inv;
    }
    return true;
}

static bool bundle_write(FILE *file, const MacroWorld *world, bool planes);
static bool bundle_read(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, int deltas, long limit, bool planes, uint32_t save_ver);

static bool planes_write(FILE *file, const MacroWorld *world) {
    uint32_t count=world->planes?(uint32_t)(world->planes->count-1):0;
    QlCodec io=qlcg_codec(file,false,QLCG_FILE_LIMIT);
    if (count>4096) return false;
    qlcg_io_uint(&io,count,4);
    if (world->planes) for (size_t i=0;i<world->planes->count;i++) {
        const MacroWorld *part=world->planes->items[i]; if (part==world) continue;
        long begin=qlcg_section_begin(file,"PLNE");
        WorldCreatorOptions copy=part->opt;
        QlCodec fields=qlcg_codec(file,false,QLCG_FILE_LIMIT);
        qlcg_fields_options(&fields,&copy);
        if (begin<0 || !fields.ok || !bundle_write(file,part,false) || !qlcg_section_finish(file,begin)) return false;
    }
    return io.ok;
}
static bool planes_read(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, long limit, uint32_t save_ver) {
    QlCodec io=qlcg_codec(file,true,(uint64_t)(limit-ftell(file)));
    uint32_t count=(uint32_t)qlcg_io_uint(&io,0,4);
    if (!io.ok || count>4096 || (count && opt->map_mode!=WORLD_MAP_INFINITE)) return false;
    int32_t coords[4096][2];
    for (uint32_t i=0;i<count;i++) {
        long end; if (!qlcg_section_open(file,"PLNE",limit,&end)) return false;
        WorldCreatorOptions part={0}; QlCodec fields=qlcg_codec(file,true,(uint64_t)(end-ftell(file)));
        qlcg_fields_options(&fields,&part);
        if (!fields.ok || !options_valid(&part) || part.map_mode!=WORLD_MAP_INFINITE || part.world_seed!=opt->world_seed ||
            part.macro_scale_idx!=opt->macro_scale_idx || part.seed!=qlcg_infinite_seed(part.world_seed,part.plane_x,part.plane_z) ||
            (part.plane_x==opt->plane_x && part.plane_z==opt->plane_z)) return false;
        for (uint32_t j=0;j<i;j++) if (coords[j][0]==part.plane_x && coords[j][1]==part.plane_z) return false;
        coords[i][0]=part.plane_x; coords[i][1]=part.plane_z;
        long body=ftell(file);
        if (!world) { if (!bundle_read(file,NULL,&part,-1,end,false,save_ver)) return false; continue; }
        MacroWorld archive={0}; archive.opt=part; archive.columns=part.columns;
        if (!qlcg_portable_rivers(file,&archive,end) || fseek(file,body,SEEK_SET)) { qlcg_river_archive_free(&archive); return false; }
        part.play_mode=world->opt.play_mode;
        memcpy(part.world_name,world->opt.world_name,sizeof(part.world_name));
        MacroWorld *made=qlcg_world_generate_with_rivers(&part,archive.river_archive);
        if (!made) return false;
        bool ok=bundle_read(file,made,&part,-1,end,false,save_ver);
        if (ok) { qlcg_world_rebuild_atlas_fields(made); ok=qlcg_infinite_attach(world,made); }
        if (!ok) { qlcg_world_free(made); return false; }
    }
    return true;
}
static const char tags[8][5]={"DELT","FACT","EXPL","ICED","ASTR","RIVR","PLAN","RAIN"};
static bool bundle_write(FILE *file, const MacroWorld *world, bool planes) {
    for (int k=0;k<8;k++) {
        if (k==6 && !planes) continue;
        bool ok=false;
        if (k==0) ok=deltas_write(file,world);
        else if (k==1) ok=factions_write(file,world);
        else if (k==2) ok=exploration_write(file,world);
        else if (k==5) {
            uint8_t *buf=NULL; size_t sz=0;
            ok=qlcg_river_archive_encode_compact(world,&buf,&sz) && qlcg_section_write_buf(file,"RIVR",1,buf,sz);
            free(buf);
        } else if (k==7) {
            uint8_t *buf=NULL; size_t sz=0;
            ok=qlcg_precip_encode_compact(world,&buf,&sz) && qlcg_section_write_buf(file,"RAIN",1,buf,sz);
            free(buf);
        } else {
            long start=qlcg_section_begin(file,tags[k]);
            if (k==3) ok=qlcg_ice_write(file,world);
            else if (k==4) ok=qlcg_astra_events_write(file,world);
            else ok=planes_write(file,world);
            ok=start>=0 && ok && qlcg_section_finish(file,start);
        }
        if (!ok) return false;
    }
    if(!qlcg_civ_economy_write(file,world) || !qlcg_chd_write(file,world)) return false;
    if(planes) {
        const Inventory *inv=&world->inventory;
        if(!inventory_validate(inv) || !qlcg_section_write_buf(file,"INV2",1,inv,sizeof(*inv))) return false;
    }
    return qlcg_doll_life_write(file,world) && qlcg_lot_write(file,world) && qlcg_event_state_write(file,world);
}
static bool bundle_read(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, int deltas, long limit, bool planes, uint32_t save_ver) {
    bool legacy_claims=true;
    char owners[MAX_FACTIONS][17]={{0}};
    for (int k=0;k<8;k++) {
        if (k==6 && !planes) continue;
        long end=0; bool ok=false;
        if (k==0 || k==1 || k==2 || k==5 || k==7) {
            uint8_t *buf=NULL; size_t sz=0;uint32_t version=0;
            if (!qlcg_section_open_alloc(file,tags[k],limit,k==1?3:k==0?2:1,&version,&buf,&sz,&end)) return false;
            if (k==0) ok=version==(save_ver>=20u?2u:1u) && deltas_read_mem(buf,sz,world,deltas,version);
            else if (k==1) ok=factions_read_mem(buf,sz,world,opt,save_ver,version,&legacy_claims,owners);
            else if (k==2) ok=exploration_read_mem(buf,sz,world,opt);
            else if (k==5) {
                if (world && !world->river_archive_preloaded) { qlcg_source_rivers_free(world); qlcg_river_surface_free(world); }
                ok=qlcg_river_archive_read_mem(buf,sz,world && !world->river_archive_preloaded?world:NULL,opt);
                if (world) world->river_archive_preloaded=false;
            } else {
                ok=qlcg_precip_read_strict_mem(buf,sz,world,opt);
            }
            free(buf);
            if (!ok || ftell(file)!=end) return false;
        } else {
            if (!qlcg_section_open(file,tags[k],limit,&end)) return false;
            if (k==3) { ok=world?qlcg_ice_read_optional_lim(file,world,end):qlcg_ice_tail_valid_lim(file,end); if(ok) fseek(file,end,SEEK_SET); }
            else if (k==4) ok=qlcg_astra_events_read(file,world,end);
            else ok=planes_read(file,world,opt,end,save_ver);
            if (!ok || ftell(file)!=end) return false;
        }
    }
    if (save_ver>=16u) {
        uint8_t *buf=NULL;size_t sz=0;long end=0;uint32_t version=0;
        if (!qlcg_section_open_alloc(file,"ECON",limit,save_ver>=17u?2:1,&version,&buf,&sz,&end)) return false;
        bool ok=qlcg_civ_economy_read_mem_ver(buf,sz,world,version);free(buf);
        if (!ok || ftell(file)!=end) return false;
    } else if (world) {
        qlcg_civ_economy_free(world);
    }
    if(save_ver>=18u) {
        if(!qlcg_chd_read(file,limit,world)) return false;
        if(planes && !inventory_read(file,limit,save_ver,world)) return false;
    }
    if (save_ver>=21u) {
        if (!qlcg_doll_life_read(file,limit,world,owners)) return false;
    } else if (world) qlcg_doll_life_free(world);
    bool has_lots=false;
    long at=ftell(file);
    if(save_ver>=22u && limit-at>=4) {
        char tag[4];
        if(fread(tag,1,4,file)!=4 || fseek(file,at,SEEK_SET)) return false;
        has_lots=!memcmp(tag,"LOT1",4);
    }
    if(has_lots) {if(!qlcg_lot_read(file,limit,world)) return false;}
    else if(world) qlcg_lot_free(world);
    if(world) qlcg_eng_restore_macro_deltas(world);
    if(save_ver>=20u) {
        int cells=opt->columns*(opt->map_mode==WORLD_MAP_INFINITE?opt->columns:opt->columns/2);
        if(!qlcg_event_state_read(file,limit,world,cells)) return false;
    }
    if(ftell(file)!=limit) return false;
    if(world && save_ver<18u) qlcg_chd_clear(world);
    if(world&&legacy_claims&&!qlcg_claims_migrate(world)) return false;
    if(world&&save_ver<16u) qlcg_civ_economy_update(world,world->last_civ_day,world->last_civ_day,false);
    return true;
}
bool qlcg_portable_body_write(FILE *file, const MacroWorld *world) { return bundle_write(file,world,true); }
bool qlcg_portable_body_read_ver(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, int deltas, long limit, uint32_t save_version) {
    return bundle_read(file,world,opt,deltas,limit,true,save_version);
}
bool qlcg_portable_body_read(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, int deltas, long limit) {
    return bundle_read(file,world,opt,deltas,limit,true,QLCG_SAVE_VERSION);
}
bool qlcg_portable_rivers(FILE *file, MacroWorld *world, long limit) {
    for (int k=0;k<=5;k++) {
        long end;
        if (k<5) {
            if (!qlcg_section_open_ver(file,tags[k],limit,k==1?3:k==0?2:1,NULL,&end) || fseek(file,end,SEEK_SET)) return false;
        } else {
            uint8_t *buf=NULL; size_t sz=0;
            if (!qlcg_section_open_alloc(file,tags[k],limit,1,NULL,&buf,&sz,&end)) return false;
            bool ok=qlcg_river_archive_read_mem(buf,sz,world,world?&world->opt:NULL) && ftell(file)==end;
            free(buf);
            return ok;
        }
    }
    return false;
}
