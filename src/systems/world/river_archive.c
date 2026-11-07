/* 定稿河线与运行缓存解耦 */
#include "river_archive.h"
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
    if(terrain_version>QLCG_TERRAIN_VERSION) return NULL;
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
bool qlcg_river_archive_write(FILE *file,const MacroWorld *w) {
    const struct RiverArchive *a=w->river_archive;
    uint32_t head[6]={0x31564952u,3,w->opt.seed,(uint32_t)w->columns,(uint32_t)w->opt.chunks_per_cell,a?a->count:0};
    uint64_t sequence=a?a->craft_sequence:0;
    uint32_t terrain=qlcg_river_archive_terrain(w);
    if(fwrite(head,sizeof(head),1,file)!=1) return false;
    if(fwrite(&sequence,sizeof(sequence),1,file)!=1||fwrite(&terrain,sizeof(terrain),1,file)!=1) return false;
    if(a) for(int i=0;i<4096;i++) for(const RiverRecord *r=a->buckets[i];r;r=r->next) {
        uint32_t h[4]={r->kind,r->cell,r->size,r->checksum};
        if(fwrite(h,sizeof(h),1,file)!=1||fwrite(r->data,1,r->size,file)!=r->size) return false;
    }
    return true;
}
bool qlcg_river_archive_read(FILE *file,MacroWorld *w,long limit) {
    long start=ftell(file),end=limit;
    if(end<=0) { if(fseek(file,0,SEEK_END)) return false; end=ftell(file); }
    if(start<0||end<start||fseek(file,start,SEEK_SET)) return false;
    if(start==end) return legacy_archive(w);
    uint32_t h[6]={0};
    if(end-start<4||fread(h,4,1,file)!=1) return false;
    if(h[0]!=0x31564952u) return fseek(file,start,SEEK_SET)==0&&legacy_archive(w);
    if(end-start<(long)sizeof(h)||fread(h+1,sizeof(h)-4,1,file)!=1||h[1]<1||h[1]>3||
       h[3]<64||h[3]>1024||h[3]%4) return false;
    uint32_t cells=h[3]*h[3]/(w&&!qlcg_world_flat(w)?2:1);
    if(h[5]>cells*3) return false;
    if(w&&(h[2]!=w->opt.seed||h[3]!=(uint32_t)w->columns||h[4]!=(uint32_t)w->opt.chunks_per_cell)) return false;
    MacroWorld staging={0};
    uint64_t sequence=0;uint32_t terrain=0;
    if(h[1]>=2&&(end-ftell(file)<(long)sizeof(sequence)||fread(&sequence,sizeof(sequence),1,file)!=1)) return false;
    if(h[1]>=3&&(end-ftell(file)<(long)sizeof(terrain)||fread(&terrain,sizeof(terrain),1,file)!=1||terrain>QLCG_TERRAIN_VERSION)) return false;
    bool ok=true;
    for(uint32_t i=0;ok&&i<h[5];i++) {
        uint32_t r[4];
        long at=ftell(file);
        if(at<0||end-at<(long)sizeof(r)||fread(r,sizeof(r),1,file)!=1||
           r[0]<1||r[0]>3||r[1]>=cells||r[2]>RIVER_RECORD_MAX||r[2]<4||end-ftell(file)<(long)r[2]) { ok=false; break; }
        RiverBuffer b={0},existing={0}; b.size=r[2]; b.data=malloc(b.size);
        if(!b.data) { ok=false; break; }
        ok=fread(b.data,1,b.size,file)==b.size&&checksum(b.data,b.size)==r[3]&&
            !qlcg_river_archive_get(&staging,r[0],r[1],&existing)&&qlcg_river_archive_put(&staging,r[0],r[1],&b);
        qlcg_river_buffer_free(&b);
    }
    if(ok&&w) {
        if(!staging.river_archive) staging.river_archive=qlcg_river_archive_create(terrain);
        if(!staging.river_archive) return false;
        staging.river_archive->craft_sequence=sequence;
        staging.river_archive->terrain_version=terrain;
        if(qlcg_river_archive_terrain(w)!=terrain) w->surface_revision++;
        qlcg_river_archive_free(w); w->river_archive=staging.river_archive; staging.river_archive=NULL;
    }
    qlcg_river_archive_free(&staging);
    return ok;
}
