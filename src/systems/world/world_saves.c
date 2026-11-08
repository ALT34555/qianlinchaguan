#include "save_portable.h"
/* 五槽容器与版本检查 */
#include "planet_macro.h"
#include "world_internal.h"
#include "world_faction_save.h"
#include "world_ice_save.h"
#include "world_precip_save.h"
#include "river_archive.h"
#include "astronomy_events.h"
#include "surface_rivers.h"
#include "source_rivers.h"
#include "save_legacy_v5.h"
#include "save_legacy_v6.h"
#include "save_legacy_v7.h"
#include "save_legacy_v9.h"
#include "save_legacy_v11.h"
#include "world_infinite.h"
#include "../../ui/ui_input.h"
#include "../../core/math_util.h"
#include "../../core/i18n.h"
#include "platform/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>

_Static_assert(sizeof(SaveContainerHeader) == 112, "container header");
_Static_assert(offsetof(WorldCreatorOptions, world_name) == 0, "options name first");
_Static_assert(offsetof(SaveSlotInfo, world_name) == offsetof(SaveSlotInfoV8, world_name),
               "v8/v9 slot name offset");
_Static_assert(sizeof(AtlasDisplayStateV8) == sizeof(AtlasDisplayState), "v8 atlas layout");

/* UTF-8 路径转宽字符后走 _wfopen */
static FILE *fopen_u8(const char *path, const char *mode) { return qlcg_platform_fopen(path, mode); }

static int remove_u8(const char *path) { return qlcg_platform_remove(path); }

static int rename_u8(const char *from, const char *to) { return qlcg_platform_replace(from, to); }

/* 文件字节数，位置随后由调用者定位 */
static long file_size_of(FILE *fp) { return qlcg_file_size(fp); }

/* 有效边界：limit>0 取槽尾，否则文件末 */
static long io_end(FILE *fp, long limit) {
    if (limit > 0) return limit;
    if (!fp || fseek(fp, 0, SEEK_END) != 0) return -1;
    return ftell(fp);
}

/* 文件名禁用字符替换 */
static void sanitize_id_part(char *s) {
    for (char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || strchr("<>:\"/\\|?*", (int)c)) *p = '_';
    }
}

/* save_{名}_{尾}，世界名按 UTF-8 截断 */
static void build_save_id(char *id, size_t cap, const char *name, const char *tail) {
    size_t fixed = 5 + 1 + strlen(tail);
    size_t room = cap > fixed + 1 ? cap - fixed - 1 : 0;
    char safe[256];
    if (!name || !*name) name = "world";
    ui_u8_copy(safe, room + 1 < sizeof(safe) ? room + 1 : sizeof(safe), name);
    sanitize_id_part(safe);
    snprintf(id, cap, "save_%s_%s", safe, tail);
}

/* 新文件名占用判定 */
static bool save_id_exists(const char *id) {
    char path[1024];
    int n = snprintf(path, sizeof(path), "userdata/saves/%s.sav", id);
    return n < 0 || n >= sizeof(path) || qlcg_platform_exists(path);
}

/* 新建 id：同秒冲突追加序号 */
static void make_file_id(char *id, size_t cap, const char *name, uint32_t seed) {
    QlLocalTime st;
    qlcg_platform_local_time(&st);
    char suffix[40];
    snprintf(suffix, sizeof(suffix), "%02d%02d%02d_%u", st.hour, st.minute, st.second, seed);
    char base[64];
    build_save_id(base, sizeof(base), name, suffix);
    ui_u8_copy(id, cap < sizeof(base) ? cap : sizeof(base), base);
    for (int n = 2; n < 100 && save_id_exists(id); n++)
        snprintf(id, cap, "%.48s_%d", base, n);
}

/* 磁盘头布局版本对应的字节数 */
static long header_size_of(uint32_t version) {
    if (version == QLCG_SAVE_VERSION) return qlcg_portable_header_size();
    if (version == 11u) return (long)sizeof(SaveSlotInfoV11);
    if (version == 5u) return (long)sizeof(SaveSlotInfoV5);
    if (version == 6u) return (long)sizeof(SaveSlotInfoV6);
    if (version == 7u) return (long)sizeof(SaveSlotInfoV7);
    if (version == 8u) return (long)sizeof(SaveSlotInfoV8);
    if (version == 9u) return (long)sizeof(SaveSlotInfoV9);
    return (long)sizeof(SaveSlotInfo);
}

static uint32_t slot_disk_version(const SaveSlotInfo *slot) {
    /* 无 disk_version 的旧档按结构版本推断 */
    if (slot->save_version < 6u) return slot->save_version;
    return slot->disk_version ? slot->disk_version : slot->save_version;
}

/* 读取当前与星球旧档 */
static bool read_save_header_at(FILE *file, long base, SaveSlotInfo *slot) {
    if (!file || !slot || base < 0 || fseek(file, base, SEEK_SET) != 0) return false;
    char tag[4];
    if (fread(tag,1,4,file)!=4 || fseek(file,base,SEEK_SET)) return false;
    if (!memcmp(tag,"QSV3",4)) return qlcg_portable_header_read(file,slot,qlcg_file_size(file));
#ifndef _WIN32
    return false;
#else
    SaveSlotInfo cur={0};
    bool current=fread(&cur,sizeof(cur),1,file)==1 && cur.valid && cur.save_version==12u;
    if(!current) {
        SaveSlotInfoV11 old;
        if(fseek(file,base,SEEK_SET)!=0 || fread(&old,sizeof(old),1,file)!=1 || !old.valid || old.save_version!=11u) return false;
        memset(&cur,0,sizeof(cur));
        memcpy(&cur,&old,offsetof(SaveSlotInfo,opt));
        memcpy(&cur.opt,&old.opt,sizeof(old.opt));
        memcpy(&cur.atlas_display,&old.atlas_display,sizeof(old)-offsetof(SaveSlotInfoV11,atlas_display));
        cur.save_version=QLCG_SAVE_VERSION;
        cur.disk_version=11u;
    } else cur.disk_version=12u;
    if((cur.opt.astronomy_enabled && cur.opt.astronomy_version!=1u) ||
       cur.opt.map_mode<WORLD_MAP_PLANET || cur.opt.map_mode>WORLD_MAP_INFINITE ||
       cur.delta_count<0 || cur.delta_count>MAX_MACRO_DELTAS) return false;
    cur.save_version=QLCG_SAVE_VERSION;
    *slot = cur;
    return true;
#endif
}

static bool write_save_header(FILE *file, const SaveSlotInfo *slot) {
    return qlcg_portable_header_write(file,slot);
}

static long save_header_size(const SaveSlotInfo *slot) {
    return header_size_of(slot_disk_version(slot));
}

static const char explore_tag[4] = { 'E', 'X', 'P', '1' };

static bool read_exploration(FILE *file, const WorldCreatorOptions *opt, MacroWorld *w, long limit) {
    long start = ftell(file);
    if (start < 0) return false;
    char tag[sizeof(explore_tag)];
    if (fread(tag, 1, sizeof(tag), file) != sizeof(tag) ||
        memcmp(tag, explore_tag, sizeof(tag)) != 0)
        return !ferror(file) && fseek(file, start, SEEK_SET) == 0;

    uint32_t count;
    bool flat=opt->map_mode==WORLD_MAP_INFINITE;
    uint64_t limit_cells = flat?UINT64_MAX:(uint64_t)opt->equator_chunks * (uint64_t)(opt->equator_chunks / 2);
    if (fread(&count, sizeof(count), 1, file) != 1 || count > limit_cells)
        return false;
    long body = ftell(file);
    if (body < 0) return false;
    long end = io_end(file, limit);
    if (end < body || (uint64_t)(end - body) < (uint64_t)count * sizeof(uint64_t) ||
        fseek(file, body, SEEK_SET) != 0) return false;

    bool restore = w && w->opt.play_mode == PLAY_MODE_SURVIVAL;
    uint64_t *chunks = restore ? malloc((size_t)count * sizeof(*chunks)) : NULL;
    if (restore && count && !chunks) return false;
    uint64_t previous = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t key;
        if (fread(&key, sizeof(key), 1, file) != 1 || (!flat && key >= limit_cells) ||
            (i > 0 && key <= previous)) {
            free(chunks);
            return false;
        }
        if (chunks) chunks[i] = key;
        previous = key;
    }
    if (chunks) {
        free(w->explored_chunks);
        w->explored_chunks = chunks;
        w->explored_count = w->explored_capacity = count;
    }
    return true;
}


static bool read_plane(FILE *fp,MacroWorld *owner,long limit) {
    uint64_t bytes; WorldCreatorOptions opt; uint32_t n;
    if(limit-ftell(fp)<8 || fread(&bytes,8,1,fp)!=1) return false;
    long begin=ftell(fp);
    if(bytes<(uint64_t)sizeof(opt)+4 || bytes>(uint64_t)(limit-begin)) return false;
    long end=begin+(long)bytes;
    if(fread(&opt,sizeof(opt),1,fp)!=1 || fread(&n,4,1,fp)!=1 || n>MAX_MACRO_DELTAS ||
       opt.map_mode!=WORLD_MAP_INFINITE || opt.astronomy_version!=1u || opt.macro_scale_idx<0 || opt.macro_scale_idx>6 ||
       opt.seed!=qlcg_infinite_seed(opt.world_seed,opt.plane_x,opt.plane_z)) return false;
    if(owner && (opt.world_seed!=owner->opt.world_seed || opt.macro_scale_idx!=owner->opt.macro_scale_idx ||
       qlcg_infinite_find(owner,opt.plane_x,opt.plane_z))) return false;
    long deltas=ftell(fp); size_t delta_bytes=(size_t)n*sizeof(SparseDeltaRecord);
    if(delta_bytes>(size_t)(end-deltas) || fseek(fp,deltas+(long)delta_bytes,SEEK_SET)!=0) return false;
    long tail=ftell(fp);
    if(!qlcg_factions_tail_valid_lim(fp,end) || fseek(fp,tail,SEEK_SET)!=0) return false;
    long faction_end=qlcg_factions_tail_end_lim(fp,end);
    if(faction_end<0 || fseek(fp,faction_end,SEEK_SET)!=0 || !read_exploration(fp,&opt,NULL,end)) return false;
    long ice=ftell(fp),ice_end=qlcg_ice_tail_end_lim(fp,end);
    if(fseek(fp,ice,SEEK_SET)!=0 || !qlcg_ice_tail_valid_lim(fp,end) ||
       fseek(fp,ice_end>=0?ice_end:ice,SEEK_SET)!=0 || !qlcg_astra_events_read(fp,NULL,end)) return false;
    MacroWorld archive={0}; archive.opt=opt; qlcg_sync_creator_dimensions(&archive.opt); archive.columns=archive.opt.columns;
    if(!qlcg_river_archive_read(fp,owner?&archive:NULL,end)) return false;
    if(!owner) return qlcg_precip_tail_valid_lim(fp,end) && ftell(fp)==end;
    opt.play_mode=owner->opt.play_mode;
    ui_u8_copy(opt.world_name,sizeof(opt.world_name),owner->opt.world_name);
    MacroWorld *v=qlcg_world_generate_with_rivers(&opt,archive.river_archive);
    if(!v) return false;
    bool ok=fseek(fp,deltas,SEEK_SET)==0 && fread(v->deltas,sizeof(*v->deltas),n,fp)==n &&
        qlcg_factions_read_lim(fp,v,end) && read_exploration(fp,&archive.opt,v,end) &&
        qlcg_ice_read_optional_lim(fp,v,end) && qlcg_astra_events_read(fp,v,end) &&
        qlcg_river_archive_read(fp,NULL,end) && qlcg_precip_read_optional_lim(fp,v,end) && ftell(fp)==end;
    v->river_archive_preloaded=false; v->delta_count=(int)n; v->delta_version++;
    if(ok) { qlcg_world_rebuild_atlas_fields(v); ok=qlcg_infinite_attach(owner,v); }
    if(!ok) qlcg_world_free(v);
    return ok;
}

bool qlcg_infinite_read(FILE *fp,MacroWorld *w,long limit) {
    long start=ftell(fp); char tag[4]; uint32_t count;
    if(start<0) return false;
    if(limit<=0) { limit=io_end(fp,0); if(fseek(fp,start,SEEK_SET)!=0) return false; }
    if(limit-start<4 || fread(tag,1,4,fp)!=4 || memcmp(tag,"PLN1",4))
        return fseek(fp,start,SEEK_SET)==0 && (!w || !qlcg_world_flat(w));
    if((w && !qlcg_world_flat(w)) || limit-ftell(fp)<4 || fread(&count,4,1,fp)!=1 ||
       count>(uint64_t)(limit-ftell(fp))/(sizeof(WorldCreatorOptions)+12)) return false;
    for(uint32_t i=0;i<count;i++) if(!read_plane(fp,w,limit)) return false;
    return true;
}

/* 各类世界记录受槽尾约束 */
static bool save_tail_valid(FILE *file, const SaveSlotInfo *slot, long base, long end) {
    if (slot->disk_version==QLCG_SAVE_VERSION) {
        return fseek(file,base+qlcg_portable_header_size(),SEEK_SET)==0 &&
            qlcg_portable_body_read(file,NULL,&slot->opt,slot->delta_count,end);
    }
    if (slot->delta_count < 0 || slot->delta_count > MAX_MACRO_DELTAS) return false;
    int extra = slot->delta_count > 64 ? slot->delta_count - 64 : 0;
    long payload = save_header_size(slot) + (long)((size_t)extra * sizeof(SparseDeltaRecord));
    long limit = end > 0 ? end : io_end(file, 0);
    if (limit < 0 || base < 0 || limit < base + payload) return false;
    if (fseek(file, base + payload, SEEK_SET) != 0) return false;
    if (slot_disk_version(slot) >= 6u) {
        if (!qlcg_factions_tail_valid_lim(file, limit)) return false;
        long faction_end = qlcg_factions_tail_end_lim(file, limit);
        WorldCreatorOptions opt = slot->opt;
        qlcg_sync_creator_dimensions(&opt);
        if (faction_end < 0 || fseek(file, faction_end, SEEK_SET) != 0 ||
            !read_exploration(file, &opt, NULL, limit)) return false;
        long ice_off = ftell(file);
        if (ice_off < 0) return false;
        long ice_end = qlcg_ice_tail_end_lim(file, limit);
        if (ice_end < 0) {
            /* 旧档无 ICE1：允许直接接 RAIN1 或到槽尾 */
            if (fseek(file, ice_off, SEEK_SET) != 0) return false;
            if (!qlcg_ice_tail_valid_lim(file, limit)) return false;
            if (fseek(file, ice_off, SEEK_SET) != 0) return false;
            if (!qlcg_astra_events_read(file, NULL, limit)) return false;
            if (!qlcg_river_archive_read(file, NULL, limit)) return false;
            if (!qlcg_infinite_read(file,NULL,limit)) return false;
            if (!qlcg_precip_tail_valid_lim(file, limit)) return false;
        } else {
            if (fseek(file, ice_off, SEEK_SET) != 0) return false;
            if (!qlcg_ice_tail_valid_lim(file, limit)) return false;
            if (fseek(file, ice_end, SEEK_SET) != 0) return false;
            if (!qlcg_astra_events_read(file, NULL, limit)) return false;
            if (!qlcg_river_archive_read(file, NULL, limit)) return false;
            if (!qlcg_infinite_read(file,NULL,limit)) return false;
            if (!qlcg_precip_tail_valid_lim(file, limit)) return false;
        }
    }
    return fseek(file, base + save_header_size(slot), SEEK_SET) == 0;
}

/* 容器头读取：魔数与尺寸校验 */
enum { PORTABLE_DIRECTORY_BYTES=24+112+3*242 };
static bool container_fields(FILE *fp, SaveContainerHeader *h, bool reading) {
    QlCodec io=qlcg_codec(fp,reading,112);
    qlcg_io_bytes(&io,h->magic,4);
    h->version=(uint32_t)qlcg_io_uint(&io,h->version,4);
    h->slot_count=(uint32_t)qlcg_io_uint(&io,h->slot_count,4);
    h->header_bytes=(uint32_t)qlcg_io_uint(&io,h->header_bytes,4);
    for(int i=0;i<5;i++) h->offset[i]=qlcg_io_uint(&io,h->offset[i],8);
    for(int i=0;i<5;i++) h->size[i]=qlcg_io_uint(&io,h->size[i],8);
    qlcg_io_bytes(&io,h->present,5); qlcg_io_bytes(&io,h->kind,5); qlcg_io_bytes(&io,h->reserved,6);
    return io.ok;
}
static bool container_read_header(FILE *fp, SaveContainerHeader *h) {
    char tag[4]; long end;
    if(!fp || !h || fseek(fp,0,SEEK_SET) || fread(tag,1,4,fp)!=4 || fseek(fp,0,SEEK_SET)) return false;
    memset(h,0,sizeof(*h));
    if(!memcmp(tag,"PACK",4)) {
        if(!qlcg_section_open(fp,"PACK",qlcg_file_size(fp),&end) || end!=PORTABLE_DIRECTORY_BYTES || !container_fields(fp,h,true)) return false;
        return !memcmp(h->magic,"QLCG",4) && h->version==2 && h->slot_count==5 && h->header_bytes==PORTABLE_DIRECTORY_BYTES;
    }
#ifdef _WIN32
    if(fread(h,sizeof(*h),1,fp)!=1) return false;
    return !memcmp(h->magic,"QLCG",4) && h->version==1 && h->slot_count==5 && h->header_bytes==112;
#else
    return false;
#endif
}
static bool container_finish(FILE *fp, SaveContainerHeader *h) {
    long end=ftell(fp);
    if(end<PORTABLE_DIRECTORY_BYTES || fseek(fp,0,SEEK_SET) || qlcg_section_begin(fp,"PACK")!=0 || !container_fields(fp,h,false) ||
       fseek(fp,PORTABLE_DIRECTORY_BYTES,SEEK_SET) || !qlcg_section_finish(fp,0)) return false;
    return fseek(fp,end,SEEK_SET)==0;
}

/* 单槽边界与槽号校验 */
static bool container_slot_ok(const SaveContainerHeader *h, int i, long bytes) {
    if (!h->present[i] || h->kind[i] != (uint8_t)i) return false;
    if (h->offset[i] < h->header_bytes) return false;
    if (h->present[i]!=1 || h->size[i] < QLCG_SECTION_HEADER) return false;
    if (h->offset[i] > (uint64_t)bytes) return false;
    if (h->size[i] > (uint64_t)bytes - h->offset[i]) return false;
    for(int j=0;j<5;j++) if(j!=i && h->present[j]) {
        if(h->offset[j]>(uint64_t)bytes || h->size[j]>(uint64_t)bytes-h->offset[j]) return false;
        if(h->offset[j]<h->offset[i]+h->size[i] && h->offset[i]<h->offset[j]+h->size[j]) return false;
    }
    return true;
}

static const char slot_names_tag[] = "SNM1";
typedef char ManualSlotNames[SAVE_SLOT_MANUAL_3][QLCG_SAVE_SLOT_NAME_BYTES];

static bool container_read_names(FILE *fp, const SaveContainerHeader *h, long bytes,
                                 ManualSlotNames names) {
    if(h->version==2) {
        if(fseek(fp,24+112,SEEK_SET)) return false;
        QlCodec io=qlcg_codec(fp,true,3*242);
        for(int i=0;i<3;i++) qlcg_io_string(&io,names[i],240);
        return io.ok;
    }
    if (memcmp(h->reserved, slot_names_tag, 4) != 0) return true;
    long end = (long)sizeof(*h) + (long)sizeof(ManualSlotNames);
    if (bytes < end) return false;
    for (int i = 0; i < QLCG_SAVE_SLOT_COUNT; i++)
        if (container_slot_ok(h, i, bytes) && h->offset[i] < (uint64_t)end) return false;
    return fseek(fp, sizeof(*h), SEEK_SET) == 0 &&
           fread(names, sizeof(ManualSlotNames), 1, fp) == 1;
}

static bool container_write_start(FILE *fp, SaveContainerHeader *h, const SaveSlotMeta *slots) {
    memset(h, 0, sizeof(*h));
    memcpy(h->magic, QLCG_SAVE_PACK_MAGIC, 4);
    h->version = 2;
    h->slot_count = QLCG_SAVE_SLOT_COUNT;
    h->header_bytes = PORTABLE_DIRECTORY_BYTES;

    ManualSlotNames names = {0};
    for (int i = SAVE_SLOT_MANUAL_1; i <= SAVE_SLOT_MANUAL_3; i++) {
        if (!slots[i].name[0]) continue;
        ui_u8_copy(names[i - SAVE_SLOT_MANUAL_1], sizeof(names[0]), slots[i].name);
        memcpy(h->reserved, slot_names_tag, 4);
    }
    if(qlcg_section_begin(fp,"PACK")!=0 || !container_fields(fp,h,false)) return false;
    QlCodec io=qlcg_codec(fp,false,3*242);
    for(int i=0;i<3;i++) qlcg_io_string(&io,names[i],240);
    return io.ok;
}

static void default_manual_name(char *buf, size_t cap, const SaveSlotInfo *info) {
    const char *world_name = info->opt.world_name[0] ? info->opt.world_name : info->world_name;
    int hour, minute;
    if (sscanf(info->saved_at_str, "%*4d-%*2d-%*2d %2d:%2d", &hour, &minute) == 2 &&
        hour >= 0 && hour < 24 && minute >= 0 && minute < 60) {
        snprintf(buf, cap, "%s_%02d%02d", world_name, hour, minute);
    } else {
        ui_u8_copy(buf, cap, world_name);
    }
}

static void slot_meta_fill(SaveSlotMeta *m, const SaveSlotInfo *info,
                           int kind, long offset, long size) {
    memset(m, 0, sizeof(*m));
    m->present = true;
    m->kind = (uint8_t)kind;
    m->offset = offset;
    m->size = size;
    m->disk_version = slot_disk_version(info);
    m->year = info->year;
    m->month = info->month;
    m->day = info->day;
    ui_u8_copy(m->saved_at_str, sizeof(m->saved_at_str), info->saved_at_str);
    if (kind >= SAVE_SLOT_MANUAL_1 && kind <= SAVE_SLOT_MANUAL_3)
        default_manual_name(m->default_name, sizeof(m->default_name), info);
    m->unix_ms = info->unix_ms;
    m->delta_count = info->delta_count;
}

static bool slot_meta_newer(const SaveSlotMeta *a, const SaveSlotMeta *b) {
    int c = strcmp(a->saved_at_str, b->saved_at_str);
    if (c) return c > 0;
    return a->unix_ms > b->unix_ms;
}

static bool unsupported_catalog(SaveFileInfo *file, bool container, int slot) {
    file->valid = true; file->container = container; file->newest_slot = slot;
    file->present_count = 1; file->slots[slot].present = true;
    ui_u8_copy(file->world_name, sizeof(file->world_name), tr("saves.unsupported"));
    return true;
}

/* 读目录并标记不支持的版本 */
static bool catalog_load(FILE *fp, long bytes, SaveFileInfo *file) {
    memset(file, 0, sizeof(*file));
    SaveContainerHeader h;
    bool container = container_read_header(fp, &h);
    if(!container) {
        char tag[4]; if(fseek(fp,0,SEEK_SET) || fread(tag,1,4,fp)!=4 || !memcmp(tag,"PACK",4)) return false;
        if(memcmp(tag,"QSV3",4) && bytes<(long)sizeof(SaveSlotInfoV5)) return false;
    }
    int newest = -1;
    if (container) {
        ManualSlotNames names = {0};
        bool named = container_read_names(fp, &h, bytes, names);
        if(!named) return false;
        for (int i = 0; i < QLCG_SAVE_SLOT_COUNT; i++) {
            if (!h.present[i]) continue;
            if (!container_slot_ok(&h, i, bytes)) return false;
            SaveSlotInfo info;
            if (h.size[i] < QLCG_SECTION_HEADER || !read_save_header_at(fp, (long)h.offset[i], &info))
                return unsupported_catalog(file, true, i);
            if (info.delta_count < 0 || info.delta_count > MAX_MACRO_DELTAS) continue;
            slot_meta_fill(&file->slots[i], &info, i, (long)h.offset[i], (long)h.size[i]);
            if (named && i >= SAVE_SLOT_MANUAL_1 && i <= SAVE_SLOT_MANUAL_3) {
                const char *name = names[i - SAVE_SLOT_MANUAL_1];
                if (memchr(name, '\0', sizeof(names[0])))
                    ui_u8_copy(file->slots[i].name, sizeof(file->slots[i].name), name);
            }
            if (newest < 0 || slot_meta_newer(&file->slots[i], &file->slots[newest])) newest = i;
        }
        file->container = true;
    } else {
        if (bytes < QLCG_SECTION_HEADER) return false;
        SaveSlotInfo info;
        if (!read_save_header_at(fp, 0, &info)) return unsupported_catalog(file, false, SAVE_SLOT_MANUAL_1);
        if (info.delta_count < 0 || info.delta_count > MAX_MACRO_DELTAS) return false;
        slot_meta_fill(&file->slots[SAVE_SLOT_MANUAL_1], &info, SAVE_SLOT_MANUAL_1, 0, bytes);
        newest = SAVE_SLOT_MANUAL_1;
        file->container = false;
    }
    if (newest < 0) return false;
    file->newest_slot = newest;
    int present = 0;
    for (int i = 0; i < QLCG_SAVE_SLOT_COUNT; i++) if (file->slots[i].present) present++;
    file->present_count = present;

    SaveSlotInfo info;
    if (!read_save_header_at(fp, file->slots[newest].offset, &info)) return false;
    ui_u8_copy(file->world_name, sizeof(file->world_name), info.world_name);
    file->seed = info.seed;
    file->play_mode = info.play_mode;
    file->calendar_mode = info.calendar_mode;
    file->macro_size_idx = info.opt.macro_size_idx;
    file->chunks_per_cell = info.opt.chunks_per_cell;
    ui_u8_copy(file->saved_at_str, sizeof(file->saved_at_str), info.saved_at_str);
    file->unix_ms = info.unix_ms;
    file->valid = true;
    return true;
}

/* 存档按保存时间倒序，files[0] 即最新 */
static int file_saved_desc(const void *a, const void *b) {
    const SaveFileInfo *x = (const SaveFileInfo *)a;
    const SaveFileInfo *y = (const SaveFileInfo *)b;
    int c = strcmp(y->saved_at_str, x->saved_at_str);
    return c ? c : strcmp(y->id, x->id);
}

/* 扫描 userdata/saves 下的存档文件 */
int qlcg_saves_scan(SaveFileInfo *out_files, int max_files) {
    if (!out_files || max_files <= 0) return 0;
    QlDirectoryEntry e;
    QlDirectory *dir = qlcg_platform_dir_open("userdata/saves");
    int count = 0;
    while (count < max_files && qlcg_platform_dir_next(dir, &e)) {
        size_t len = strlen(e.name);
        if (e.directory || len <= 4 || qlcg_stricmp(e.name + len - 4, ".sav")) continue;
        char path[2048];
        snprintf(path, sizeof(path), "userdata/saves/%s", e.name);
        FILE *fp = fopen_u8(path, "rb");
        if (!fp) continue;
        long bytes = file_size_of(fp);
        SaveFileInfo info;
        if (bytes > 0 && catalog_load(fp, bytes, &info)) {
            ui_u8_copy(info.file_path, sizeof(info.file_path), path);
            char base[1024];
            snprintf(base, sizeof(base), "%s", e.name);
            size_t n = strlen(base);
            if (n > 4 && qlcg_stricmp(base + n - 4, ".sav") == 0) base[n - 4] = '\0';
            ui_u8_copy(info.id, sizeof(info.id), base);
            out_files[count++] = info;
        }
        fclose(fp);
    }
    qlcg_platform_dir_close(dir);
    /* 文件名序与时间无关，统一按保存时间倒序 */
    if (count > 1) qsort(out_files, (size_t)count, sizeof(SaveFileInfo), file_saved_desc);
    return count;
}

/* 读指定槽的完整头与文件内偏移 */
bool qlcg_saves_load_slot(const SaveFileInfo *file, int slot, SaveSlotInfo *out,
                          long *out_offset, long *out_size) {
    if (!file || !file->valid || !out) return false;
    if (slot < 0 || slot >= QLCG_SAVE_SLOT_COUNT) return false;
    if (!file->slots[slot].present || !qlcg_save_version_supported(file->slots[slot].disk_version) ||
        file->slots[slot].size < header_size_of(file->slots[slot].disk_version)) return false;
    SaveFileInfo refreshed;
    if(file->slots[slot].disk_version!=QLCG_SAVE_VERSION) {
        if(!qlcg_saves_migrate_file(file->file_path)) return false;
        FILE *scan=fopen_u8(file->file_path,"rb"); if(!scan) return false;
        bool read=catalog_load(scan,file_size_of(scan),&refreshed); fclose(scan);
        if(!read) return false;
        ui_u8_copy(refreshed.file_path,sizeof(refreshed.file_path),file->file_path);
        ui_u8_copy(refreshed.id,sizeof(refreshed.id),file->id); file=&refreshed;
    }
    FILE *fp = fopen_u8(file->file_path, "rb");
    if (!fp) return false;
    bool ok = read_save_header_at(fp, file->slots[slot].offset, out);
    if(ok && out->disk_version==QLCG_SAVE_VERSION) ok=save_tail_valid(fp,out,file->slots[slot].offset,file->slots[slot].offset+file->slots[slot].size);
    fclose(fp);
    if (!ok) return false;
    ui_u8_copy(out->file_path, sizeof(out->file_path), file->file_path);
    ui_u8_copy(out->id, sizeof(out->id), file->id);
    if (out_offset) *out_offset = file->slots[slot].offset;
    if (out_size) *out_size = file->slots[slot].size;
    return true;
}

/* 手动槽默认：空槽优先，满则最早 */
static int pick_manual_slot(const SaveFileInfo *file) {
    if (!file || !file->valid) return SAVE_SLOT_MANUAL_1;
    for (int i = SAVE_SLOT_MANUAL_1; i <= SAVE_SLOT_MANUAL_3; i++)
        if (!file->slots[i].present) return i;
    int best = SAVE_SLOT_MANUAL_1;
    for (int i = SAVE_SLOT_MANUAL_2; i <= SAVE_SLOT_MANUAL_3; i++)
        if (strcmp(file->slots[i].saved_at_str, file->slots[best].saved_at_str) < 0) best = i;
    return best;
}

/* 只写槽体尾段，头由调用方写 */
static bool write_slot_body(FILE *fp, const MacroWorld *w, const SaveSlotInfo *slot) {
    (void)slot;
    return qlcg_portable_body_write(fp,w);
}

/* 原样搬运槽块 */
static bool copy_bytes(FILE *in, long offset, long size, FILE *out) {
    if (!in || !out || offset < 0 || size <= 0) return false;
    if (fseek(in, offset, SEEK_SET) != 0) return false;
    char buf[65536];
    long left = size;
    while (left > 0) {
        size_t chunk = (size_t)left > sizeof(buf) ? sizeof(buf) : (size_t)left;
        if (fread(buf, 1, chunk, in) != chunk) return false;
        if (fwrite(buf, 1, chunk, out) != chunk) return false;
        left -= (long)chunk;
    }
    return true;
}

/* 写入后校验容器与目标槽尾段 */
static bool verify_container(const char *path, int target) {
    FILE *fp = fopen_u8(path, "rb");
    if (!fp) return false;
    long bytes = file_size_of(fp);
    SaveContainerHeader h;
    bool ok = bytes > 0 && container_read_header(fp, &h);
    ManualSlotNames names;
    if (ok) ok = container_read_names(fp, &h, bytes, names);
    int present = 0;
    for (int i = 0; ok && i < QLCG_SAVE_SLOT_COUNT; i++) {
        if (!h.present[i]) continue;
        if (!container_slot_ok(&h, i, bytes)) { ok = false; break; }
        SaveSlotInfo info;
        if (!read_save_header_at(fp, (long)h.offset[i], &info)) { ok = false; break; }
        present++;
        if (!save_tail_valid(fp, &info, (long)h.offset[i],
                                            (long)h.offset[i] + (long)h.size[i])) {
            ok = false;
        }
    }
    fclose(fp);
    return ok && present > 0;
}

bool qlcg_saves_verify_file(const char *path) { return verify_container(path,-1); }

bool qlcg_saves_import_file(const char *source, char *out_path, size_t capacity) {
    FILE *in=fopen_u8(source,"rb"); if(!in) return false;
    long bytes=file_size_of(in); fclose(in); if(bytes<=0) return false;
    qlcg_platform_mkdir("userdata"); qlcg_platform_mkdir("userdata/saves");
    char path[260],temporary[280]; QlLocalTime time; qlcg_platform_local_time(&time);
    bool available=false;
    for(int i=0;i<100;i++) {
        snprintf(path,sizeof(path),"userdata/saves/import_%04d%02d%02d_%02d%02d%02d_%d.sav",time.year,time.month,time.day,time.hour,time.minute,time.second,i);
        snprintf(temporary,sizeof(temporary),"%s.new",path);
        if(!qlcg_platform_exists(path) && !qlcg_platform_exists(temporary)) { available=true; break; }
    }
    if(!available || !out_path || strlen(path)>=capacity || !qlcg_platform_copy(source,temporary)) return false;
    bool ok=qlcg_saves_migrate_file(temporary) && verify_container(temporary,-1);
    if(ok) { FILE *fp=fopen_u8(temporary,"r+b"); ok=fp && qlcg_platform_sync(fp); if(fp) fclose(fp); }
    if(ok) ok=rename_u8(temporary,path)==0;
    if(!ok) remove_u8(temporary); else ui_u8_copy(out_path,capacity,path);
    return ok;
}

bool qlcg_saves_migrate_file(const char *path) {
    FILE *in=fopen_u8(path,"rb"); if(!in) return false;
    SaveFileInfo cat; long bytes=file_size_of(in);
    bool ok=bytes>0 && catalog_load(in,bytes,&cat), needed=false;
    for(int i=0;ok && i<5;i++) if(cat.slots[i].present) {
        if(!qlcg_save_version_supported(cat.slots[i].disk_version)) ok=false;
        if(cat.slots[i].disk_version!=QLCG_SAVE_VERSION) needed=true;
    }
    if(!ok || !needed) { fclose(in); return ok; }
#ifndef _WIN32
    fclose(in); return false;
#else
    char temporary[1024],backup[1024];
    if(snprintf(temporary,sizeof(temporary),"%s.migrate",path)>=(int)sizeof(temporary)) { fclose(in); return false; }
    FILE *out=fopen_u8(temporary,"w+b"); if(!out) { fclose(in); return false; }
    SaveContainerHeader h; ok=container_write_start(out,&h,cat.slots);
    for(int i=0;ok && i<5;i++) if(cat.slots[i].present) {
        SaveSlotMeta *meta=&cat.slots[i]; SaveSlotInfo info;
        ok=read_save_header_at(in,meta->offset,&info) && save_tail_valid(in,&info,meta->offset,meta->offset+meta->size);
        long start=ftell(out);
        if(ok && meta->disk_version==QLCG_SAVE_VERSION) ok=copy_bytes(in,meta->offset,meta->size,out);
        else if(ok) {
            ui_u8_copy(info.file_path,sizeof(info.file_path),path);
            MacroWorld archive={0}; archive.opt=info.opt; qlcg_sync_creator_dimensions(&archive.opt); archive.columns=archive.opt.columns;
            ok=qlcg_saves_read_rivers(&archive,&info,meta->offset,meta->size);
            if(!ok) { qlcg_river_archive_free(&archive); break; }
            MacroWorld *world=qlcg_world_generate_with_rivers(&archive.opt,archive.river_archive);
            if(!world) { ok=false; break; }
            ok=qlcg_saves_restore_deltas(world,&info,meta->offset,meta->size) &&
                write_save_header(out,&info) && write_slot_body(out,world,&info);
            qlcg_world_free(world);
        }
        long end=ftell(out);
        if(start<0 || end<start) ok=false;
        h.offset[i]=(uint64_t)start; h.size[i]=(uint64_t)(end-start); h.present[i]=1; h.kind[i]=(uint8_t)i;
    }
    if(ok) ok=container_finish(out,&h) && qlcg_platform_sync(out);
    if(fclose(out)) ok=false;
    fclose(in);
    if(ok) ok=verify_container(temporary,-1);
    if(ok) {
        bool saved=false;
        for(int i=0;i<100 && !saved;i++) {
            int n=snprintf(backup,sizeof(backup),"%s.legacy-%d",path,i);
            if(n<0 || n>=(int)sizeof(backup)) break;
            if(!qlcg_platform_exists(backup)) { saved=qlcg_platform_copy(path,backup); break; }
        }
        ok=saved && rename_u8(temporary,path)==0;
    }
    if(!ok) remove_u8(temporary);
    return ok;
#endif
}

/* 填槽头：世界状态与视点 */
static void fill_slot_info(SaveSlotInfo *slot, const MacroWorld *w, const CalendarClock *clk,
                           const AtlasDisplayState *atlas_st, float px, float py, float pz,
                           float yaw, float pitch, bool flying, bool menu_auto_pause,
                           const char *id, const char *path, const char *name) {
    memset(slot, 0, sizeof(*slot));
    slot->valid = true;
    slot->save_version = QLCG_SAVE_VERSION;
    slot->disk_version = QLCG_SAVE_VERSION;
    ui_u8_copy(slot->world_name, sizeof(slot->world_name), name);
    ui_u8_copy(slot->ren_ming, sizeof(slot->ren_ming), w->opt.ren_ming);
    slot->main_color    = w->opt.faction_main_color;
    slot->play_mode     = w->opt.play_mode;
    slot->seed          = w->opt.seed;
    slot->calendar_mode = clk->mode;
    slot->unix_ms       = clk->unix_ms;

    CalendarSnapshot snap;
    qlcg_calendar_snapshot(clk, 480, &snap);
    if (clk->mode == CALENDAR_YUAN) {
        slot->year = snap.yuan.year; slot->month = snap.yuan.month; slot->day = snap.yuan.day;
    } else {
        slot->year = snap.gregorian.year; slot->month = snap.gregorian.month; slot->day = snap.gregorian.day;
    }

    QlLocalTime st;
    qlcg_platform_local_time(&st);
    snprintf(slot->saved_at_str, sizeof(slot->saved_at_str), "%04d-%02d-%02d %02d:%02d:%02d",
             st.year, st.month, st.day, st.hour, st.minute, st.second);

    ui_u8_copy(slot->id, sizeof(slot->id), id);
    ui_u8_copy(slot->file_path, sizeof(slot->file_path), path);
    slot->player_x = px; slot->player_y = py; slot->player_z = pz;
    slot->player_yaw = yaw; slot->player_pitch = pitch;
    slot->flying = flying;
    slot->opt = w->opt;
    slot->opt.calendar_mode = slot->calendar_mode;
    qlcg_init_atlas_display(&slot->atlas_display);
    if (atlas_st) slot->atlas_display = *atlas_st;
    qlcg_normalize_atlas_display(&slot->atlas_display);
    /* 玄虚：生存模式强制回填创建值，悠游保存当前设置 */
    if (w->opt.play_mode == PLAY_MODE_SURVIVAL) {
        slot->mystery_time_speed     = w->opt.creation_time_speed;
        slot->mystery_menu_auto_pause = false;
    } else {
        slot->mystery_time_speed     = clk ? clk->time_speed : w->opt.creation_time_speed;
        slot->mystery_menu_auto_pause = menu_auto_pause;
    }
    slot->delta_count = qlcg_clampi(w->delta_count, 0, MAX_MACRO_DELTAS);
    for (int i = 0; i < slot->delta_count && i < 64; ++i) slot->deltas[i] = w->deltas[i];
}

/* 保存到指定槽；file_id 空则新建文件 */
bool qlcg_saves_write_slot_named(const MacroWorld *w, const CalendarClock *clk,
                           const AtlasDisplayState *atlas_st,
                           float px, float py, float pz, float yaw, float pitch, bool flying,
                           bool menu_auto_pause,
                           const char *file_id, const char *save_name, int slot_kind, const char *slot_name,
                           char *out_id, size_t id_cap, int *out_slot,
                           char *out_msg, size_t msg_cap) {
    if (!w || !out_msg || msg_cap == 0) return false;
    out_msg[0] = '\0';
    qlcg_platform_mkdir("userdata");
    qlcg_platform_mkdir("userdata/saves");

    char id[64];
    char path[300];
    const char *name = (save_name && *save_name) ? save_name : w->opt.world_name;
    if (!file_id || !*file_id) {
        make_file_id(id, sizeof(id), name, w->opt.seed);
    } else {
        if (strchr(file_id, '/') || strchr(file_id, '\\') || strlen(file_id) >= sizeof(id)) {
            snprintf(out_msg, msg_cap, "%s", tr("saves.err.container"));
            return false;
        }
        ui_u8_copy(id, sizeof(id), file_id);
    }
    snprintf(path, sizeof(path), "userdata/saves/%s.sav", id);

    if(qlcg_platform_exists(path) && !qlcg_saves_migrate_file(path)) {
        snprintf(out_msg,msg_cap,"%s",tr("saves.err.incomplete")); return false;
    }
    SaveSlotInfo slot;
    fill_slot_info(&slot, w, clk, atlas_st, px, py, pz, yaw, pitch, flying,
                   menu_auto_pause, id, path, name);

    FILE *old = fopen_u8(path, "rb");
    SaveFileInfo old_cat;
    memset(&old_cat, 0, sizeof(old_cat));
    if (old) {
        long bytes = file_size_of(old);
        if (bytes <= 0 || !catalog_load(old, bytes, &old_cat) ||
            !qlcg_save_version_supported(old_cat.slots[old_cat.newest_slot].disk_version)) {
            fclose(old);
            snprintf(out_msg, msg_cap, "%s", tr("saves.err.container"));
            return false;
        }
    }

    int target = slot_kind;
    if (target == SAVE_SLOT_MANUAL_ANY) target = pick_manual_slot(old ? &old_cat : NULL);
    if (target < 0 || target >= QLCG_SAVE_SLOT_COUNT) target = SAVE_SLOT_MANUAL_1;
    if (slot_name && target >= SAVE_SLOT_MANUAL_1 && target <= SAVE_SLOT_MANUAL_3)
        ui_u8_copy(old_cat.slots[target].name, sizeof(old_cat.slots[target].name), slot_name);

    char tmp_path[sizeof(slot.file_path) + 8];
    snprintf(tmp_path, sizeof(tmp_path), "%s.new", path);
    FILE *fp = fopen_u8(tmp_path, "w+b");
    if (!fp) {
        if (old) fclose(old);
        snprintf(out_msg, msg_cap, "%s", tr("saves.err.noDir"));
        return false;
    }
    SaveContainerHeader h;
    bool ok = container_write_start(fp, &h, old_cat.slots);
    for (int i = 0; ok && i < QLCG_SAVE_SLOT_COUNT; i++) {
        long off = ftell(fp);
        if (off < 0) { ok = false; break; }
        if (i == target) {
            h.offset[i] = (uint64_t)off;
            ok = write_save_header(fp, &slot) && write_slot_body(fp, w, &slot);
            long end = ftell(fp);
            if (ok && end < off) ok = false;
            if (ok) h.size[i] = (uint64_t)(end - off);
        } else if (old && old_cat.slots[i].present) {
            h.offset[i] = (uint64_t)off;
            ok = copy_bytes(old, old_cat.slots[i].offset, old_cat.slots[i].size, fp);
            h.size[i] = (uint64_t)old_cat.slots[i].size;
        } else {
            continue;
        }
        h.present[i] = 1;
        h.kind[i] = (uint8_t)i;
    }
    if (ok) ok = container_finish(fp,&h);
    if (ok) ok=qlcg_platform_sync(fp);
    if (fclose(fp) != 0) ok = false;
    if (old) fclose(old);
    if (ok) ok = verify_container(tmp_path, target);
    if (ok && rename_u8(tmp_path, path) != 0) ok = false;
    if (!ok) {
        remove_u8(tmp_path);
        snprintf(out_msg, msg_cap, "%s", tr("saves.err.incomplete"));
        return false;
    }

    if (out_id && id_cap) ui_u8_copy(out_id, id_cap, id);
    if (out_slot) *out_slot = target;
    if (target == SAVE_SLOT_QUICK)
        snprintf(out_msg, msg_cap, tr("saves.ok.quick"), slot.world_name);
    else if (target == SAVE_SLOT_AUTO)
        snprintf(out_msg, msg_cap, tr("saves.ok.auto"), slot.world_name);
    else {
        SaveSlotMeta *meta = &old_cat.slots[target];
        default_manual_name(meta->default_name, sizeof(meta->default_name), &slot);
        snprintf(out_msg, msg_cap, tr("saves.ok.manual"), target, qlcg_save_slot_display_name(meta));
    }
    return true;
}

bool qlcg_saves_write_slot(const MacroWorld *w, const CalendarClock *clk,
                           const AtlasDisplayState *atlas_st,
                           float px, float py, float pz, float yaw, float pitch, bool flying,
                           bool menu_auto_pause, const char *file_id, const char *save_name,
                           int slot_kind, char *out_id, size_t id_cap, int *out_slot,
                           char *out_msg, size_t msg_cap) {
    return qlcg_saves_write_slot_named(w, clk, atlas_st, px, py, pz, yaw, pitch, flying,
                                      menu_auto_pause, file_id, save_name, slot_kind, NULL,
                                      out_id, id_cap, out_slot, out_msg, msg_cap);
}

bool qlcg_saves_restore_deltas(MacroWorld *w, const SaveSlotInfo *slot, long offset, long size) {
    if (!w || !slot) return false;
    if(slot->disk_version==QLCG_SAVE_VERSION) {
        FILE *fp=fopen_u8(slot->file_path,"rb"); if(!fp) return false;
        bool ok=offset>=0 && size>0 && offset<=QLCG_FILE_LIMIT-size && fseek(fp,offset+qlcg_portable_header_size(),SEEK_SET)==0 &&
            qlcg_portable_body_read(fp,w,&slot->opt,slot->delta_count,offset+size);
        fclose(fp); return ok;
    }
    int count = qlcg_clampi(slot->delta_count, 0, MAX_MACRO_DELTAS);
    if (slot->save_version == 2u) count = qlcg_clampi(count, 0, 64);
    w->delta_count = 0;
    if (count > 64 || slot_disk_version(slot) >= 6u) {
        FILE *fp = fopen_u8(slot->file_path, "rb");
        if (!fp) return false;
        long end = size > 0 ? offset + size : 0;
        size_t extra = count > 64 ? (size_t)(count - 64) : 0;
        bool ok = fseek(fp, offset + save_header_size(slot), SEEK_SET) == 0;
        if (ok && extra) ok = fread(w->deltas + 64, sizeof(SparseDeltaRecord), extra, fp) == extra;
        if (ok && slot_disk_version(slot) >= 6u) ok = qlcg_factions_read_lim(fp, w, end);
        if (ok && slot_disk_version(slot) >= 6u) ok = read_exploration(fp, &w->opt, w, end);
        /* 可选 ICE1 */
        if (ok && slot_disk_version(slot) >= 6u) ok = qlcg_ice_read_optional_lim(fp, w, end);
        if (ok && slot_disk_version(slot) >= 6u) ok = qlcg_astra_events_read(fp, w, end);
        if (ok && slot_disk_version(slot) >= 6u) {
            if(!w->river_archive_preloaded) { qlcg_source_rivers_free(w); qlcg_river_surface_free(w); }
            ok = qlcg_river_archive_read(fp, w->river_archive_preloaded?NULL:w, end);
            w->river_archive_preloaded=false;
        }
        /* 可选降水尾段 */
        if(ok) ok=qlcg_infinite_read(fp,w,end);
        if (ok && slot_disk_version(slot) >= 6u) ok = qlcg_precip_read_optional_lim(fp, w, end);
        fclose(fp);
        if (!ok) return false;
    }
    memcpy(w->deltas, slot->deltas, (size_t)(count < 64 ? count : 64) * sizeof(SparseDeltaRecord));
    w->delta_count = count;
    w->delta_version++;
    return true;
}

/* 先读河线，再生成基础世界 */
bool qlcg_saves_read_rivers(MacroWorld *w,const SaveSlotInfo *slot,long offset,long size) {
    FILE *fp=fopen_u8(slot->file_path,"rb"); if(!fp) return false;
    long end=size>0?offset+size:io_end(fp,0);
    if(slot->disk_version==QLCG_SAVE_VERSION) {
        bool ok=fseek(fp,offset+qlcg_portable_header_size(),SEEK_SET)==0 && qlcg_portable_rivers(fp,w,end);
        fclose(fp); return ok;
    }
    int extra=slot->delta_count>64?slot->delta_count-64:0;
    long at=offset+save_header_size(slot)+(long)((size_t)extra*sizeof(SparseDeltaRecord));
    bool ok=at>=offset&&at<=end&&fseek(fp,at,SEEK_SET)==0;
    long faction_end=ok?qlcg_factions_tail_end_lim(fp,end):-1;
    ok=ok&&faction_end>=0&&fseek(fp,faction_end,SEEK_SET)==0&&read_exploration(fp,&w->opt,NULL,end);
    if(ok) {
        long ice_start=ftell(fp),ice_end=qlcg_ice_tail_end_lim(fp,end);
        ok=ice_start>=0&&fseek(fp,ice_end>=0?ice_end:ice_start,SEEK_SET)==0&&qlcg_astra_events_read(fp,NULL,end)&&qlcg_river_archive_read(fp,w,end);
    }
    fclose(fp); return ok;
}

/* v5 缺尾段，升级写头读不回 */
static bool legacy_tail_missing(const SaveSlotInfo *slot) {
    return slot_disk_version(slot) < 6u;
}

/* 改名：容器逐槽升级头，旧单档保持原流程 */
static bool rename_save(const char *file_path, int target, const char *new_name,
                        char *out_msg, size_t msg_cap) {
    if (out_msg && msg_cap) out_msg[0] = '\0';
    if (!file_path || !new_name || (target < 0 && !*new_name)) {
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.nameRequired"));
        return false;
    }
    if(!qlcg_saves_migrate_file(file_path)) {
        if(out_msg && msg_cap) snprintf(out_msg,msg_cap,"%s",tr("saves.renameFailed"));
        return false;
    }
    FILE *fp = fopen_u8(file_path, "rb");
    if (!fp) {
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.renameFailed"));
        return false;
    }
    long bytes = file_size_of(fp);
    SaveFileInfo cat;
    if (bytes <= 0 || !catalog_load(fp, bytes, &cat)) {
        fclose(fp);
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.renameFailed"));
        return false;
    }
    if (target >= 0 && !cat.slots[target].present) {
        fclose(fp);
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.renameFailed"));
        return false;
    }

    char tmp_path[320];
    snprintf(tmp_path, sizeof(tmp_path), "%s.new", file_path);

    if (!cat.container && target < 0) {
        SaveSlotInfo slot;
        if (!read_save_header_at(fp, 0, &slot)) { fclose(fp); return false; }
        if (legacy_tail_missing(&slot)) {
            fclose(fp);
            if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.err.legacy"));
            return false;
        }
        ui_u8_copy(slot.world_name, sizeof(slot.world_name), new_name);
        ui_u8_copy(slot.opt.world_name, sizeof(slot.opt.world_name), new_name);
        long old_head = save_header_size(&slot);
        FILE *out = fopen_u8(tmp_path, "w+b");
        if (!out) { fclose(fp); return false; }
        bool ok = write_save_header(out, &slot);
        if (ok) ok = fseek(fp, old_head, SEEK_SET) == 0;
        if (ok) ok = copy_bytes(fp, old_head, bytes - old_head, out);
        if (ok) ok=qlcg_platform_sync(out);
    if (fclose(out) != 0) ok = false;
        fclose(fp);
        if (!ok || rename_u8(tmp_path, file_path) != 0) {
            remove_u8(tmp_path);
            if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.renameFailed"));
            return false;
        }
        if (out_msg) snprintf(out_msg, msg_cap, tr("saves.renamed"), slot.world_name);
        return true;
    }

    for (int i = 0; i < QLCG_SAVE_SLOT_COUNT; i++) {
        if (target < 0 && cat.slots[i].present && cat.slots[i].disk_version < 6u) {
            fclose(fp);
            if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.err.legacy"));
            return false;
        }
    }

    FILE *out = fopen_u8(tmp_path, "w+b");
    if (!out) { fclose(fp); return false; }
    if (target >= 0)
        ui_u8_copy(cat.slots[target].name, sizeof(cat.slots[target].name), new_name);
    SaveContainerHeader h;
    bool ok = container_write_start(out, &h, cat.slots);
    for (int i = 0; ok && i < QLCG_SAVE_SLOT_COUNT; i++) {
        const SaveSlotMeta *meta = &cat.slots[i];
        if (!meta->present) continue;
        long off = ftell(out);
        if (off < 0) { ok = false; break; }
        if (target >= 0) {
            ok = copy_bytes(fp, meta->offset, meta->size, out);
        } else {
            SaveSlotInfo info;
            if (!read_save_header_at(fp, meta->offset, &info)) { ok = false; break; }
            long old_head = header_size_of(slot_disk_version(&info));
            ui_u8_copy(info.world_name, sizeof(info.world_name), new_name);
            ui_u8_copy(info.opt.world_name, sizeof(info.opt.world_name), new_name);
            ok = write_save_header(out, &info) &&
                 copy_bytes(fp, meta->offset + old_head, meta->size - old_head, out);
        }
        long end = ftell(out);
        if (ok && end < off) ok = false;
        if (!ok) break;
        h.offset[i] = (uint64_t)off;
        h.size[i] = (uint64_t)(end - off);
        h.present[i] = 1;
        h.kind[i] = (uint8_t)i;
    }
    if (ok) ok = container_finish(out,&h);
    if (ok) ok=qlcg_platform_sync(out);
    if (fclose(out) != 0) ok = false;
    fclose(fp);
    if (ok) ok = verify_container(tmp_path, -1);
    if (!ok || rename_u8(tmp_path, file_path) != 0) {
        remove_u8(tmp_path);
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.renameFailed"));
        return false;
    }
    if (out_msg) {
        if (target < 0) snprintf(out_msg, msg_cap, tr("saves.renamed"), new_name);
        else snprintf(out_msg, msg_cap, "%s", tr("saves.slotRenamed"));
    }
    return true;
}

bool qlcg_saves_rename(const char *file_path, const char *new_name,
                       char *out_msg, size_t msg_cap) {
    return rename_save(file_path, -1, new_name, out_msg, msg_cap);
}

bool qlcg_saves_rename_slot(const char *file_path, int slot, const char *name,
                           char *out_msg, size_t msg_cap) {
    if (slot < SAVE_SLOT_MANUAL_1 || slot > SAVE_SLOT_MANUAL_3) {
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.renameFailed"));
        return false;
    }
    return rename_save(file_path, slot, name, out_msg, msg_cap);
}

/* 将所选槽复制为单槽悠游存档 */
bool qlcg_saves_copy_to_leisure(const SaveFileInfo *file, int slot,
                                char *out_msg, size_t msg_cap) {
    if (!file || !file->valid) return false;
    if (slot < 0 || slot >= QLCG_SAVE_SLOT_COUNT) slot = file->newest_slot;
    if (!file->slots[slot].present) {
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.err.deltaRead"));
        return false;
    }
    SaveFileInfo refreshed;
    if(!qlcg_saves_migrate_file(file->file_path)) return false;
    FILE *scan=fopen_u8(file->file_path,"rb"); if(!scan) return false;
    bool catalog_ok=catalog_load(scan,file_size_of(scan),&refreshed); fclose(scan);
    if(!catalog_ok) return false;
    ui_u8_copy(refreshed.file_path,sizeof(refreshed.file_path),file->file_path);
    ui_u8_copy(refreshed.id,sizeof(refreshed.id),file->id); file=&refreshed;
    FILE *in = fopen_u8(file->file_path, "rb");
    if (!in) {
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.err.deltaRead"));
        return false;
    }
    long off = file->slots[slot].offset;
    long size = file->slots[slot].size;
    SaveSlotInfo copy;
    if (!read_save_header_at(in, off, &copy) || !save_tail_valid(in, &copy, off, off + size)) {
        fclose(in);
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.err.deltaRead"));
        return false;
    }
    if (legacy_tail_missing(&copy)) {
        fclose(in);
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.err.legacy"));
        return false;
    }
    long old_head = header_size_of(slot_disk_version(&copy));
    copy.play_mode = PLAY_MODE_LEISURE;
    copy.opt.play_mode = PLAY_MODE_LEISURE;
    const char *sfx = tr("saves.copySuffix");
    char nm[240];
    size_t keep = sizeof(nm) - 1;
    if (strlen(sfx) < keep) keep -= strlen(sfx);
    ui_u8_copy(nm, keep + 1, copy.world_name);
    strncat(nm, sfx, sizeof(nm) - strlen(nm) - 1);
    ui_u8_copy(copy.world_name, sizeof(copy.world_name), nm);
    ui_u8_copy(copy.opt.world_name, sizeof(copy.opt.world_name), nm);

    char id[64];
    snprintf(id, sizeof(id), "%.48s_leisure", file->id);
    for (int n = 2; n < 100 && save_id_exists(id); n++)
        snprintf(id, sizeof(id), "%.40s_leisure_%d", file->id, n);
    char path[300];
    snprintf(path, sizeof(path), "userdata/saves/%s.sav", id);
    ui_u8_copy(copy.id, sizeof(copy.id), id);
    ui_u8_copy(copy.file_path, sizeof(copy.file_path), path);

    char tmp_path[320];
    snprintf(tmp_path, sizeof(tmp_path), "%s.new", path);
    FILE *out = fopen_u8(tmp_path, "w+b");
    if (!out) {
        fclose(in);
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.err.convert"));
        return false;
    }
    SaveContainerHeader h;
    SaveSlotMeta slots[QLCG_SAVE_SLOT_COUNT] = {0};
    ui_u8_copy(slots[SAVE_SLOT_MANUAL_1].name, sizeof(slots[SAVE_SLOT_MANUAL_1].name),
               file->slots[slot].name);
    bool ok = container_write_start(out, &h, slots);
    long slot_off = ftell(out);
    ok = ok && slot_off >= 0 && write_save_header(out, &copy) &&
         copy_bytes(in, off + old_head, size - old_head, out);
    long slot_end = ftell(out);
    if (ok && slot_end < slot_off) ok = false;
    if (ok) {
        h.offset[SAVE_SLOT_MANUAL_1] = (uint64_t)slot_off;
        h.size[SAVE_SLOT_MANUAL_1] = (uint64_t)(slot_end - slot_off);
        h.present[SAVE_SLOT_MANUAL_1] = 1;
        h.kind[SAVE_SLOT_MANUAL_1] = SAVE_SLOT_MANUAL_1;
        ok = container_finish(out,&h);
    }
    if (ok) ok=qlcg_platform_sync(out);
    if (fclose(out) != 0) ok = false;
    fclose(in);
    if (!ok) {
        remove_u8(tmp_path);
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.err.convertWrite"));
        return false;
    }
    if (!verify_container(tmp_path,-1) || rename_u8(tmp_path, path) != 0) {
        remove_u8(tmp_path);
        if (out_msg) snprintf(out_msg, msg_cap, "%s", tr("saves.err.convert"));
        return false;
    }
    if (out_msg) snprintf(out_msg, msg_cap, tr("saves.ok.copied"), copy.world_name);
    return true;
}
