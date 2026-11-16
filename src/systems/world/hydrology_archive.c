#include "hydrology_oxbow.h"
#include "hydrology_river.h"
#include "hydrology_river_internal.h"
#include "core/binary.h"
#include "core/config.h"
#include "core/math_util.h"
#include "surface_frame.h"
/* 定稿河线与运行缓存解耦 */
#include "hydrology_archive.h"
#include "hydrology_fields.h"
#include "surface_detail.h"
#include "hydrology_sources.h"
#include "world_infinite.h"
#include "ice_runtime.h"
#include <stdlib.h>
#include <string.h>

#define RIVER_RECORD_MAX (16u*1024u*1024u)
typedef struct RiverRecord {
    struct RiverRecord *next;
    uint32_t kind,cell,size,checksum;
    unsigned char data[];
} RiverRecord;
struct RiverArchive {
    RiverRecord *buckets[4096];
    uint32_t count;
    uint64_t craft_sequence;
    uint32_t terrain_version;
};
struct RiverArchive *qlcg_river_archive_create(uint32_t terrain_version) {
    if(terrain_version>QLCG_TERRAIN_VERSION||terrain_version==6||terrain_version==7) return NULL;
    struct RiverArchive *a=calloc(1,sizeof(*a));
    if(a) a->terrain_version=terrain_version;
    return a;
}
uint32_t qlcg_river_archive_terrain(const MacroWorld *w) {
    return w&&w->river_archive?w->river_archive->terrain_version:QLCG_TERRAIN_VERSION;
}
static bool legacy_archive(MacroWorld *w) {
    if(!w) return true;
    struct RiverArchive *a=qlcg_river_archive_create(0);
    if(!a) return false;
    if(qlcg_river_archive_terrain(w)!=0) w->surface_revision++;
    qlcg_river_archive_free(w);w->river_archive=a;
    return true;
}
static unsigned bucket(int kind,int cell) { return ((uint32_t)cell*2654435761u+(uint32_t)kind*97u)&4095; }
static int headwater_valid(uint32_t terrain,const unsigned char *data,size_t size,int cell) {
    uint32_t version=0;if(size<4)return 0;memcpy(&version,data,4);
    return terrain>=5&&(terrain>=8||version<=4)&&qlcg_headwater_validate(data,size,cell);
}
static int upgrade_check(const MacroWorld *w) {
    uint32_t terrain=qlcg_river_archive_terrain(w);
    if(terrain>=QLCG_TERRAIN_VERSION) return 0;
    if(w->delta_count||qlcg_ice_edit_count(w)) return -2;
    const struct RiverArchive *a=w->river_archive;
    if(a&&a->craft_sequence&&terrain<13) return -2;
    if(a) for(int i=0;i<4096;i++) for(const RiverRecord *r=a->buckets[i];r;r=r->next) {
        RiverBuffer b={(unsigned char *)r->data,r->size,0,0,1,0};int result=-1;
        if(r->kind==RIVER_RECORD_MAIN) result=qlcg_river_main_natural(w,&b);
        else if(r->kind==RIVER_RECORD_SOURCE) {
            result=qlcg_river_sources_natural(w,r->cell,&b);
            if(result==-2&&terrain>=13) result=1;
        }
        else if(r->kind==RIVER_RECORD_OXBOW) {
            int valid=0;OxbowPlan plan={0};qlcg_river_values(&b,&valid,1,4);
            if(valid==1) qlcg_river_OxbowPlan(&b,&plan);
            result=!b.failed&&b.at==b.size&&(valid==0||valid==1)?1:-1;
        }
        else if(r->kind==RIVER_RECORD_HEADWATER)
            result=headwater_valid(terrain,r->data,r->size,r->cell)?1:-1;
        if(result<0) return result;
    }
    return 1;
}
static void upgrade_apply(MacroWorld *w) {
    uint32_t terrain=qlcg_river_archive_terrain(w);
    if(terrain>=QLCG_TERRAIN_VERSION) return;
    struct RiverArchive *a=w->river_archive;
    for(int i=0;a&&terrain<13&&i<4096;i++) {
        RiverRecord **slot=a->buckets+i;
        while(*slot) {
            RiverRecord *r=*slot;
            const MacroCellPlan *m=r->kind==RIVER_RECORD_SOURCE?qlcg_macro_cell_plan(w,r->cell):NULL;
            if(r->kind==RIVER_RECORD_HEADWATER||(m&&m->head)) {
                *slot=r->next;free(r);a->count--;
            } else slot=&r->next;
        }
    }
    qlcg_source_rivers_free(w);qlcg_river_surface_free(w);qlcg_oxbow_free(w);qlcg_terrain_detail_free(w);
    if(a)a->terrain_version=QLCG_TERRAIN_VERSION;
    w->surface_revision++;
    w->decor_ready=false;qlcg_world_rebuild_atlas_fields(w);w->decor_ready=true;
}
int qlcg_world_upgrade_terrain(MacroWorld *w) {
    int result=upgrade_check(w);if(result<0) return result;
    if(w->planes) for(size_t k=0;k<w->planes->count;k++) {
        int status=upgrade_check(w->planes->items[k]);if(status<0) return status;
        if(status>0) result=1;
    }
    if(!result) return 0;
    if(w->planes) for(size_t k=0;k<w->planes->count;k++) upgrade_apply(w->planes->items[k]);
    upgrade_apply(w);return 1;
}
static uint32_t checksum(const unsigned char *data,size_t size) {
    uint32_t hash=2166136261u;
    for(size_t i=0;i<size;i++) { hash^=data[i]; hash*=16777619u; }
    return hash;
}
uint64_t qlcg_river_archive_next_id(MacroWorld *w) {
    if(!w->river_archive) w->river_archive=qlcg_river_archive_create(QLCG_TERRAIN_VERSION);
    return w->river_archive?++w->river_archive->craft_sequence:0;
}
void qlcg_river_io(RiverBuffer *b,void *data,size_t size) {
    if(b->failed) return;
    if(b->reading) {
        if(b->at>b->size||size>b->size-b->at) { b->failed=1; return; }
        memcpy(data,b->data+b->at,size); b->at+=size; return;
    }
    if(size>RIVER_RECORD_MAX||b->size>RIVER_RECORD_MAX-size) { b->failed=1; return; }
    if(b->size+size>b->capacity) {
        size_t cap=(b->size+size)*2+256;
        unsigned char *p=realloc(b->data,cap);
        if(!p) { b->failed=1; return; }
        b->data=p; b->capacity=cap;
    }
    memcpy(b->data+b->size,data,size); b->size+=size;
}
void qlcg_river_buffer_free(RiverBuffer *b) {
    if(!b->reading) free(b->data);
    memset(b,0,sizeof(*b));
}
int qlcg_river_archive_get(const MacroWorld *w,int kind,int cell,RiverBuffer *b) {
    if(!w->river_archive) return 0;
    for(RiverRecord *r=w->river_archive->buckets[bucket(kind,cell)];r;r=r->next)
        if(r->kind==(uint32_t)kind&&r->cell==(uint32_t)cell) {
            *b=(RiverBuffer){r->data,r->size,0,0,1,0}; return 1;
        }
    return 0;
}
int qlcg_river_archive_put(MacroWorld *w,int kind,int cell,const RiverBuffer *b) {
    RiverArchiveUpdate update={kind,cell,*b};
    return qlcg_river_archive_commit(w,&update,1);
}
int qlcg_river_archive_commit(MacroWorld *w,const RiverArchiveUpdate *updates,int count) {
    if(!w||!updates||count<1) return 0;
    RiverRecord *pending=NULL;
    for(int i=count-1;i>=0;i--) {
        const RiverBuffer *b=&updates[i].buffer;
        RiverRecord *r=b->failed||b->size>RIVER_RECORD_MAX?NULL:malloc(sizeof(*r)+b->size);
        if(!r) { while(pending) { r=pending->next; free(pending); pending=r; } return 0; }
        r->kind=updates[i].kind; r->cell=updates[i].cell; r->size=(uint32_t)b->size;
        memcpy(r->data,b->data,b->size); r->checksum=checksum(r->data,r->size);
        r->next=pending; pending=r;
    }
    if(!w->river_archive) w->river_archive=qlcg_river_archive_create(QLCG_TERRAIN_VERSION);
    if(!w->river_archive) { while(pending) { RiverRecord *r=pending->next; free(pending); pending=r; } return 0; }
    while(pending) {
        RiverRecord *r=pending; pending=r->next;
        RiverRecord **slot=&w->river_archive->buckets[bucket(r->kind,r->cell)];
        while(*slot&&((*slot)->kind!=r->kind||(*slot)->cell!=r->cell)) slot=&(*slot)->next;
        RiverRecord *old=*slot; r->next=old?old->next:NULL; *slot=r;
        if(old) free(old); else w->river_archive->count++;
    }
    return 1;
}
void qlcg_river_archive_free(MacroWorld *w) {
    if(!w->river_archive) return;
    for(int i=0;i<4096;i++) {
        RiverRecord *r=w->river_archive->buckets[i];
        while(r) { RiverRecord *next=r->next; free(r); r=next; }
    }
    free(w->river_archive); w->river_archive=NULL;
}
enum { RIV_MAGIC_V1=0x31564952u, RIV_MAGIC_V2=0x32564952u, RIV_MAIN_PREFIX=140u, RIV_RAW_MAX=128u*1024u*1024u };
static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void write_u32_le(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)(v&0xFFu); p[1]=(uint8_t)((v>>8)&0xFFu); p[2]=(uint8_t)((v>>16)&0xFFu); p[3]=(uint8_t)((v>>24)&0xFFu);
}
static double read_f64_le(const uint8_t *p) {
    uint64_t u=0; for(int i=0;i<8;i++) u|=(uint64_t)p[i]<<(8*i);
    double d; memcpy(&d,&u,8); return d;
}
static void write_f64_le(uint8_t *p, double d) {
    uint64_t u=0; memcpy(&u,&d,8);
    for(int i=0;i<8;i++) p[i]=(uint8_t)(u>>(8*i));
}
static void init_frame_world(MacroWorld *dst, const MacroWorld *w, uint32_t cols) {
    memset(dst,0,sizeof(*dst));
    if(w) {
        dst->opt=w->opt;
        dst->columns=w->columns>0?w->columns:(int)cols;
        bool flat=qlcg_world_flat(w);
        dst->rows=w->rows>0?w->rows:(flat?dst->columns:dst->columns/2);
        if(w->circumference>0) {
            dst->circumference=w->circumference;
            dst->radius=w->radius>0?w->radius:w->circumference/QLCG_TAU;
        } else {
            WorldCreatorOptions dims=w->opt; qlcg_sync_creator_dimensions(&dims);
            dst->circumference=(float)dims.equator_chunks*(float)CHUNK_SIZE;
            dst->radius=dst->circumference/QLCG_TAU;
        }
    }
}
static bool archive_words(FILE *file, uint32_t *values, int count, bool reading) {
    QlCodec io=qlcg_codec(file,reading,(uint64_t)count*4);
    for(int i=0;i<count;i++) values[i]=(uint32_t)qlcg_io_uint(&io,values[i],4);
    return io.ok;
}
static bool archive_sequence(FILE *file, uint64_t *value, bool reading) {
    QlCodec io=qlcg_codec(file,reading,8); *value=qlcg_io_uint(&io,*value,8); return io.ok;
}
bool qlcg_river_archive_write(FILE *file,const MacroWorld *w) {
    const struct RiverArchive *a=w->river_archive;
    uint32_t head[6]={RIV_MAGIC_V1,3,w->opt.seed,(uint32_t)w->columns,(uint32_t)w->opt.chunks_per_cell,a?a->count:0};
    uint64_t sequence=a?a->craft_sequence:0;
    uint32_t terrain=qlcg_river_archive_terrain(w);
    if(!archive_words(file,head,6,false)) return false;
    if(!archive_sequence(file,&sequence,false)||!archive_words(file,&terrain,1,false)) return false;
    if(a) for(int i=0;i<4096;i++) for(const RiverRecord *r=a->buckets[i];r;r=r->next) {
        uint32_t h[4]={r->kind,r->cell,r->size,r->checksum};
        if(!archive_words(file,h,4,false)||fwrite(r->data,1,r->size,file)!=r->size) return false;
    }
    return true;
}
bool qlcg_river_archive_encode_compact(const MacroWorld *w,uint8_t **out_buf,size_t *out_size) {
    if(!w||!out_buf||!out_size) return false;
    *out_buf=NULL; *out_size=0;
    const struct RiverArchive *a=w->river_archive;
    size_t raw_cap=36;
    if(a) for(int i=0;i<4096;i++) for(const RiverRecord *r=a->buckets[i];r;r=r->next) raw_cap+=12u+r->size;
    if(raw_cap>RIV_RAW_MAX) return false;
    uint8_t *raw=malloc(raw_cap); if(!raw) return false;
    QlCodec io=qlcg_codec_mem(raw,false,raw_cap);
    qlcg_io_uint(&io,RIV_MAGIC_V2,4); qlcg_io_uint(&io,4,4);
    qlcg_io_uint(&io,w->opt.seed,4); qlcg_io_uint(&io,(uint32_t)w->columns,4);
    qlcg_io_uint(&io,(uint32_t)w->opt.chunks_per_cell,4); qlcg_io_uint(&io,a?a->count:0,4);
    qlcg_io_uint(&io,a?a->craft_sequence:0,8); qlcg_io_uint(&io,qlcg_river_archive_terrain(w),4);
    if(a) for(int i=0;i<4096 && io.ok;i++) for(const RiverRecord *r=a->buckets[i];r && io.ok;r=r->next) {
        bool compact_main=false; int32_t pt_count=0;
        if(r->kind==RIVER_RECORD_MAIN && r->size>=RIV_MAIN_PREFIX) {
            uint32_t u0=read_u32_le(r->data);
            pt_count=u0<=INT32_MAX?(int32_t)u0:-1-(int32_t)(UINT32_MAX-u0);
            if(pt_count>=2 && pt_count<=RIVER_CURVE_POINTS && r->size>=RIV_MAIN_PREFIX+(size_t)pt_count*40u)
                compact_main=true;
        }
        size_t enc_size=compact_main?(r->size-(size_t)pt_count*16u):r->size;
        qlcg_io_uint(&io,r->kind,4); qlcg_io_uint(&io,r->cell,4); qlcg_io_uint(&io,(uint32_t)enc_size,4);
        if(!compact_main) qlcg_io_bytes(&io,(void *)r->data,r->size);
        else {
            qlcg_io_bytes(&io,(void *)r->data,RIV_MAIN_PREFIX);
            const uint8_t *pts=r->data+RIV_MAIN_PREFIX;
            for(int k=0;k<pt_count;k++) {
                qlcg_io_bytes(&io,(void *)(pts+(size_t)k*40u),16);
                qlcg_io_bytes(&io,(void *)(pts+(size_t)k*40u+32u),8);
            }
            size_t tail_off=RIV_MAIN_PREFIX+(size_t)pt_count*40u;
            if(r->size>tail_off) qlcg_io_bytes(&io,(void *)(r->data+tail_off),r->size-tail_off);
        }
    }
    if(!io.ok || io.bytes>UINT32_MAX) { free(raw); return false; }
    size_t raw_used=(size_t)io.bytes, bound=qlcg_lz4_bound(raw_used);
    uint8_t *out=malloc(8u+bound);
    if(!out) { free(raw); return false; }
    write_u32_le(out,RIV_MAGIC_V2); write_u32_le(out+4,(uint32_t)raw_used);
    size_t comp=qlcg_lz4_compress(raw,raw_used,out+8,bound);
    free(raw);
    if(!comp) { free(out); return false; }
    *out_buf=out; *out_size=8u+comp;
    return true;
}
static bool archive_decode_body(const uint8_t *buf,size_t size,bool v2,MacroWorld *w,const WorldCreatorOptions *opt) {
    if(size<24) return false;
    QlCodec io=qlcg_codec_mem((void *)buf,true,size);
    uint32_t magic=(uint32_t)qlcg_io_uint(&io,0,4), ver=(uint32_t)qlcg_io_uint(&io,0,4);
    uint32_t seed=(uint32_t)qlcg_io_uint(&io,0,4), cols=(uint32_t)qlcg_io_uint(&io,0,4);
    uint32_t cpc=(uint32_t)qlcg_io_uint(&io,0,4), count=(uint32_t)qlcg_io_uint(&io,0,4);
    if(!io.ok || magic!=(v2?RIV_MAGIC_V2:RIV_MAGIC_V1) || (v2?ver!=4:(ver<1||ver>3)) || cols<64 || cols>1024 || cols%4) return false;
    bool flat=w?qlcg_world_flat(w):(opt && opt->map_mode==WORLD_MAP_INFINITE);
    uint32_t cells=cols*cols/(flat?1u:2u);
    if(count>cells*4u) return false;
    const WorldCreatorOptions *chk=w?&w->opt:opt;
    if(chk && (seed!=chk->seed || cols!=(uint32_t)chk->columns || cpc!=(uint32_t)chk->chunks_per_cell)) return false;
    uint64_t sequence=0; uint32_t terrain=0;
    if(v2 || ver>=2) sequence=qlcg_io_uint(&io,0,8);
    if(v2 || ver>=3) terrain=(uint32_t)qlcg_io_uint(&io,0,4);
    if(!io.ok || terrain>QLCG_TERRAIN_VERSION || terrain==6 || terrain==7) return false;
    MacroWorld staging={0}, fw={0};
    if(w && v2) init_frame_world(&fw,w,cols);
    bool ok=true;
    for(uint32_t i=0;ok && i<count;i++) {
        uint32_t kind=(uint32_t)qlcg_io_uint(&io,0,4), cell=(uint32_t)qlcg_io_uint(&io,0,4);
        uint32_t rsize=(uint32_t)qlcg_io_uint(&io,0,4);
        uint32_t cks=v2?0u:(uint32_t)qlcg_io_uint(&io,0,4);
        if(!io.ok || kind<1 || kind>4 || cell>=cells || rsize>RIVER_RECORD_MAX || rsize<4 || rsize>size-io.bytes) { ok=false; break; }
        const uint8_t *rec=buf+io.bytes;
        io.bytes+=rsize;
        if(!v2) {
            if(checksum(rec,rsize)!=cks) { ok=false; break; }
            if(kind==RIVER_RECORD_HEADWATER && !headwater_valid(terrain,rec,rsize,(int)cell)) { ok=false; break; }
            if(w) {
                RiverBuffer b={(unsigned char *)rec,rsize,0,0,1,0}, ex={0};
                ok=!qlcg_river_archive_get(&staging,(int)kind,(int)cell,&ex) && qlcg_river_archive_put(&staging,(int)kind,(int)cell,&b);
            }
        } else if(kind==RIVER_RECORD_MAIN) {
            if(rsize<RIV_MAIN_PREFIX) { ok=false; break; }
            uint32_t u0=read_u32_le(rec);
            int32_t pt_count=u0<=INT32_MAX?(int32_t)u0:-1-(int32_t)(UINT32_MAX-u0);
            if(pt_count<2 || pt_count>RIVER_CURVE_POINTS || rsize<RIV_MAIN_PREFIX+(size_t)pt_count*24u) { ok=false; break; }
            if(w) {
                size_t full_sz=rsize+(size_t)pt_count*16u;
                uint8_t *full=malloc(full_sz); if(!full) { ok=false; break; }
                memcpy(full,rec,RIV_MAIN_PREFIX);
                SurfaceFrame frame=qlcg_surface_frame(&fw,(int)cell);
                const uint8_t *src=rec+RIV_MAIN_PREFIX;
                uint8_t *dst=full+RIV_MAIN_PREFIX;
                for(int k=0;k<pt_count;k++) {
                    double wx=read_f64_le(src+(size_t)k*24u), wz=read_f64_le(src+(size_t)k*24u+8u);
                    double width=read_f64_le(src+(size_t)k*24u+16u);
                    SurfacePoint lp=qlcg_frame_local(&frame,wx,wz);
                    write_f64_le(dst+(size_t)k*40u,wx); write_f64_le(dst+(size_t)k*40u+8u,wz);
                    write_f64_le(dst+(size_t)k*40u+16u,lp.x); write_f64_le(dst+(size_t)k*40u+24u,lp.z);
                    write_f64_le(dst+(size_t)k*40u+32u,width);
                }
                size_t tail_off=RIV_MAIN_PREFIX+(size_t)pt_count*24u;
                if(rsize>tail_off) memcpy(full+RIV_MAIN_PREFIX+(size_t)pt_count*40u,rec+tail_off,rsize-tail_off);
                RiverBuffer b={full,full_sz,0,0,1,0}, ex={0};
                ok=!qlcg_river_archive_get(&staging,(int)kind,(int)cell,&ex) && qlcg_river_archive_put(&staging,(int)kind,(int)cell,&b);
                free(full);
            }
        } else {
            if(kind==RIVER_RECORD_HEADWATER && !headwater_valid(terrain,rec,rsize,(int)cell)) { ok=false; break; }
            if(w) {
                RiverBuffer b={(unsigned char *)rec,rsize,0,0,1,0}, ex={0};
                ok=!qlcg_river_archive_get(&staging,(int)kind,(int)cell,&ex) && qlcg_river_archive_put(&staging,(int)kind,(int)cell,&b);
            }
        }
    }
    if(ok && io.bytes!=size) ok=false;
    if(ok && w) {
        if(!staging.river_archive) staging.river_archive=qlcg_river_archive_create(terrain);
        if(!staging.river_archive) { qlcg_river_archive_free(&staging); return false; }
        staging.river_archive->craft_sequence=sequence;
        staging.river_archive->terrain_version=terrain;
        if(qlcg_river_archive_terrain(w)!=terrain) w->surface_revision++;
        qlcg_river_archive_free(w); w->river_archive=staging.river_archive; staging.river_archive=NULL;
    }
    qlcg_river_archive_free(&staging);
    return ok;
}
bool qlcg_river_archive_read_mem(const uint8_t *data,size_t size,MacroWorld *w,const WorldCreatorOptions *opt) {
    if(size==0) return legacy_archive(w);
    if(!data || size<4) return false;
    uint32_t magic=read_u32_le(data);
    if(magic==RIV_MAGIC_V1) return archive_decode_body(data,size,false,w,opt);
    if(magic==RIV_MAGIC_V2) {
        if(size<8) return false;
        uint32_t raw_size=read_u32_le(data+4);
        if(raw_size<36 || raw_size>RIV_RAW_MAX) return false;
        uint8_t *raw=malloc(raw_size); if(!raw) return false;
        bool ok=qlcg_lz4_decompress(data+8,size-8,raw,raw_size) && archive_decode_body(raw,raw_size,true,w,opt);
        free(raw);
        return ok;
    }
    return false;
}
bool qlcg_river_archive_read(FILE *file,MacroWorld *w,long limit) {
    long start=ftell(file),end=limit;
    if(end<=0) { if(fseek(file,0,SEEK_END)) return false; end=ftell(file); }
    if(start<0||end<start||fseek(file,start,SEEK_SET)) return false;
    if(start==end) return legacy_archive(w);
    uint32_t h[6]={0};
    if(end-start<4||!archive_words(file,h,1,true)) return false;
    if(h[0]!=RIV_MAGIC_V1) return fseek(file,start,SEEK_SET)==0&&legacy_archive(w);
    if(fseek(file,start,SEEK_SET)) return false;
    size_t size=(size_t)(end-start);
    uint8_t *buf=malloc(size); if(!buf) return false;
    bool ok=fread(buf,1,size,file)==size && qlcg_river_archive_read_mem(buf,size,w,w?&w->opt:NULL);
    free(buf);
    return ok;
}
