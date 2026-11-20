#include "editor_buildings.h"
#include "platform/platform.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int x,z,kind,fixed; float scale; } ExtraSlot;
typedef struct { int count; ExtraSlot slots[9]; } ExtraTemplate;
typedef struct {
    QlcgBuildingModel model;
    unsigned offset,vertices,indices;
} ExtraModel;
typedef struct {
    const char *id,*name;
    int first,count;
    const ExtraTemplate *templates;
    int template_count;
} ExtraPack;
#include "editor_buildings.inc"

#define COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))
static const ExtraPack *extra_pack(int style) {
    int pi=style-qlcg_camp_pack_count();
    return pi>=0 && pi<COUNT(packs)?packs+pi:NULL;
}
int qlcg_lot_style_count(void) {return qlcg_camp_pack_count()+COUNT(packs);}
const char *qlcg_lot_style_id(int style) {
    const ExtraPack *p=extra_pack(style);
    return p?p->id:qlcg_camp_pack_id(style);
}
const char *qlcg_lot_style_name(int style) {
    static const char *names[]={"原始营地","蛮族营地","耕作者营地","渔人营地"};
    const ExtraPack *p=extra_pack(style);
    return p?p->name:style>=0 && style<COUNT(names)?names[style]:"";
}
int qlcg_lot_style_unit(int style,int ordinal) {
    if(ordinal<0) return -1;
    const ExtraPack *p=extra_pack(style);
    if(p) return ordinal<p->count?LOT_EXT_BASE+(int)(p-packs)*100+ordinal:-1;
    const char *pack=qlcg_camp_pack_id(style);
    if(pack) for(int i=0;i<qlcg_building_model_count();i++) {
        if(!strcmp(qlcg_building_model_at(i)->pack,pack) && ordinal--==0) return i;
    }
    return -1;
}
static const ExtraModel *entry(int id) {
    if(id<LOT_EXT_BASE) return NULL;
    int pi=(id-LOT_EXT_BASE)/100,index=(id-LOT_EXT_BASE)%100;
    return pi<COUNT(packs) && index<packs[pi].count?models+packs[pi].first+index:NULL;
}
const QlcgBuildingModel *qlcg_extra_model(int id) {
    const ExtraModel *e=entry(id);return e?&e->model:NULL;
}
static uint16_t u16(const unsigned char *bytes) {return (uint16_t)(bytes[0]|((uint16_t)bytes[1]<<8));}
bool qlcg_extra_load(int id) {
    const ExtraModel *e=entry(id);
    if(!e) return id<LOT_EXT_BASE || (id>=LOT_TEA && id<LOT_MODEL_END);
    LpGeometry *g=meshes+(e-models);
    if(g->index_count) return true;
    FILE *file=qlcg_platform_fopen("content/data/world/editor_buildings.blm","rb");
    if(!file) return false;
    size_t size=12u*e->vertices+2u*e->indices;
    unsigned char magic[4],*data=malloc(size);
    bool ok=data && fread(magic,1,4,file)==4 && !memcmp(magic,"BLM1",4) &&
        !fseek(file,(long)e->offset,SEEK_SET) && fread(data,1,size,file)==size;
    fclose(file);
    if(!ok) {free(data);return false;}
    LpGeometry out={0};
    out.vertex_count=(int)e->vertices;out.triangle_count=(int)e->indices/3;out.index_count=(int)e->indices;
    out.positions=malloc(e->vertices*3*sizeof(float));out.normals=malloc(e->vertices*3*sizeof(float));
    out.colors=malloc(e->vertices*3);out.indices=malloc(e->indices*sizeof(*out.indices));
    ok=out.positions && out.normals && out.colors && out.indices;
    for(unsigned i=0;ok && i<e->vertices;i++) {
        const unsigned char *v=data+i*12;
        for(unsigned a=0;a<3;a++) {
            out.positions[3*i+a]=(int16_t)u16(v+2*a)*.001f;
            out.normals[3*i+a]=(int8_t)v[6+a]/127.f;out.colors[3*i+a]=v[9+a];
        }
    }
    for(unsigned i=0;ok && i<e->indices;i++) {
        unsigned v=u16(data+12*e->vertices+2*i);
        if(v>=e->vertices) ok=false;else out.indices[i]=v;
    }
    free(data);
    if(!ok) {qlcg_lp_geometry_free(&out);return false;}
    out.min_y=e->model.bounds_min[1];out.height=e->model.bounds_max[1]-out.min_y;
    *g=out;return true;
}
void qlcg_extra_free(void) {
    for(int i=0;i<COUNT(meshes);i++) qlcg_lp_geometry_free(meshes+i);
}
static int pick_unit(const ExtraPack *pack,int kind,LpRng *rng) {
    int ids[100],count=0;
    for(int i=0;i<pack->count;i++) {
        const QlcgBuildingModel *m=&models[pack->first+i].model;
        if(m->kind==kind && m->plot_size[0]<=9 && m->plot_size[2]<=9) ids[count++]=i;
    }
    return count?ids[(int)(qlcg_lp_rng_next(rng)*count)]:-1;
}
bool qlcg_extra_preset(QlcgLot *lot,int style,uint32_t seed,int turn) {
    const ExtraPack *pack=extra_pack(style);
    if(!pack) return false;
    LpRng rng;qlcg_lp_rng_seed(&rng,seed);
    const ExtraTemplate *layout=&pack->templates[(int)(qlcg_lp_rng_next(&rng)*pack->template_count)];
    QlcgLot next={.pack=style,.type=LOT_PRESET,.seed=seed,.turn=turn};
    next.parts=calloc(layout->count*2,sizeof(*next.parts));
    if(!next.parts) return false;
    float yaw=turn*1.57079632679f,c=cosf(yaw),s=sinf(yaw);
    for(int i=0;i<layout->count;i++) {
        const ExtraSlot *slot=layout->slots+i;
        int index=slot->fixed<0?pick_unit(pack,slot->kind,&rng):slot->fixed;
        if(index<0) {free(next.parts);return false;}
        float facing=0,nearest=INFINITY;
        if(slot->kind==QLCG_BUILDING_ROOM) for(int j=0;j<layout->count;j++) {
            const ExtraSlot *yard=layout->slots+j;
            if(yard->kind!=QLCG_BUILDING_YARD) continue;
            int dx=yard->x-slot->x,dz=yard->z-slot->z;
            float d=(float)(dx*dx+dz*dz);
            if(d<nearest) {nearest=d;facing=roundf(atan2f((float)-dx,(float)dz)/1.5707963f)*1.5707963f;}
        }
        int model=LOT_EXT_BASE+(int)(pack-packs)*100+index;
        next.parts[next.count++]=(QlcgSettlementPart){.model=model,.slot=i,.parent=-1,.scale=slot->scale,
            .yaw=yaw+facing,.x=c*slot->x-s*slot->z,.z=s*slot->x+c*slot->z};
    }
    int count=next.count;
    for(int i=0;i<count;i++) {
        int index=pick_unit(pack,QLCG_BUILDING_DOOR,&rng);
        if(index<0 || layout->slots[i].kind!=QLCG_BUILDING_ROOM) continue;
        QlcgSettlementPart door={.model=LOT_EXT_BASE+(int)(pack-packs)*100+index,.parent=i,.scale=1};
        if(qlcg_lot_socket(next.parts+i,&door)) next.parts[next.count++]=door;
    }
    *lot=next;return true;
}
