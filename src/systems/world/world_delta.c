#include "world_delta.h"
#include "world_prop_edit.h"
#include "planet_macro.h"
#include "surface_internal.h"
#include "core/binary.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { SIDE=CHUNK_CELLS, CELLS=SIDE*SIDE, MAX_BLOCKS=MAX_CHUNK_DELTAS, MAX_STROKE_BLOCKS=100 };
typedef struct { int16_t height,water; uint8_t material; int8_t forest; } Sample;
typedef struct { uint16_t x,z; int32_t kind; uint8_t state; } Prop;
typedef struct {
    int x,z;
    Sample samples[CELLS];
    Prop *props;
    unsigned count,capacity,workbenches;
} Block;
typedef struct ChunkDeltaTable {
    Block **blocks,**index;
    unsigned count,capacity,index_capacity;
} ChunkDeltaTable;

static unsigned key_hash(int x,int z) {
    uint32_t h=(uint32_t)x*0x9e3779b1u^(uint32_t)z*0x85ebca77u;
    h^=h>>16; h*=0x7feb352du;
    return h^(h>>15);
}

static Block **index_slot(const ChunkDeltaTable *t,int x,int z) {
    unsigned mask=t->index_capacity-1,i=key_hash(x,z)&mask;
    while(t->index[i] && (t->index[i]->x!=x || t->index[i]->z!=z)) i=(i+1)&mask;
    return &t->index[i];
}

static unsigned block_index(const ChunkDeltaTable *t,int x,int z) {
    unsigned lo=0,hi=t->count;
    while(lo<hi) {
        unsigned mid=lo+(hi-lo)/2;
        const Block *b=t->blocks[mid];
        if(b->z<z || (b->z==z && b->x<x)) lo=mid+1;
        else hi=mid;
    }
    return lo;
}
static Block *find_block(const ChunkDeltaTable *t,int x,int z) {
    return t && t->index_capacity?*index_slot(t,x,z):NULL;
}
static bool reserve_blocks(ChunkDeltaTable **owner,unsigned extra) {
    if(!*owner) *owner=calloc(1,sizeof(**owner));
    ChunkDeltaTable *t=*owner;
    if(!t || extra>MAX_BLOCKS-t->count) return false;
    unsigned count=t->count+extra,cap=t->capacity?t->capacity:8;
    while(cap<count) cap*=2;
    if(cap>t->capacity) {
        Block **blocks=realloc(t->blocks,cap*sizeof(*blocks));
        if(!blocks) return false;
        t->blocks=blocks;t->capacity=cap;
    }
    if(2*cap>t->index_capacity) {
        Block **index=calloc(2*cap,sizeof(*index));
        if(!index) return false;
        free(t->index);t->index=index;t->index_capacity=2*cap;
        for(unsigned i=0;i<t->count;i++) {
            Block *b=t->blocks[i];
            *index_slot(t,b->x,b->z)=b;
        }
    }
    return true;
}

static void insert_block(ChunkDeltaTable *t,Block *b) {
    unsigned i=block_index(t,b->x,b->z);
    memmove(t->blocks+i+1,t->blocks+i,(t->count-i)*sizeof(b));
    t->blocks[i]=b;t->count++;
    *index_slot(t,b->x,b->z)=b;
}

static Block *ensure_block(ChunkDeltaTable **owner,int x,int z) {
    Block *b=find_block(*owner,x,z);
    if(b) return b;
    if(!reserve_blocks(owner,1)) return NULL;
    b=calloc(1,sizeof(*b));
    if(!b) return NULL;
    b->x=x;b->z=z;
    insert_block(*owner,b);
    return b;
}
static void release_table(ChunkDeltaTable *t) {
    if(!t) return;
    for(unsigned i=0;i<t->count;i++) {
        free(t->blocks[i]->props);
        free(t->blocks[i]);
    }
    free(t->index);free(t->blocks);free(t);
}
void qlcg_chd_free(MacroWorld *w) {
    release_table(w->chunk_deltas);
    w->chunk_deltas=NULL;
}
static void install_table(MacroWorld *w,ChunkDeltaTable *table) {
    if (w->chunk_deltas || table) w->surface_revision++;
    qlcg_chd_free(w);
    w->chunk_deltas=table;
}
void qlcg_chd_clear(MacroWorld *w) {
    install_table(w,NULL);
}
static bool coordinates(float x,float z) {
    return fabsf(x)<1e9f && fabsf(z)<1e9f;
}
static int block_coord(int vertex) {
    int r=vertex%SIDE;
    return vertex/SIDE-(r<0);
}

static int canonical_x(const MacroWorld *w,int cx) {
    int width=w->equator_chunks,half=width/2;
    if (width<=0 || (cx>=-half && cx<width-half) || qlcg_world_flat(w)) return cx;
    cx%=width;
    if (cx< -half) cx+=width;
    if (cx>=width-half) cx-=width;
    return cx;
}
static Block *world_block(const MacroWorld *w,int cx,int cz) {
    return find_block(w->chunk_deltas,canonical_x(w,cx),cz);
}

static bool reserve_stroke(MacroWorld *w,int x0,int z0,int x1,int z1) {
    int xs[MAX_STROKE_BLOCKS],zs[MAX_STROKE_BLOCKS];
    Block *blocks[MAX_STROKE_BLOCKS];
    unsigned count=0;
    for(int cz=block_coord(z0);cz<=block_coord(z1);cz++)
        for(int cx=block_coord(x0);cx<=block_coord(x1);cx++) {
            if (!qlcg_world_flat(w) && w->equator_chunks>0 && cx-block_coord(x0)>=w->equator_chunks) break;
            int key=canonical_x(w,cx);
            if(find_block(w->chunk_deltas,key,cz)) continue;
            if(count==MAX_STROKE_BLOCKS) return false;
            xs[count]=key;zs[count++]=cz;
        }
    if(!count) return true;
    if(!reserve_blocks(&w->chunk_deltas,count)) return false;
    for(unsigned i=0;i<count;i++) {
        blocks[i]=calloc(1,sizeof(*blocks[i]));
        if(!blocks[i]) {
            while(i) free(blocks[--i]);
            return false;
        }
        blocks[i]->x=xs[i];blocks[i]->z=zs[i];
    }
    for(unsigned i=0;i<count;i++) insert_block(w->chunk_deltas,blocks[i]);
    return true;
}
static const Sample *at_vertex(const MacroWorld *w,int gx,int gz) {
    int cx=block_coord(gx),cz=block_coord(gz);
    const Block *b=world_block(w,cx,cz);
    return b?&b->samples[(gz-cz*SIDE)*SIDE+gx-cx*SIDE]:NULL;
}
static int limited(int v,int low,int high) {
    return v<low?low:v>high?high:v;
}
struct ChdLotSnapshot { Block block; };
ChdLotSnapshot *qlcg_chd_lot_snapshot(const MacroWorld *w,int cx,int cz) {
    ChdLotSnapshot *snapshot=calloc(1,sizeof(*snapshot));
    if(!snapshot) return NULL;
    const Block *b=world_block(w,cx,cz);
    if(b) {
        snapshot->block=*b;
        snapshot->block.props=NULL;
        snapshot->block.capacity=b->count;
        if(b->count) {
            snapshot->block.props=malloc(b->count*sizeof(Prop));
            if(!snapshot->block.props) {free(snapshot);return NULL;}
            memcpy(snapshot->block.props,b->props,b->count*sizeof(Prop));
        }
    }
    snapshot->block.x=canonical_x(w,cx);snapshot->block.z=cz;
    return snapshot;
}
void qlcg_chd_lot_dispose(ChdLotSnapshot *snapshot) {
    if(snapshot) {free(snapshot->block.props);free(snapshot);}
}
static void dirty_lot(MacroWorld *w,int cx,int cz) {
    uint32_t prior=w->surface_revision++;
    qlcg_world_surface_dirty(w,w->delta_version,prior,cx*32.0-2,cz*32.0-2,cx*32.0+34,cz*32.0+34,!qlcg_world_flat(w));
}
void qlcg_chd_lot_reset(MacroWorld *w,int cx,int cz) {
    Block *b=world_block(w,cx,cz);
    if(!b) return;
    free(b->props);
    *b=(Block){.x=b->x,.z=b->z};
    dirty_lot(w,cx,cz);
}
bool qlcg_chd_lot_swap(MacroWorld *w,int cx,int cz,ChdLotSnapshot *snapshot) {
    Block *b=ensure_block(&w->chunk_deltas,canonical_x(w,cx),cz);
    if(!b || !snapshot) return false;
    Block old=*b;*b=snapshot->block;snapshot->block=old;
    dirty_lot(w,cx,cz);return true;
}
bool qlcg_chd_lot_brush(MacroWorld *w,const RegionData *chunk,const ChdLotBrush *brush) {
    if(!w || !chunk || !chunk->heights || !brush || brush->radius<=0 || brush->radius>16 ||
       brush->material<0 || brush->material>6) return false;
    int cx=chunk->center_cx,cz=chunk->center_cz;
    Block *b=ensure_block(&w->chunk_deltas,canonical_x(w,cx),cz);
    if(!b) return false;
    float target=qlcg_region_height_at(chunk,brush->tool==LOT_FLAT?0:brush->x,brush->tool==LOT_FLAT?0:brush->z);
    bool changed=false;
    for(int iz=0;iz<SIDE;iz++) for(int ix=0;ix<SIDE;ix++) {
        float x=ix*2.f-16,z=iz*2.f-16;
        float dx=fabsf(x-brush->x),dz=fabsf(z-brush->z);
        float weight=fmaxf(0,1-(brush->square?fmaxf(dx,dz):hypotf(dx,dz))/brush->radius);
        if(brush->tool==LOT_RECT) weight=x>=fminf(brush->x,brush->end_x) && x<=fmaxf(brush->x,brush->end_x) &&
            z>=fminf(brush->z,brush->end_z) && z<=fmaxf(brush->z,brush->end_z);
        if(brush->tool==LOT_FLAT) weight=1;
        if(weight<=0) continue;
        Sample *s=b->samples+iz*SIDE+ix,old=*s;
        float edge=fminf(16-fabsf(x),16-fabsf(z));
        float h=qlcg_region_height_at(chunk,x,z),amount=brush->strength*weight;
        float dh=0;
        switch(brush->tool) {
        case LOT_RAISE: dh=amount;break;
        case LOT_LOWER: dh=-amount;break;
        case LOT_SMOOTH: {
            float mean=(qlcg_region_height_at(chunk,x-2,z)+qlcg_region_height_at(chunk,x+2,z)+
                        qlcg_region_height_at(chunk,x,z-2)+qlcg_region_height_at(chunk,x,z+2))*.25f;
            dh=(mean-h)*fminf(1,amount);break;
        }
        case LOT_RECT: case LOT_FLAT: dh=(target-h)*weight;break;
        case LOT_POND:
            dh=-amount*2;
            if(edge>=2) s->water=(int16_t)limited((int)s->height-(int)lroundf(amount*35),INT16_MIN,INT16_MAX);
            break;
        case LOT_FILL: s->water=0;dh=amount;break;
        case LOT_PAINT: s->material=(uint8_t)brush->material;break;
        case LOT_GRASS: case LOT_PATH: s->forest=(int8_t)limited(s->forest+(brush->tool==LOT_GRASS?25:-25),-100,100);break;
        }
        if(isfinite(dh)) s->height=(int16_t)limited(s->height+(int)lroundf(dh*100*fminf(1,edge*.25f)),INT16_MIN,INT16_MAX);
        changed|=memcmp(s,&old,sizeof(old))!=0;
    }
    if(changed) dirty_lot(w,cx,cz);
    return changed;
}
typedef struct { int32_t height,water; } LakeChange;
struct ChdLakeStroke {
    int x,z,width,rows;
    LakeChange changes[];
};
typedef struct { float ax,az,bx,bz,radius,meander; int water_cm; } Channel;
static bool edit_grid(MacroWorld *w,float x,float z,float radius,int height_cm,int substrate,
                      int forest_pct,ChdLakeStroke *lake,const Channel *channel) {
    if(!w || !coordinates(x,z) || !(radius>0 && radius<=128) || substrate<0 || substrate>5 ||
       height_cm<INT16_MIN || height_cm>INT16_MAX || forest_pct < -100 || forest_pct>100) return false;
    if(!lake && !height_cm && !substrate && !forest_pct) return false;
    int x0=lake?lake->x:(int)floor(((double)x-radius)/2);
    int z0=lake?lake->z:(int)floor(((double)z-radius)/2);
    int x1=lake?x0+lake->width-1:(int)ceil(((double)x+radius)/2);
    int z1=lake?z0+lake->rows-1:(int)ceil(((double)z+radius)/2);
    if(!reserve_stroke(w,x0,z0,x1,z1)) return false;
    bool changed=false;
    for(int gz=z0;gz<=z1;gz++) for(int gx=x0;gx<=x1;gx++) {
        double dx=gx*2.0-x,dz=gz*2.0-z;
        double weight;
        bool in_cell=false;
        if(channel) {
            float t;
            float distance=dist_to_seg2d(gx*2.f,gz*2.f,channel->ax,channel->az,channel->bx,channel->bz,&t);
            float bend=sinf(t*6.283185307f)*channel->meander*6;
            weight=fmax(0,1-fabsf(distance-bend)/channel->radius);
            weight*=weight;
        } else {
            weight=1.0-hypot(dx,dz)/radius;
            in_cell=hypot(dx+1,dz+1)<=radius;
        }
        if(weight<=0 && !in_cell) continue;
        int cx=block_coord(gx),cz=block_coord(gz);
        Block *b=world_block(w,cx,cz);
        Sample *s=&b->samples[(gz-cz*SIDE)*SIDE+gx-cx*SIDE];
        Sample next=*s;
        if(weight>0) next.height=(int16_t)limited(s->height+(int)lround(weight*height_cm),INT16_MIN,INT16_MAX);
        if(in_cell) {
            if(substrate) next.material=(uint8_t)substrate;
            next.forest=(int8_t)limited(s->forest+forest_pct,-100,100);
        }
        if(lake && weight>0) {
            int depth=(int)lround(weight*(channel?channel->water_cm:450));
            next.water=(int16_t)limited(next.height+depth,INT16_MIN,INT16_MAX);
            if(depth && !next.water) next.water=1;
            LakeChange *change=&lake->changes[(gz-z0)*lake->width+gx-x0];
            change->height=next.height-s->height;
            change->water=next.water-s->water;
        }
        if(memcmp(s,&next,sizeof(next))) { *s=next;changed=true; }
    }
    if(changed) {
        uint32_t prior=w->surface_revision++;
        qlcg_world_surface_dirty(w,w->delta_version,prior,x0*2.0-2,z0*2.0-2,x1*2.0+2,z1*2.0+2,!qlcg_world_flat(w));
    }
    return changed;
}
bool qlcg_chd_edit(MacroWorld *w,float x,float z,float radius,int height_cm,int substrate,int forest_pct) {
    return edit_grid(w,x,z,radius,height_cm,substrate,forest_pct,NULL,NULL);
}
static ChdLakeStroke *new_water_stroke(double min_x,double min_z,double max_x,double max_z) {
    int x0=(int)floor(min_x/2),z0=(int)floor(min_z/2);
    int width=1+(int)ceil(max_x/2)-x0,rows=1+(int)ceil(max_z/2)-z0;
    ChdLakeStroke *stroke=calloc(1,sizeof(*stroke)+(size_t)width*rows*sizeof(LakeChange));
    if(!stroke) return NULL;
    stroke->x=x0;stroke->z=z0;stroke->width=width;stroke->rows=rows;
    return stroke;
}
ChdLakeStroke *qlcg_chd_lake(MacroWorld *w,float x,float z,float radius) {
    if(!coordinates(x,z) || !(radius>0 && radius<=128)) return NULL;
    ChdLakeStroke *stroke=new_water_stroke((double)x-radius,(double)z-radius,(double)x+radius,(double)z+radius);
    if(stroke && edit_grid(w,x,z,radius,-1600,0,0,stroke,NULL)) return stroke;
    free(stroke);return NULL;
}
ChdLakeStroke *qlcg_chd_channel(MacroWorld *w,float ax,float az,float bx,float bz,
                               float width,float depth,float meander) {
    if(!w || !coordinates(ax,az) || !coordinates(bx,bz)) return NULL;
    if(!qlcg_world_flat(w) && w->circumference>0) bx=ax+remainderf(bx-ax,w->circumference);
    if(!(width>0 && width<=16) ||
       !(depth>0 && depth<=64) || !isfinite(meander) || fabsf(meander)>1 ||
       !(hypotf(bx-ax,bz-az)>0) || hypotf(bx-ax,bz-az)>128) return NULL;
    Channel channel={ax,az,bx,bz,width*1.4f,meander,(int)lroundf(depth*68)};
    double margin=channel.radius+fabs(meander)*6;
    ChdLakeStroke *stroke=new_water_stroke(fmin(ax,bx)-margin,fmin(az,bz)-margin,
                                           fmax(ax,bx)+margin,fmax(az,bz)+margin);
    if(stroke && edit_grid(w,ax,az,channel.radius,-(int)lroundf(depth*100),0,0,stroke,&channel)) return stroke;
    free(stroke);return NULL;
}
void qlcg_chd_undo_lake(MacroWorld *w,const ChdLakeStroke *stroke) {
    for(int z=0;z<stroke->rows;z++) for(int x=0;x<stroke->width;x++) {
        LakeChange change=stroke->changes[z*stroke->width+x];
        if(!change.height && !change.water) continue;
        int gx=stroke->x+x,gz=stroke->z+z,cx=block_coord(gx),cz=block_coord(gz);
        Block *b=world_block(w,cx,cz);
        if(!b) continue;
        Sample *s=&b->samples[(gz-cz*SIDE)*SIDE+gx-cx*SIDE];
        s->height=(int16_t)limited(s->height-change.height,INT16_MIN,INT16_MAX);
        s->water=(int16_t)limited(s->water-change.water,INT16_MIN,INT16_MAX);
    }
    uint32_t prior=w->surface_revision++;
    qlcg_world_surface_dirty(w,w->delta_version,prior,stroke->x*2.0-2,stroke->z*2.0-2,
        (stroke->x+stroke->width)*2.0,(stroke->z+stroke->rows)*2.0,!qlcg_world_flat(w));
}
void qlcg_chd_sample(const MacroWorld *w,float x,float z,float *height,float *water,float *forest) {
    if(!w->chunk_deltas || !coordinates(x,z)) return;
    int gx=(int)floor(x*.5),gz=(int)floor(z*.5);
    float tx=(float)(x*.5-gx),tz=(float)(z*.5-gz);
    float diagonal=fmaxf(0,tx+tz-1);
    float weights[4]={fmaxf(0,1-tx-tz),tx-diagonal,tz-diagonal,diagonal};
    float dh=0,dw=0;
    for(int i=0;i<4;i++) {
        float weight=weights[i];
        if(i && weight==0) continue;
        const Sample *s=at_vertex(w,gx+(i%2),gz+(i/2));
        if(!s) continue;
        dh+=weight*s->height;dw+=weight*s->water;
        if(!i && s->forest && forest) *forest=fminf(1,fmaxf(0,*forest+s->forest*.01f));
    }
    if(dw!=0 && water) *water=fmaxf(*water,*height+dw*.01f);
    if(dh!=0) *height+=dh*.01f;
}
int qlcg_chd_material(const MacroWorld *w,float x,float z) {
    if(!w->chunk_deltas || !coordinates(x,z)) return 0;
    const Sample *s=at_vertex(w,(int)floor(x*.5),(int)floor(z*.5));
    return s?s->material:0;
}
static int compare_props(const void *a,const void *b) {
    const Prop *p=a,*q=b;
    if(p->x!=q->x) return (int)p->x-q->x;
    if(p->z!=q->z) return (int)p->z-q->z;
    return (p->kind>q->kind)-(p->kind<q->kind);
}
static int prop_index(const Block *b,const Prop *key) {
    int lo=0,hi=b?(int)b->count:0;
    while(lo<hi) {
        int mid=lo+(hi-lo)/2;
        if(compare_props(b->props+mid,key)<0) lo=mid+1;
        else hi=mid;
    }
    if(b && (unsigned)lo<b->count && !compare_props(b->props+lo,key)) return lo;
    return -1-lo;
}
static Prop prop_key(float x,float z,int kind,int *cx,int *cz) {
    double px=round(x*100.0),pz=round(z*100.0);
    *cx=(int)floor(px/3200.0);*cz=(int)floor(pz/3200.0);
    return (Prop){(uint16_t)(px-3200.0*(*cx)),(uint16_t)(pz-3200.0*(*cz)),kind,0};
}
static int raw_state(const MacroWorld *w,float x,float z,int kind) {
    if(!w || !w->chunk_deltas || !coordinates(x,z)) return 0;
    int cx,cz;
    Prop p=prop_key(x,z,kind,&cx,&cz);
    const Block *b=world_block(w,cx,cz);
    int i=prop_index(b,&p);
    return i<0?0:b->props[i].state;
}
static int planted_state(int state) {
    if(state<CHD_PROP_PLANTED) return 0;
    return state>=CHD_PROP_REPLANTED?state-2:state;
}
int qlcg_chd_planted_state(const MacroWorld *w,float x,float z,int kind) {
    return planted_state(raw_state(w,x,z,kind));
}
int qlcg_chd_prop_state(const MacroWorld *w,float x,float z,int kind) {
    int state=raw_state(w,x,z,kind);
    if(state>=CHD_PROP_REPLANTED) return CHD_PROP_REMOVED;
    if(state==CHD_PROP_PLANTED) return 0;
    return state==CHD_PROP_PLANTED_WITHERED?CHD_PROP_WITHERED:state;
}
void qlcg_chd_visit_planted(const MacroWorld *w,int cx,int cz,ChdPropVisit visit,void *context) {
    const Block *b=w?world_block(w,cx,cz):NULL;
    if(!b || !visit) return;
    for(unsigned i=0;i<b->count;i++) {
        const Prop *p=&b->props[i];
        if(p->state>=CHD_PROP_PLANTED && p->kind!=CHD_PROP_WORKBENCH) {
            int visible=planted_state(p->state);
            visit((float)(cx*32.0+p->x*.01),(float)(cz*32.0+p->z*.01),p->kind,visible,context);
        }
    }
}
unsigned qlcg_chd_workbench_count(const MacroWorld *w,int cx,int cz) {
    const Block *b=w?world_block(w,cx,cz):NULL;
    return b?b->workbenches:0;
}
void qlcg_chd_visit_workbenches(const MacroWorld *w,int cx,int cz,ChdPropVisit visit,void *context) {
    const Block *b=w?world_block(w,cx,cz):NULL;
    if(!b || !b->workbenches || !visit) return;
    for(unsigned i=0;i<b->count;i++) {
        const Prop *p=&b->props[i];
        if(p->kind==CHD_PROP_WORKBENCH && p->state==CHD_PROP_PLANTED)
            visit((float)(cx*32.0+p->x*.01),(float)(cz*32.0+p->z*.01),p->kind,p->state,context);
    }
}
static bool change_prop(MacroWorld *w,float x,float z,int kind,int state) {
    if(!w || !coordinates(x,z) || kind<=0) return false;
    int cx,cz;
    Prop p=prop_key(x,z,kind,&cx,&cz);
    Block *b=state?ensure_block(&w->chunk_deltas,canonical_x(w,cx),cz):world_block(w,cx,cz);
    if(!b) return false;
    int i=prop_index(b,&p);
    if(i>=0) {
        int old=b->props[i].state;
        if(state==CHD_PROP_PLANTED) {
            if(old!=CHD_PROP_REMOVED) return false;
            state=CHD_PROP_REPLANTED;
        } else if(old>=CHD_PROP_PLANTED) {
            if(state==CHD_PROP_REMOVED) state=old>=CHD_PROP_REPLANTED?CHD_PROP_REMOVED:0;
            else if(state==CHD_PROP_WITHERED)
                state=old>=CHD_PROP_REPLANTED?CHD_PROP_REPLANTED_WITHERED:CHD_PROP_PLANTED_WITHERED;
        }
    }
    if(!state) {
        if(i<0) return false;
        b->count--;
        memmove(b->props+i,b->props+i+1,(b->count-i)*sizeof(p));
    } else if(i>=0) {
        if(b->props[i].state==state) return false;
        b->props[i].state=(uint8_t)state;
    } else {
        if(b->count>=UINT16_MAX) return false;
        if(b->count==b->capacity) {
            unsigned cap=b->capacity?b->capacity*2:4;
            Prop *next=realloc(b->props,cap*sizeof(*next));
            if(!next) return false;
            b->props=next;b->capacity=cap;
        }
        i=-1-i;
        memmove(b->props+i+1,b->props+i,(b->count-i)*sizeof(p));
        p.state=(uint8_t)state;b->props[i]=p;b->count++;
    }
    uint32_t prior=w->surface_revision++;
    qlcg_world_surface_dirty(w,w->delta_version,prior,x-2,z-2,x+2,z+2,!qlcg_world_flat(w));
    return true;
}

bool qlcg_chd_set_prop(MacroWorld *w,float x,float z,int kind,int state) {
    return kind!=CHD_PROP_WORKBENCH && state>=0 && state<=2 && change_prop(w,x,z,kind,state);
}
bool qlcg_chd_plant(MacroWorld *w,float x,float z,int kind) {
    return kind!=CHD_PROP_WORKBENCH && change_prop(w,x,z,kind,CHD_PROP_PLANTED);
}
bool qlcg_chd_place_workbench(MacroWorld *w,float x,float z) {
    int cx,cz;
    if(!coordinates(x,z)) return false;
    Prop key=prop_key(x,z,CHD_PROP_WORKBENCH,&cx,&cz);
    if(key.x%200!=100 || key.z%200!=100 || !change_prop(w,x,z,CHD_PROP_WORKBENCH,CHD_PROP_PLANTED)) return false;
    world_block(w,cx,cz)->workbenches++;
    return true;
}
static bool occupied(Sample s) {
    return s.height || s.water || s.material || s.forest;
}
static unsigned sample_count(const Block *b) {
    unsigned count=0;
    for(int i=0;i<CELLS;i++) count+=occupied(b->samples[i]);
    return count;
}
static int signed_bits(uint32_t n,int bits) {
    return (n & (1u<<(bits-1)))?(int)n-(1<<bits):(int)n;
}
bool qlcg_chd_write(FILE *file,const MacroWorld *w) {
    long begin=qlcg_section_begin(file,"CHD1");
    if(begin<0) return false;
    QlCodec io=qlcg_codec(file,false,QLCG_FILE_LIMIT);
    const ChunkDeltaTable *t=w->chunk_deltas;
    unsigned blocks=0;
    if(t) for(unsigned i=0;i<t->count;i++)
        if(t->blocks[i]->count || sample_count(t->blocks[i])) blocks++;
    qlcg_io_uint(&io,blocks,4);
    if(t) for(unsigned k=0;k<t->count && io.ok;k++) {
        const Block *b=t->blocks[k];
        unsigned count=sample_count(b);
        if(!count && !b->count) continue;
        qlcg_io_i32(&io,b->x);qlcg_io_i32(&io,b->z);
        qlcg_io_uint(&io,count,2);qlcg_io_uint(&io,b->count,2);
        for(unsigned i=0;i<CELLS;i++) if(occupied(b->samples[i])) {
            Sample s=b->samples[i];
            qlcg_io_uint(&io,i,1);qlcg_io_uint(&io,(uint16_t)s.height,2);
            qlcg_io_uint(&io,(uint16_t)s.water,2);qlcg_io_uint(&io,s.material,1);
            qlcg_io_uint(&io,(uint8_t)s.forest,1);
        }
        for(unsigned i=0;i<b->count;i++) {
            Prop p=b->props[i];
            qlcg_io_uint(&io,p.x,2);qlcg_io_uint(&io,p.z,2);
            qlcg_io_i32(&io,p.kind);qlcg_io_uint(&io,p.state,1);
        }
    }
    return io.ok && qlcg_section_finish(file,begin);
}

static bool merge_props(Block *b,Prop *incoming,unsigned count,bool prefer_incoming) {
    if (!b->count) {
        b->props=incoming;
        b->count=b->capacity=count;
    } else {
        Prop *merged=malloc((count+b->count)*sizeof(*merged));
        if (!merged) { free(incoming); return false; }
        unsigned i=0,j=0,total=0;
        while (i<b->count || j<count) {
            if (i==b->count) merged[total++]=incoming[j++];
            else if (j==count) merged[total++]=b->props[i++];
            else {
                int cmp=compare_props(b->props+i,incoming+j);
                if (cmp<0) merged[total++]=b->props[i++];
                else if (cmp>0) merged[total++]=incoming[j++];
                else { merged[total++]=prefer_incoming?incoming[j]:b->props[i]; i++; j++; }
            }
        }
        free(incoming);
        if (total>UINT16_MAX) { free(merged); return false; }
        free(b->props);
        b->props=merged;
        b->count=b->capacity=total;
    }
    b->workbenches=0;
    for (unsigned i=0;i<b->count;i++) b->workbenches+=b->props[i].kind==CHD_PROP_WORKBENCH;
    return true;
}

static bool read_props(QlCodec *io,Block *b,unsigned count,bool prefer_incoming) {
    if(!count) return true;
    Prop *props=calloc(count,sizeof(*props));
    if(!props) return false;
    for(unsigned i=0;i<count && io->ok;i++) {
        props[i].x=(uint16_t)qlcg_io_uint(io,0,2);
        props[i].z=(uint16_t)qlcg_io_uint(io,0,2);
        props[i].kind=qlcg_io_i32(io,0);
        props[i].state=(uint8_t)qlcg_io_uint(io,0,1);
        if(props[i].x>=3200 || props[i].z>=3200 || props[i].kind<=0 ||
           !props[i].state || props[i].state>CHD_PROP_REPLANTED_WITHERED) io->ok=false;
        if(props[i].kind==CHD_PROP_WORKBENCH) {
            if(props[i].state!=CHD_PROP_PLANTED || props[i].x%200!=100 || props[i].z%200!=100) io->ok=false;
        }
    }
    if(io->ok) {
        for(unsigned i=1;i<count;i++) if(compare_props(props+i-1,props+i)>=0) io->ok=false;
    }
    if(b && io->ok) io->ok=merge_props(b,props,count,prefer_incoming);
    else free(props);
    return io->ok;
}
bool qlcg_chd_read(FILE *file,long limit,MacroWorld *w) {
    long end;
    if(!qlcg_section_open(file,"CHD1",limit,&end)) return false;
    QlCodec io=qlcg_codec(file,true,end-ftell(file));
    unsigned count=(unsigned)qlcg_io_uint(&io,0,4);
    if(!io.ok || count>MAX_BLOCKS || count>(io.limit-io.bytes)/19) return false;
    ChunkDeltaTable *t=NULL;
    int prev_x=0,prev_z=0;
    for(unsigned k=0;k<count && io.ok;k++) {
        int x=qlcg_io_i32(&io,0),z=qlcg_io_i32(&io,0);
        unsigned n=(unsigned)qlcg_io_uint(&io,0,2),props=(unsigned)qlcg_io_uint(&io,0,2);
        if((k && (z<prev_z || (z==prev_z && x<=prev_x))) || n>CELLS ||
            (!n && !props) || !io.ok || n*7u+props*9u>io.limit-io.bytes) { io.ok=false;break; }
        prev_x=x;prev_z=z;
        int key=w?canonical_x(w,x):x;
        Block *b=w?ensure_block(&t,key,z):NULL;
        if(w && !b) { io.ok=false;break; }
        int previous=-1;
        for(unsigned j=0;j<n && io.ok;j++) {
            int i=(int)qlcg_io_uint(&io,0,1);
            Sample s;
            s.height=(int16_t)signed_bits((uint32_t)qlcg_io_uint(&io,0,2),16);
            s.water=(int16_t)signed_bits((uint32_t)qlcg_io_uint(&io,0,2),16);
            s.material=(uint8_t)qlcg_io_uint(&io,0,1);
            s.forest=(int8_t)signed_bits((uint32_t)qlcg_io_uint(&io,0,1),8);
            if(i<=previous || !occupied(s) || s.material>6 || s.forest < -100 || s.forest>100) io.ok=false;
            previous=i;
            if(b) {
                Sample *dest=b->samples+i;
                dest->height=(int16_t)limited(dest->height+s.height,INT16_MIN,INT16_MAX);
                dest->forest=(int8_t)limited(dest->forest+s.forest,-100,100);
                if (s.water && (x==key || !dest->water)) dest->water=s.water;
                if (s.material && (x==key || !dest->material)) dest->material=s.material;
            }
        }
        if(io.ok) io.ok=read_props(&io,b,props,x==key);
    }
    if(!io.ok || io.bytes!=io.limit) { release_table(t);return false; }
    if(w) install_table(w,t);
    return true;
}
