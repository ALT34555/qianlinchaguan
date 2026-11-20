#include "camp_layout.h"
#include "building_model.h"
#include <math.h>
#include <string.h>

typedef struct {
    const char *id;
    int counts[3],pools[3][16],masks[13],turns[16][4],positions[4][2],merge_scale;
} CampPack;
#include "camp_rules.inc"

int qlcg_camp_pack_count(void) {return (int)(sizeof(packs)/sizeof(packs[0]));}
const char *qlcg_camp_pack_id(int i) {return i>=0&&i<qlcg_camp_pack_count()?packs[i].id:NULL;}
static int pick(LpRng *rng,int count) {return (int)(qlcg_lp_rng_next(rng)*count);}

static int room_mask(const CampPack *pack,int growers,LpRng *rng) {
    if(!growers)return pack->masks[pick(rng,13)];
    int count=1+pick(rng,3);
    if(count==1)return 1;
    if(count==2)return pack->masks[4+pick(rng,4)];
    return pack->masks[8+pick(rng,3)];
}

int qlcg_camp_generate(int index,uint32_t seed,QlcgCampLayout *out) {
    if(!out||index<0||index>=qlcg_camp_pack_count())return 0;
    *out=(QlcgCampLayout){0};
    const CampPack *pack=packs+index;
    const int growers=!strcmp(pack->id,"house_0003"),fishers=!strcmp(pack->id,"house_0004");
    const int unique=growers||!strcmp(pack->id,"house_0002");
    const float quarter=1.5707963267948966f;
    LpRng rng;qlcg_lp_rng_seed(&rng,seed);
    out->mask=room_mask(pack,growers,&rng);
    int pool[3][16],left[3],yard_turn[4]={0};
    memcpy(pool,pack->pools,sizeof(pool));memcpy(left,pack->counts,sizeof(left));
    for(int slot=0;slot<4;slot++){
        int kind=(out->mask&(1<<slot))?QLCG_BUILDING_ROOM:QLCG_BUILDING_YARD;
        int choice=pick(&rng,left[kind]);
        out->units[slot]=pool[kind][choice];
        if(unique)pool[kind][choice]=pool[kind][--left[kind]];
        if(kind==QLCG_BUILDING_YARD&&!growers)yard_turn[slot]=pick(&rng,4);
    }
    int merged=out->mask==15;
    for(int kind=QLCG_BUILDING_ROOM;kind<=QLCG_BUILDING_YARD;kind++){
        for(int slot=0;slot<(merged?1:4);slot++){
            if(((out->mask>>slot)&1)!=(kind==QLCG_BUILDING_ROOM))continue;
            int turn=kind==QLCG_BUILDING_ROOM?pack->turns[out->mask][slot]:yard_turn[slot];
            out->parts[out->count++]=(QlcgSettlementPart){.model=out->units[slot],.slot=slot,.parent=-1,
                .x=merged?0:(float)pack->positions[slot][0],.z=merged?0:(float)pack->positions[slot][1],
                .yaw=-quarter*turn,.scale=merged?(float)pack->merge_scale:1};
        }
    }
    int shared=pool[QLCG_BUILDING_DOOR][pick(&rng,left[QLCG_BUILDING_DOOR])];
    int bases=out->count;
    for(int i=0;i<bases;i++){
        const QlcgSettlementPart *parent=out->parts+i;
        const QlcgBuildingModel *house=qlcg_building_model_at(parent->model);
        if(house->kind!=QLCG_BUILDING_ROOM)continue;
        for(int j=0;j<house->socket_count;j++){
            const QlcgBuildingSocket *socket=house->sockets+j;
            if(strcmp(socket->name,"socket_door"))continue;
            int door=growers||fishers?pool[2][pick(&rng,left[2])]:shared;
            const double *p=socket->position,*q=socket->quaternion;
            float c=cosf(parent->yaw),s=sinf(parent->yaw),scale=parent->scale;
            out->parts[out->count++]=(QlcgSettlementPart){.model=door,.slot=parent->slot,.parent=i,
                .x=parent->x+(float)(c*p[0]-s*p[2])*scale,.y=(float)p[1]*scale,
                .z=parent->z+(float)(s*p[0]+c*p[2])*scale,
                .yaw=parent->yaw-(float)atan2(2*(q[3]*q[1]+q[0]*q[2]),1-2*(q[1]*q[1]+q[2]*q[2])),.scale=scale};
            break;
        }
    }
    return out->count;
}
