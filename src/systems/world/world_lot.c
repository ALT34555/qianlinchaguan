#include "world_lot.h"
#include "model/editor_buildings.h"
#include "world_claims.h"
#include "model/plant_material.h"
#include "world_settlement.h"
#include "core/binary.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static QlcgLotEntry **entry_at(QlcgLotEntry **head,int cx,int cz) {
    while(*head && ((*head)->cz<cz || ((*head)->cz==cz && (*head)->cx<cx))) head=&(*head)->next;
    return head;
}
const QlcgLot *qlcg_lot_local(const MacroWorld *w,int cx,int cz) {
    if(!w || !w->lots || !qlcg_claims_canonical(w,&cx,cz)) return NULL;
    for(const QlcgLotEntry *e=w->lots;e && e->cz<=cz;e=e->next)
        if(e->cx==cx && e->cz==cz) return &e->lot;
    return NULL;
}
const QlcgLot *qlcg_lot_at(const MacroWorld *w,int cx,int cz) {
    QlcgCivTerritory t;
    if(!w || (!w->lots && !w->planes) || !qlcg_civ_territory_at(w,cx,cz,&t)) return NULL;
    return qlcg_lot_local(t.owner,t.cx,t.cz);
}
bool qlcg_lot_copy(QlcgLot *out,const QlcgLot *src) {
    QlcgLot copy=src?*src:(QlcgLot){.type=LOT_LAND};
    copy.parts=NULL;
    if(copy.count) {
        copy.parts=malloc((size_t)copy.count*sizeof(*copy.parts));
        if(!copy.parts) return false;
        memcpy(copy.parts,src->parts,(size_t)copy.count*sizeof(*copy.parts));
    }
    *out=copy;
    return true;
}
static bool valid(const QlcgLot *lot) {
    if(lot->type!=LOT_CUSTOM && lot->type!=LOT_PRESET && lot->type!=LOT_LAND &&
        (lot->type<9010 || lot->type>9019)) return false;
    if(lot->pack<0 || lot->pack>=qlcg_lot_style_count() || lot->turn<0 || lot->turn>3 ||
       !(fabsf(lot->flag_x)<=16 && fabsf(lot->flag_z)<=16) || lot->count<0 || lot->count>UINT16_MAX ||
       (lot->count && (!lot->parts || (lot->type!=LOT_CUSTOM && lot->type!=LOT_PRESET)))) return false;
    for(int i=0;i<lot->count;i++) {
        const QlcgSettlementPart *p=lot->parts+i;
        if(!qlcg_lot_model(p->model) || !(fabsf(p->x)<=16 && fabsf(p->z)<=16 && fabsf(p->y)<=64 &&
           fabsf(p->yaw)<=16 && p->scale>=.05f && p->scale<=4) || p->slot < -1 || p->slot>15 ||
           (p->flags&~(QLCG_PART_FLAT|QLCG_PART_SLOPE)) || p->parent < -1 || p->parent>=i) return false;
    }
    return true;
}
bool qlcg_lot_commit(MacroWorld *w,int cx,int cz,const QlcgLot *lot) {
    QlcgCivTerritory t;
    if(!w || w->opt.play_mode!=PLAY_MODE_LEISURE || !w->opt.artificial_enabled ||
       !qlcg_civ_territory_at(w,cx,cz,&t) || !t.faction->is_player ||
       !qlcg_claims_canonical(t.owner,&t.cx,t.cz) || (lot && !valid(lot))) return false;
    MacroWorld *owner=(MacroWorld *)t.owner;
    QlcgLotEntry **slot=entry_at(&owner->lots,t.cx,t.cz);
    QlcgLotEntry *e=*slot;
    bool found=e && e->cx==t.cx && e->cz==t.cz;
    if(lot) {
        QlcgLot copy;
        if(!qlcg_lot_copy(&copy,lot)) return false;
        if(!found) {
            e=calloc(1,sizeof(*e));
            if(!e) { free(copy.parts);return false; }
            e->next=*slot;*slot=e;e->cx=t.cx;e->cz=t.cz;e->faction=t.faction->id;
        }
        free(e->lot.parts);
        copy.revision=owner->territory_revision+1;
        e->lot=copy;
    } else if(found) {
        *slot=e->next;free(e->lot.parts);free(e);
    }
    owner->territory_revision++;
    if(w!=owner) w->territory_revision++;
    return true;
}
bool qlcg_lot_preset(QlcgLot *lot,int pack,uint32_t seed,int turn) {
    if(pack>=qlcg_camp_pack_count()) return qlcg_extra_preset(lot,pack,seed,turn);
    QlcgCampLayout layout;
    if(!qlcg_camp_generate(pack,seed,&layout)) return false;
    float angle=turn*1.57079632679f,c=cosf(angle),s=sinf(angle);
    for(int i=0;i<layout.count;i++) {
        QlcgSettlementPart *p=layout.parts+i;
        float x=p->x,z=p->z;
        p->x=c*x-s*z;p->z=s*x+c*z;p->yaw+=angle;
    }
    QlcgLot src={.type=LOT_PRESET,.pack=pack,.turn=turn,.seed=seed,.count=layout.count,.parts=layout.parts};
    return qlcg_lot_copy(lot,&src);
}
typedef struct { float x,z,rx,rz,c,s; } Footprint;
static Footprint footprint(const QlcgSettlementPart *p) {
    const QlcgBuildingModel *m=qlcg_lot_model(p->model);
    const double *lo=m->bounds_min,*hi=m->bounds_max;
    float x=(float)(hi[0]+lo[0])*.5f*p->scale,z=(float)(hi[2]+lo[2])*.5f*p->scale;
    float c=cosf(p->yaw),s=sinf(p->yaw);
    if((m->kind==QLCG_BUILDING_ROOM || (m->kind==QLCG_BUILDING_YARD && hi[0]-lo[0]>=7.6 && hi[2]-lo[2]>=7.6)) && m->plot_size[0]>0 && m->plot_size[2]>0)
        return (Footprint){p->x,p->z,(float)m->plot_size[0]*.5f*p->scale,(float)m->plot_size[2]*.5f*p->scale,c,s};
    return (Footprint){p->x+c*x-s*z,p->z+s*x+c*z,(float)(hi[0]-lo[0])*.5f*p->scale,
        (float)(hi[2]-lo[2])*.5f*p->scale,c,s};
}
static float extent(Footprint a,float x,float z) {
    return a.rx*fabsf(a.c*x+a.s*z)+a.rz*fabsf(-a.s*x+a.c*z);
}
static bool overlap(Footprint a,Footprint b) {
    float axes[4][2]={{a.c,a.s},{-a.s,a.c},{b.c,b.s},{-b.s,b.c}};
    for(int i=0;i<4;i++) {
        float x=axes[i][0],z=axes[i][1];
        if(fabsf((a.x-b.x)*x+(a.z-b.z)*z)>=extent(a,x,z)+extent(b,x,z)-.01f) return false;
    }
    return true;
}
bool qlcg_lot_placeable(const QlcgLot *lot,const QlcgSettlementPart *p,int ignore) {
    if(!lot || !p || !qlcg_lot_model(p->model)) return false;
    Footprint a=footprint(p);
    if(fabsf(a.x)+extent(a,1,0)>16.01f || fabsf(a.z)+extent(a,0,1)>16.01f) return false;
    for(int i=0;i<lot->count;i++) {
        if(i==ignore || i==p->parent || (ignore>=0 && lot->parts[i].parent==ignore) ||
            (p->parent>=0 && lot->parts[i].parent==p->parent)) continue;
        if(overlap(a,footprint(lot->parts+i))) return false;
    }
    return true;
}
int qlcg_lot_pick(const QlcgLot *lot,float x,float z,float pad) {
    if(lot) for(int i=lot->count-1;i>=0;i--) {
        Footprint a=footprint(lot->parts+i);
        float dx=x-a.x,dz=z-a.z;
        if(fabsf(dx*a.c+dz*a.s)<=a.rx+pad && fabsf(-dx*a.s+dz*a.c)<=a.rz+pad) return i;
    }
    return -1;
}
bool qlcg_lot_socket(const QlcgSettlementPart *room,QlcgSettlementPart *door) {
    const QlcgBuildingModel *m=qlcg_lot_model(room->model);
    for(int j=0;j<m->socket_count;j++) {
        const QlcgBuildingSocket *socket=m->sockets+j;
        if(strcmp(socket->name,"socket_door")) continue;
        float c=cosf(room->yaw),s=sinf(room->yaw);
        const double *p=socket->position,*q=socket->quaternion;
        door->x=room->x+(float)(c*p[0]-s*p[2])*room->scale;
        door->z=room->z+(float)(s*p[0]+c*p[2])*room->scale;
        door->y=room->y+(float)p[1]*room->scale;door->scale=room->scale;
        door->yaw=room->yaw-(float)atan2(2*(q[3]*q[1]+q[0]*q[2]),1-2*(q[1]*q[1]+q[2]*q[2]));
        return true;
    }
    return false;
}
bool qlcg_lot_door(const QlcgLot *lot,QlcgSettlementPart *door) {
    float best=4;QlcgSettlementPart result=*door;
    for(int i=0;i<lot->count;i++) {
        QlcgSettlementPart p=*door;
        if(!qlcg_lot_socket(lot->parts+i,&p)) continue;
        float distance=hypotf(p.x-door->x,p.z-door->z);
        if(distance<best) {best=distance;p.parent=i;result=p;}
    }
    *door=result;return best<4;
}
bool qlcg_lot_put(QlcgLot *lot,QlcgSettlementPart part,int index) {
    if(!lot || !qlcg_lot_model(part.model)) return false;
    if(index< -1 || index>=lot->count || part.parent>=lot->count || part.parent< -1) return false;
    if(part.parent>=0) {
        if(qlcg_lot_model(part.model)->kind!=QLCG_BUILDING_DOOR ||
           qlcg_lot_model(lot->parts[part.parent].model)->kind!=QLCG_BUILDING_ROOM) return false;
        for(int i=0;i<lot->count;i++) if(lot->parts[i].parent==part.parent && i!=index) {
            if(index>=0) return false;
            index=i;break;
        }
    }
    if(!qlcg_lot_placeable(lot,&part,index)) return false;
    if(index>=0 && part.parent>=index) {
        /* 重新挂载的门序列化时紧随新父级 */
        memmove(lot->parts+index,lot->parts+index+1,(lot->count-index-1)*sizeof(part));
        for(int i=index;i<lot->count-1;i++) if(lot->parts[i].parent>index) lot->parts[i].parent--;
        part.parent--;lot->parts[lot->count-1]=part;return true;
    }
    if(index>=0) for(int i=index+1;i<lot->count;i++) if(lot->parts[i].parent==index) {
        QlcgSettlementPart child=lot->parts[i];
        if(!qlcg_lot_socket(&part,&child) || !qlcg_lot_placeable(lot,&child,i)) return false;
    }
    if(index<0) {
        if(lot->count==UINT16_MAX) return false;
        QlcgSettlementPart *parts=realloc(lot->parts,(lot->count+1)*sizeof(*parts));
        if(!parts) return false;
        lot->parts=parts;index=lot->count++;
    }
    lot->parts[index]=part;
    for(int i=index+1;i<lot->count;i++) if(lot->parts[i].parent==index) qlcg_lot_socket(&part,lot->parts+i);
    return true;
}
void qlcg_lot_remove(QlcgLot *lot,int index) {
    if(index<0 || index>=lot->count) return;
    for(int i=lot->count-1;i>index;i--) if(lot->parts[i].parent==index) qlcg_lot_remove(lot,i);
    memmove(lot->parts+index,lot->parts+index+1,(lot->count-index-1)*sizeof(*lot->parts));
    lot->count--;
    for(int i=index;i<lot->count;i++) if(lot->parts[i].parent>index) lot->parts[i].parent--;
}
static QlcgSettlementPart relative_part(const MacroWorld *w,const TerritoryFaction *f,const QlcgLotEntry *e,int i) {
    QlcgSettlementPart p=e->lot.parts[i];
    float dx=(float)(e->cx-f->cx)*CHUNK_SIZE;
    if(!qlcg_world_flat(w) && w->circumference>0) dx=remainderf(dx,w->circumference);
    p.x+=dx;p.z+=(e->cz-f->cz)*CHUNK_SIZE;p.parent=-1;return p;
}
int qlcg_lot_homes(const MacroWorld *w,const TerritoryFaction *f,QlcgSettlementPart *out,int cap) {
    int n=0;
    for(const QlcgLotEntry *e=w->lots;e && n<cap;e=e->next) if(e->faction==f->id)
        for(int i=0;i<e->lot.count && n<cap;i++)
            if(qlcg_lot_model(e->lot.parts[i].model)->kind==QLCG_BUILDING_ROOM) out[n++]=relative_part(w,f,e,i);
    return n;
}
bool qlcg_lot_facility(const MacroWorld *w,const TerritoryFaction *f,bool tea,QlcgSettlementPart *out) {
    float best=INFINITY;
    if(!w || !f) return false;
    for(const QlcgLotEntry *e=w->lots;e;e=e->next) if(e->faction==f->id)
        for(int i=0;i<e->lot.count;i++) {
            const QlcgSettlementPart *obj=e->lot.parts+i;
            const QlcgBuildingModel *m=qlcg_lot_model(obj->model);
            const QlcgBuildingSocket *fire=NULL;
            if(tea) {if(!qlcg_lot_is_seat(obj->model)) continue;}
            else {
                for(int j=0;j<m->socket_count;j++) if(!strcmp(m->sockets[j].name,"socket_fire")) {fire=m->sockets+j;break;}
                if(!fire && !strstr(m->unit_id,"campfire") && !strstr(m->unit_id,"stove")) continue;
            }
            QlcgSettlementPart p=relative_part(w,f,e,i);
            if(fire) {
                const double *v=fire->position;float c=cosf(p.yaw),s=sinf(p.yaw);
                p.x+=(float)(c*v[0]-s*v[2])*p.scale;p.z+=(float)(s*v[0]+c*v[2])*p.scale;
            }
            float d=p.x*p.x+p.z*p.z;
            if(d<best) {*out=p;best=d;}
        }
    return isfinite(best);
}
bool qlcg_lot_farm(const QlcgLot *lot,const QlcgCivTerritory *t,QlcgFarmPlot *out) {
    if(!lot || lot->type<9010 || lot->type>9019) return false;
    const QlcgCropType *crop=qlcg_crop_type_find(lot->crop);
    const QlcgFarmlandType *type=crop?qlcg_farmland_type_find(crop->farmland_id):NULL;
    if(!type || type->chunk_type!=lot->type) {
        type=NULL;
        for(int i=0;i<qlcg_farmland_type_count();i++) {
            const QlcgFarmlandType *item=qlcg_farmland_type_at(i);
            if(item->chunk_type==lot->type) {type=item;break;}
        }
    }
    const char *plant=crop?crop->plant_id:qlcg_plant_species_id(qlcg_plant_species_index(lot->crop));
    if(!type || !plant) return false;
    *out=(QlcgFarmPlot){.id=t->faction->id,.cx=t->cx,.cz=t->cz,.width=QLCG_FARMLAND_SIDE,.depth=QLCG_FARMLAND_SIDE,
        .yaw=lot->turn*1.57079632679f,.stage=3,.season=1,.crop_id=lot->crop,.farm_type_id=type->id,.plant_id=plant};
    return true;
}
void qlcg_lot_free(MacroWorld *w) {
    while(w->lots) {
        QlcgLotEntry *e=w->lots;w->lots=e->next;free(e->lot.parts);free(e);
    }
}

/* 坐标与角度采用定点小端厘米/厘度编码 */
static float fixed(QlCodec *io,float v,float units) {
    return (int16_t)qlcg_io_uint(io,(uint16_t)(int16_t)lroundf(v*units),2)/units;
}
static bool lot_codec(QlCodec *io,QlcgLotEntry *e) {
    QlcgLot *l=&e->lot;
    e->cx=qlcg_io_i32(io,e->cx);e->cz=qlcg_io_i32(io,e->cz);
    e->faction=(int)qlcg_io_uint(io,e->faction,2);
    /* 最高位标记含网格槽与地基标志的记录 */
    unsigned code=(unsigned)qlcg_io_uint(io,(unsigned)l->type|0x8000u,2);
    bool has_slot=(code&0x8000u)!=0;
    l->type=(int)(code&0x7fffu);l->pack=(int)qlcg_io_uint(io,l->pack,1);
    l->turn=(int)qlcg_io_uint(io,l->turn,1);l->seed=(uint32_t)qlcg_io_uint(io,l->seed,4);
    l->flag_moved=qlcg_io_bool(io,l->flag_moved);l->flag_x=fixed(io,l->flag_x,100);l->flag_z=fixed(io,l->flag_z,100);
    qlcg_io_string(io,l->crop,sizeof(l->crop));
    l->count=(int)qlcg_io_uint(io,l->count,2);
    if(io->reading && l->count) {
        if(!io->ok || (io->limit-io->bytes)/(has_slot?16:14)<(unsigned)l->count) return false;
        l->parts=calloc(l->count,sizeof(*l->parts));
        if(!l->parts) return false;
    }
    for(int i=0;io->ok && i<l->count;i++) {
        QlcgSettlementPart p=l->parts[i];
        p.model=(int)qlcg_io_uint(io,p.model,2);p.parent=(int)qlcg_io_uint(io,p.parent+1,2)-1;
        p.x=fixed(io,p.x,100);p.y=fixed(io,p.y,100);p.z=fixed(io,p.z,100);
        p.yaw=fixed(io,remainderf(p.yaw,6.28318530718f),5729.577951f);
        p.scale=fixed(io,p.scale,1000);
        if(has_slot) {
            unsigned slot=(unsigned)qlcg_io_uint(io,p.slot<0?255u:(unsigned)p.slot,1);
            p.slot=slot==255?-1:(int)slot;
            p.flags=(unsigned)qlcg_io_uint(io,p.flags,1);
        } else p.slot=-1;
        if(io->reading) l->parts[i]=p;
    }
    return io->ok && valid(l);
}
bool qlcg_lot_write(FILE *file,const MacroWorld *w) {
    long start=qlcg_section_begin(file,"LOT1");
    if(start<0) return false;
    QlCodec io=qlcg_codec(file,false,QLCG_FILE_LIMIT);
    for(const QlcgLotEntry *e=w->lots;e;e=e->next) {
        QlcgLotEntry copy=*e;
        if(!lot_codec(&io,&copy)) return false;
    }
    return qlcg_section_finish(file,start);
}
bool qlcg_lot_read(FILE *file,long limit,MacroWorld *w) {
    long end;
    if(!qlcg_section_open(file,"LOT1",limit,&end)) return false;
    QlCodec io=qlcg_codec(file,true,end-ftell(file));
    QlcgLotEntry *head=NULL,**tail=&head,*prev=NULL;
    bool ok=true;int count=0;
    while(io.bytes<io.limit && ok) {
        QlcgLotEntry *e=calloc(1,sizeof(*e));
        if(!e) {ok=false;break;}
        *tail=e;tail=&e->next;
        ok=++count<=QLCG_MAX_CHUNK_CLAIMS && lot_codec(&io,e) && e->faction<MAX_FACTIONS &&
           (!prev || prev->cz<e->cz || (prev->cz==e->cz && prev->cx<e->cx));
        if(ok && w) {
            const QlcgChunkClaim *claim=qlcg_claims_at(w,e->cx,e->cz);
            ok=claim && claim->faction==e->faction && claim->cx==e->cx;
        }
        prev=e;
    }
    if(ok && w) {qlcg_lot_free(w);w->lots=head;head=NULL;}
    while(head) {QlcgLotEntry *next=head->next;free(head->lot.parts);free(head);head=next;}
    return ok && io.ok;
}
