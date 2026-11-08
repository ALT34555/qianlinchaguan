#include "save_portable.h"
#include "world_ice_save.h"
#include "world_precip_save.h"
#include "astronomy_events.h"
#include "river_archive.h"
#include "world_infinite.h"
#include "source_rivers.h"
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
long qlcg_portable_header_size(void) {
    SaveSlotInfo s={0}; QlCodec io=qlcg_codec(NULL,false,QLCG_FILE_LIMIT);
    qlcg_fields_slot(&io,&s);
    return (long)io.bytes+QLCG_SECTION_HEADER;
}
bool qlcg_portable_header_write(FILE *file, const SaveSlotInfo *slot) {
    long start=qlcg_section_begin(file,"QSV3");
    SaveSlotInfo copy=*slot; QlCodec io=qlcg_codec(file,false,QLCG_FILE_LIMIT);
    qlcg_fields_slot(&io,&copy);
    return start>=0 && io.ok && qlcg_section_finish(file,start);
}
bool qlcg_portable_header_read(FILE *file, SaveSlotInfo *slot, long limit) {
    long end;
    if (!qlcg_section_open(file,"QSV3",limit,&end)) return false;
    memset(slot,0,sizeof(*slot)); QlCodec io=qlcg_codec(file,true,(uint64_t)(end-ftell(file)));
    qlcg_fields_slot(&io,slot);
    if (!io.ok || ftell(file)!=end || !options_valid(&slot->opt) || slot->delta_count<0 || slot->delta_count>MAX_MACRO_DELTAS) return false;
    slot->valid=true; slot->save_version=slot->disk_version=QLCG_SAVE_VERSION;
    return true;
}
static bool deltas_io(FILE *file, MacroWorld *world, bool reading, int expected, long end) {
    QlCodec io=qlcg_codec(file,reading,(uint64_t)(end-ftell(file)));
    uint32_t n=(uint32_t)qlcg_io_uint(&io,world?(uint32_t)world->delta_count:0,4);
    if (n>MAX_MACRO_DELTAS || (reading && expected>=0 && n!=(uint32_t)expected)) return false;
    for (uint32_t i=0;i<n && io.ok;i++) {
        SparseDeltaRecord d={0}; if (!reading) d=world->deltas[i];
        qlcg_fields_delta(&io,&d);
        if (reading && world) world->deltas[i]=d;
    }
    if (reading && world && io.ok) { world->delta_count=(int)n; world->delta_version++; }
    return io.ok;
}
static bool factions_io(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, bool reading, long end) {
    QlCodec io=qlcg_codec(file,reading,(uint64_t)(end-ftell(file)));
    uint32_t cells=(uint32_t)qlcg_io_uint(&io,world?(uint32_t)world->cell_count:0,4);
    uint32_t count=(uint32_t)qlcg_io_uint(&io,world?(uint32_t)world->faction_count:0,4);
    int day=qlcg_io_i32(&io,world?world->last_civ_day:0);
    WorldCreatorOptions dims=*opt; qlcg_sync_creator_dimensions(&dims);
    uint32_t expected=(uint32_t)dims.columns*dims.columns/(dims.map_mode==WORLD_MAP_INFINITE?1:2);
    if (count>MAX_FACTIONS || !cells || cells>1024u*1024u || (reading && cells!=expected) ||
        (world && cells!=(uint32_t)world->cell_count)) return false;
    for (uint32_t i=0;i<count && io.ok;i++) {
        TerritoryFaction f={0}; if (!reading) f=world->factions[i];
        qlcg_fields_faction(&io,&f);
        if (f.id!=(int)i || f.flag_cell<0 || (uint32_t)f.flag_cell>=cells) io.ok=false;
        if (reading && world) world->factions[i]=f;
    }
    for (uint32_t i=0;i<cells && io.ok;i++) {
        uint16_t bits=(uint16_t)qlcg_io_uint(&io,world?(uint16_t)world->claim_faction[i]:0,2);
        int faction=bits<=INT16_MAX?(int)bits:-1-(int)(UINT16_MAX-bits);
        if (reading && (faction< -1 || faction>=(int)count)) io.ok=false;
        if (reading && world) world->claim_faction[i]=(int16_t)faction;
    }
    for (uint32_t i=0;i<cells && io.ok;i++) {
        uint8_t type=(uint8_t)qlcg_io_uint(&io,world?world->claim_type[i]:0,1);
        if (reading && world) world->claim_type[i]=type;
    }
    if (reading && world && io.ok) { world->faction_count=(int)count; world->last_civ_day=day; world->territory_revision++; }
    return io.ok;
}
static bool exploration_io(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, bool reading, long end) {
    QlCodec io=qlcg_codec(file,reading,(uint64_t)(end-ftell(file)));
    uint64_t size=world?world->explored_count:0;
    if (size>UINT32_MAX) return false;
    uint32_t count=(uint32_t)qlcg_io_uint(&io,size,4);
    if (count>(uint64_t)(end-ftell(file))/8) return false;
    uint64_t max=(uint64_t)opt->equator_chunks*(opt->equator_chunks/2),prev=0;
    uint64_t *keys=reading && world && count?malloc((size_t)count*8):NULL;
    if (reading && world && count && !keys) return false;
    for (uint32_t i=0;i<count && io.ok;i++) {
        uint64_t key=qlcg_io_uint(&io,!reading?world->explored_chunks[i]:0,8);
        if ((i && key<=prev) || (opt->map_mode!=WORLD_MAP_INFINITE && key>=max)) io.ok=false;
        if (keys) keys[i]=key;
        prev=key;
    }
    if (reading && world && io.ok) {
        free(world->explored_chunks); world->explored_chunks=keys;
        world->explored_count=world->explored_capacity=count; keys=NULL;
    }
    free(keys); return io.ok;
}
static bool bundle_write(FILE *file, const MacroWorld *world, bool planes);
static bool bundle_read(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, int deltas, long limit, bool planes);

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
static bool planes_read(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, long limit) {
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
        if (!world) { if (!bundle_read(file,NULL,&part,-1,end,false)) return false; continue; }
        MacroWorld archive={0}; archive.opt=part; archive.columns=part.columns;
        if (!qlcg_portable_rivers(file,&archive,end) || fseek(file,body,SEEK_SET)) { qlcg_river_archive_free(&archive); return false; }
        part.play_mode=world->opt.play_mode;
        memcpy(part.world_name,world->opt.world_name,sizeof(part.world_name));
        MacroWorld *made=qlcg_world_generate_with_rivers(&part,archive.river_archive);
        if (!made) return false;
        bool ok=bundle_read(file,made,&part,-1,end,false);
        if (ok) { qlcg_world_rebuild_atlas_fields(made); ok=qlcg_infinite_attach(world,made); }
        if (!ok) { qlcg_world_free(made); return false; }
    }
    return true;
}
static const char tags[8][5]={"DELT","FACT","EXPL","ICED","ASTR","RIVR","PLAN","RAIN"};
static bool bundle_write(FILE *file, const MacroWorld *world, bool planes) {
    for (int k=0;k<8;k++) {
        if (k==6 && !planes) continue;
        long start=qlcg_section_begin(file,tags[k]); bool ok=false;
        switch (k) {
            case 0: ok=deltas_io(file,(MacroWorld *)world,false,-1,QLCG_FILE_LIMIT); break;
            case 1: ok=factions_io(file,(MacroWorld *)world,&world->opt,false,QLCG_FILE_LIMIT); break;
            case 2: ok=exploration_io(file,(MacroWorld *)world,&world->opt,false,QLCG_FILE_LIMIT); break;
            case 3: ok=qlcg_ice_write(file,world); break;
            case 4: ok=qlcg_astra_events_write(file,world); break;
            case 5: ok=qlcg_river_archive_write(file,world); break;
            case 6: ok=planes_write(file,world); break;
            case 7: ok=qlcg_precip_write(file,world); break;
        }
        if (start<0 || !ok || !qlcg_section_finish(file,start)) return false;
    }
    return true;
}
static bool bundle_read(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, int deltas, long limit, bool planes) {
    for (int k=0;k<8;k++) {
        if (k==6 && !planes) continue;
        long end; if (!qlcg_section_open(file,tags[k],limit,&end)) return false;
        bool ok=false;
        switch (k) {
            case 0: ok=deltas_io(file,world,true,deltas,end); break;
            case 1: ok=factions_io(file,world,opt,true,end); break;
            case 2: ok=exploration_io(file,world,opt,true,end); break;
            case 3: ok=world?qlcg_ice_read_optional_lim(file,world,end):qlcg_ice_tail_valid_lim(file,end); if(ok) fseek(file,end,SEEK_SET); break;
            case 4: ok=qlcg_astra_events_read(file,world,end); break;
            case 5:
                if (world && !world->river_archive_preloaded) { qlcg_source_rivers_free(world); qlcg_river_surface_free(world); }
                ok=qlcg_river_archive_read(file,world && !world->river_archive_preloaded?world:NULL,end);
                if (world) world->river_archive_preloaded=false;
                break;
            case 6: ok=planes_read(file,world,opt,end); break;
            case 7: ok=qlcg_precip_read_strict(file,world,opt,end); break;
        }
        if (!ok || ftell(file)!=end) return false;
    }
    return ftell(file)==limit;
}
bool qlcg_portable_body_write(FILE *file, const MacroWorld *world) { return bundle_write(file,world,true); }
bool qlcg_portable_body_read(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, int deltas, long limit) {
    return bundle_read(file,world,opt,deltas,limit,true);
}
bool qlcg_portable_rivers(FILE *file, MacroWorld *world, long limit) {
    for (int k=0;k<=5;k++) {
        long end; if (!qlcg_section_open(file,tags[k],limit,&end)) return false;
        if (k==5) return qlcg_river_archive_read(file,world,end) && ftell(file)==end;
        if (fseek(file,end,SEEK_SET)) return false;
    }
    return false;
}
