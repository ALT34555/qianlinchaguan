#include "model/editor_buildings.h"
#include "world_lot.h"
#include "world_settlement.h"
#include "world_infinite.h"
#include "world_civ_economy.h"
#include "world_claims.h"
#include "world_delta.h"
#include "surface_stream.h"
#include "model/building_model.h"
#include <math.h>
#include <string.h>

int qlcg_settlement_claim_type(const MacroWorld *w,int cx,int cz) {
    if(w&&w->chunk_claims) {
        QlcgCivTerritory territory;
        return qlcg_civ_territory_at(w,cx,cz,&territory)?territory.type:0;
    }
    if(!w) return 0;
    double x=((double)cx+.5)*CHUNK_SIZE,z=((double)cz+.5)*CHUNK_SIZE,lx=x,lz=z;
    const MacroWorld *owner=qlcg_world_flat(w)?qlcg_infinite_resolve(w,x,z,&lx,&lz):w;
    if(!owner||!owner->claim_type) return 0;
    int cell=qlcg_world_cell_at_wxz(owner,(float)lx,(float)lz);
    return cell>=0&&cell<owner->cell_count?owner->claim_type[cell]:0;
}

const TerritoryFaction *qlcg_settlement_chunk(const MacroWorld *w,int cx,int cz) {
    if(!w||!w->opt.artificial_enabled) return NULL;
    if(w->chunk_claims) {
        QlcgCivTerritory territory;
        return qlcg_civ_territory_at(w,cx,cz,&territory)&&territory.type==QLCG_FLAG_CHUNK?territory.faction:NULL;
    }
    double x=((double)cx+.5)*CHUNK_SIZE,z=((double)cz+.5)*CHUNK_SIZE,lx=x,lz=z;
    const MacroWorld *owner=qlcg_world_flat(w)?qlcg_infinite_resolve(w,x,z,&lx,&lz):w;
    if(!owner||!owner->opt.artificial_enabled) return NULL;
    int fx=(int)floor(lx/CHUNK_SIZE),fz=(int)floor(lz/CHUNK_SIZE);
    int period=(int)lround(owner->circumference/CHUNK_SIZE);
    if(owner->claim_faction&&owner->columns>0&&owner->rows>0) {
        int cell=qlcg_world_cell_at_wxz(owner,(float)lx,(float)lz);
        int id=cell>=0&&cell<owner->cell_count?owner->claim_faction[cell]:-1;
        if(id>=0&&id<owner->faction_count) {
            const TerritoryFaction *f=owner->factions+id;int dx=fx-f->cx;
            if(f->active&&fz==f->cz&&(dx==0||(!qlcg_world_flat(owner)&&period>0&&dx%period==0))) return f;
        }
    }
    for(int i=0;i<owner->faction_count;i++) {
        const TerritoryFaction *f=owner->factions+i;
        int dx=fx-f->cx;
        if(f->active&&fz==f->cz&&(dx==0||(!qlcg_world_flat(owner)&&period>0&&dx%period==0))) return f;
    }
    return NULL;
}

static uint32_t settlement_seed(const TerritoryFaction *f) {
    uint32_t h=2166136261u;
    for(int i=0;i<16&&f->uid[i];i++) h=(h^(unsigned char)f->uid[i])*16777619u;
    h=(h^(uint32_t)f->cx)*16777619u;
    return (h^(uint32_t)f->cz)*16777619u;
}

float qlcg_settlement_yaw(const TerritoryFaction *f) {
    return (float)(settlement_seed(f)&3u)*1.57079632679f;
}

typedef struct {
    uint32_t seed;
    QlcgCampLayout layout;
} SettlementMemo;

int qlcg_settlement_layout(const TerritoryFaction *f,QlcgSettlementPart parts[QLCG_SETTLEMENT_PARTS]) {
    if(!f||!parts)return 0;
    static _Thread_local SettlementMemo cache[32];
    uint32_t seed=settlement_seed(f);
    SettlementMemo *entry=&cache[(seed>>8)%32];
    if(!entry->layout.count||entry->seed!=seed){
        int pack=(int)((seed>>16)%(uint32_t)qlcg_camp_pack_count());
        qlcg_camp_generate(pack,seed,&entry->layout);
        float yaw=(float)(seed&3u)*1.57079632679f,c=cosf(yaw),s=sinf(yaw);
        for(int i=0;i<entry->layout.count;i++){
            QlcgSettlementPart *p=entry->layout.parts+i;
            float x=p->x,z=p->z;
            p->x=x*c-z*s;p->z=x*s+z*c;p->yaw+=yaw;
        }
        entry->seed=seed;
    }
    memcpy(parts,entry->layout.parts,sizeof(entry->layout.parts));
    return entry->layout.count;
}

bool qlcg_settlement_cleared(const TerritoryFaction *f,float x,float z,float padding) {
    if(!f)return false;
    if((fabsf(x)<8.2f+padding&&fabsf(z)<8.2f+padding)||fabsf(x)<1.5f+padding||fabsf(z)<1.5f+padding)return true;
    float yaw=qlcg_settlement_yaw(f),east=x*cosf(yaw)+z*sinf(yaw);
    return east>=8-padding&&east<=QLCG_SETTLEMENT_PLAZA_X+3.5f+padding&&fabsf(z*cosf(yaw)-x*sinf(yaw))<=7+padding;
}

bool qlcg_settlement_base(const RegionData *chunk,float x,float z,float *height,float *dx,float *dz) {
    *height=qlcg_region_height_at(chunk,x,z);
    *dx=(qlcg_region_height_at(chunk,x+3,z)-qlcg_region_height_at(chunk,x-3,z))/6;
    *dz=(qlcg_region_height_at(chunk,x,z+3)-qlcg_region_height_at(chunk,x,z-3))/6;
    return isfinite(*height)&&isfinite(*dx)&&isfinite(*dz);
}



#include "model/yard_colliders.inc"

static const BuildingCollider primitive_hut[]={
    {0,-.18f,3.48f,3.55f,-.30f,1.08f,true},
    {0,-.1f,2.62f,2.65f,1.08f,2.58f,true},
    {0,0,1.37f,1.37f,2.58f,4.05f,true}
};
static const BuildingCollider barbarian_hut[]={
    {0,-.18f,3.62f,3.69f,-.30f,1.16f,true},
    {0,-.1f,2.74f,2.77f,1.16f,2.65f,true},
    {0,0,1.43f,1.43f,2.65f,4.13f,true}
};
static const BuildingCollider log_hut[]={
    {0,0,2.75f,2.35f,-.25f,2.34f,false},
    {0,0,3.0f,1.83f,2.34f,2.92f,false},
    {0,0,3.05f,.89f,2.92f,3.65f,false},
    {0,0,3.05f,.23f,3.65f,3.89f,false}
};
static const BuildingCollider fishnet_tent[]={
    {0,-.13f,3.02f,3.03f,-.04f,1.28f,true},
    {0,-.08f,1.81f,1.76f,1.28f,2.44f,true},
    {0,0,.98f,.98f,2.44f,3.72f,true}
};
static const BuildingCollider driftwood_tent[]={
    {0,-.13f,2.84f,2.91f,-.04f,1.3f,true},
    {0,-.05f,1.68f,1.69f,1.3f,2.48f,true},
    {0,0,.9f,.9f,2.48f,3.7f,true}
};
static const BuildingCollider driedfish_tent[]={
    {0,-.15f,2.19f,2.22f,-.06f,1.2f,false},
    {0,-.15f,1.37f,2.22f,1.2f,2.44f,false},
    {0,-.15f,.61f,2.31f,2.44f,3.67f,false}
};
#define STILT(x,z) {x,z,.16f,.16f,-.30f,.55f,false}
static const BuildingCollider stilt_hut[]={
    {0,-.71f,2.37f,1.47f,.54f,2.78f,false},
    {0,0,2.55f,2.34f,.46f,.54f,false},
    {0,0,1.61f,2.8f,2.78f,3.36f,false},
    {0,0,.68f,2.8f,3.36f,4.10f,false},
    {0,2.57f,.88f,.47f,.08f,.27f,false},
    {0,2.35f,.88f,.25f,.25f,.45f,false},
    STILT(-2.3f,-2.09f), STILT(0,-2.09f), STILT(2.3f,-2.09f),
    STILT(-2.3f, .01f), STILT(0, .01f), STILT(2.3f, .01f),
    STILT(-2.3f, 2.11f), STILT(0, 2.11f), STILT(2.3f, 2.11f)
};
#undef STILT

static const BuildingCollider *building_colliders(const QlcgBuildingModel *model,
                                                  BuildingCollider *door,int *count) {
    *count=0;
    if(model->colliders) {*count=model->collider_count;return model->colliders;}
    if(model->kind==3) {
        static const BuildingCollider gate[]={{-.87f,0,.12f,.14f,0,1.8f},{.87f,0,.12f,.14f,0,1.8f},{0,0,1,.19f,1.64f,1.80f}};
        static const BuildingCollider wide_gate[]={{-1.87f,0,.12f,.14f,0,2.15f},{1.87f,0,.12f,.14f,0,2.15f},{0,0,2,.19f,1.99f,2.15f}};
        static const BuildingCollider shelter[]={{-1.2f,-.67f,.065f,.065f,0,2},{1.2f,-.67f,.065f,.065f,0,2},
            {-1.2f,1.47f,.065f,.065f,0,2},{1.2f,1.47f,.065f,.065f,0,2},{0,.40f,1.4f,1.3f,1.98f,2.5f},
            {0,0,.23f,.22f,0,.44f},{0,.9f,.27f,.19f,0,.47f}};
        if(!strcmp(model->id,"gate")) {*count=3;return gate;}
        if(!strcmp(model->id,"wide_gate")) {*count=3;return wide_gate;}
        if(!strcmp(model->id,"tea_shelter")) {*count=7;return shelter;}
        static const BuildingCollider steps[]={{0,.667f,1,.334f,0,.2f},{0,0,1,.334f,0,.4f},{0,-.667f,1,.334f,0,.6f}};
        if(!strcmp(model->id,"steps")) {*count=3;return steps;}
        const double *a=model->bounds_min,*b=model->bounds_max;
        *door=(BuildingCollider){(float)(a[0]+b[0])*.5f,(float)(a[2]+b[2])*.5f,
            (float)(b[0]-a[0])*.5f,(float)(b[2]-a[2])*.5f,(float)a[1],(float)b[1]};
        *count=1;return door;
    }
    if(model->kind==QLCG_BUILDING_YARD) {
        for(size_t i=0;i<sizeof(yard_colliders)/sizeof(yard_colliders[0]);i++) {
            if(strcmp(model->id,yard_colliders[i].id))continue;
            *count=yard_colliders[i].count;
            return yard_colliders[i].boxes;
        }
        return NULL;
    }
    if(model->kind==QLCG_BUILDING_DOOR) {
        const double *lo=model->bounds_min,*hi=model->bounds_max;
        *door=(BuildingCollider){.x=(float)(lo[0]+hi[0])*.5f,.z=0,
            .half_x=(float)(hi[0]-lo[0])*.5f,.half_z=.14f,
            .bottom=(float)lo[1],.top=(float)hi[1]};
        *count=1;
        return door;
    }
    if(model->kind!=QLCG_BUILDING_ROOM)return NULL;
#define USE_COLLIDERS(boxes) { *count=(int)(sizeof(boxes)/sizeof((boxes)[0])); return boxes; }
    if(!strcmp(model->pack,"house_0001")) USE_COLLIDERS(primitive_hut)
    if(!strcmp(model->pack,"house_0002")) USE_COLLIDERS(barbarian_hut)
    if(!strcmp(model->pack,"house_0003")) USE_COLLIDERS(log_hut)
    if(strcmp(model->pack,"house_0004"))return NULL;
    if(strstr(model->unit_id,"tent_fishnet")) USE_COLLIDERS(fishnet_tent)
    if(strstr(model->unit_id,"tent_driftwood")) USE_COLLIDERS(driftwood_tent)
    if(strstr(model->unit_id,"tent_driedfish")) USE_COLLIDERS(driedfish_tent)
    USE_COLLIDERS(stilt_hut)
#undef USE_COLLIDERS
}

bool qlcg_settlement_yard_footprint(const QlcgSettlementPart *part,float center[2],float half[2]) {
    const QlcgBuildingModel *model=qlcg_lot_model(part->model);
    if(model->kind!=QLCG_BUILDING_YARD)return false;
    BuildingCollider unused;
    int count;
    const BuildingCollider *boxes=building_colliders(model,&unused,&count);
    float min_x=INFINITY,min_z=INFINITY,max_x=-INFINITY,max_z=-INFINITY;
    for(int i=0;i<count;i++) {
        const BuildingCollider *box=&boxes[i];
        if(box->top<=.01f || box->bottom>=1.7f)continue;
        min_x=fminf(min_x,box->x-box->half_x);max_x=fmaxf(max_x,box->x+box->half_x);
        min_z=fminf(min_z,box->z-box->half_z);max_z=fmaxf(max_z,box->z+box->half_z);
    }
    if(!isfinite(min_x))return false;
    float c=cosf(part->yaw),s=sinf(part->yaw);
    float x=(min_x+max_x)*.5f*part->scale,z=(min_z+max_z)*.5f*part->scale;
    float rx=(max_x-min_x)*.5f*part->scale,rz=(max_z-min_z)*.5f*part->scale;
    center[0]=part->x+c*x-s*z;center[1]=part->z+s*x+c*z;
    half[0]=fabsf(c)*rx+fabsf(s)*rz;half[1]=fabsf(s)*rx+fabsf(c)*rz;
    return true;
}

static bool collider_overlaps(const BuildingCollider *box,float x,float z,float margin) {
    float dx=fmaxf(0,fabsf(x-box->x)-margin),dz=fmaxf(0,fabsf(z-box->z)-margin);
    if(dx>=box->half_x || dz>=box->half_z)return false;
    if(!box->rounded)return true;
    dx/=box->half_x;
    dz/=box->half_z;
    return dx*dx+dz*dz<1;
}

typedef enum { BUILDING_OVERLAP, BUILDING_LANDING, BUILDING_CEILING } BuildingQuery;

typedef struct {
    const RegionData *chunk;
    const BuildingCollider *boxes;
    int count;
    float x,z,lo,hi,half,result;
    BuildingQuery query;
} WorkbenchQuery;

static void query_workbench(float wx,float wz,int kind,int state,void *context) {
    (void)kind; (void)state;
    WorkbenchQuery *q=context;
    float lx=wx-q->chunk->center_x,lz=wz-q->chunk->center_z;
    float x=(q->x-lx)/QLCG_WORKBENCH_SCALE,z=(q->z-lz)/QLCG_WORKBENCH_SCALE;
    float half=q->half/QLCG_WORKBENCH_SCALE;
    if (fabsf(x)>2+half || fabsf(z)>2+half) return;
    float y=qlcg_region_height_at(q->chunk,lx,lz);
    const float epsilon=.0001f;
    for (int i=0;i<q->count;i++) {
        const BuildingCollider *box=q->boxes+i;
        if (!collider_overlaps(box,x,z,half)) continue;
        float bottom=y+box->bottom*QLCG_WORKBENCH_SCALE,top=y+box->top*QLCG_WORKBENCH_SCALE;
        if (q->query==BUILDING_OVERLAP && q->lo<top-epsilon && q->hi>bottom+epsilon) { q->result=1; return; }
        if (q->query==BUILDING_LANDING && top<=q->hi+epsilon && top>=q->lo) q->result=fmaxf(q->result,top);
        if (q->query==BUILDING_CEILING && bottom>=q->lo-epsilon && bottom<=q->hi) q->result=fminf(q->result,bottom);
    }
}

static float query_colliders(const RegionData *reg,float x,float z,float lo,float hi,
                             float half,BuildingQuery query) {
    float result=query==BUILDING_CEILING?INFINITY:query==BUILDING_LANDING?-INFINITY:0;
    if(!reg || !reg->stream)return result;
    float lx,lz;
    const RegionData *chunk=qlcg_region_chunk_at(reg,x,z,&lx,&lz);
    if(!chunk) return result;
    const MacroWorld *w=reg->stream->world;
    if(qlcg_chd_workbench_count(w,chunk->center_cx,chunk->center_cz)) {
        const QlcgBuildingModel *bench=qlcg_building_model_find(QLCG_WORKBENCH_MODEL);
        BuildingCollider fallback;
        int count=0;
        const BuildingCollider *boxes=bench?building_colliders(bench,&fallback,&count):NULL;
        WorkbenchQuery placed={chunk,boxes,count,lx,lz,lo,hi,half,result,query};
        qlcg_chd_visit_workbenches(w,chunk->center_cx,chunk->center_cz,query_workbench,&placed);
        result=placed.result;
        if (query==BUILDING_OVERLAP && result>0) return result;
    }
    const QlcgLot *lot=qlcg_lot_at(w,chunk->center_cx,chunk->center_cz);
    if(!lot && (chunk->flag_count<=0 || fabsf(lx)>10+half || fabsf(lz)>10+half)) return result;
    QlcgSettlementPart generated[QLCG_SETTLEMENT_PARTS];
    const QlcgSettlementPart *parts=lot?lot->parts:generated;
    int count=lot?lot->count:qlcg_settlement_layout(qlcg_settlement_chunk(w,chunk->center_cx,chunk->center_cz),generated);
    const float epsilon=.0001f;
    for(int i=0;i<count;i++) {
        const QlcgSettlementPart *part=&parts[i];
        float hx=lx-part->x,hz=lz-part->z;
        float c=cosf(part->yaw),s=sinf(part->yaw);
        float local_x=(hx*c+hz*s)/part->scale,local_z=(-hx*s+hz*c)/part->scale;
        float margin=half/part->scale;
        const QlcgBuildingModel *model=qlcg_lot_model(part->model);
        if(local_x<model->bounds_min[0]-margin || local_x>model->bounds_max[0]+margin ||
           local_z<model->bounds_min[2]-margin || local_z>model->bounds_max[2]+margin) continue;

        BuildingCollider door;
        int box_count;
        const BuildingCollider *boxes=building_colliders(model,&door,&box_count);
        if(!box_count)continue;
        const QlcgSettlementPart *parent=part->parent<0?part:&parts[part->parent];
        float base,dx,dz;
        if(!qlcg_settlement_base(chunk,parent->x,parent->z,&base,&dx,&dz))continue;
        if(parent->flags&QLCG_PART_FLAT) dx=dz=0;
        float y=base+(lx-parent->x)*dx+(lz-parent->z)*dz+part->y;
        for(int j=0;j<box_count;j++) {
            const BuildingCollider *box=&boxes[j];
            if(!collider_overlaps(box,local_x,local_z,margin))continue;
            float bottom=y+box->bottom*part->scale,top=y+box->top*part->scale;
            if(query==BUILDING_OVERLAP && lo<top-epsilon && hi>bottom+epsilon)return 1;
            if(query==BUILDING_LANDING && top<=hi+epsilon && top>=lo)result=fmaxf(result,top);
            if(query==BUILDING_CEILING && bottom>=lo-epsilon && bottom<=hi)result=fminf(result,bottom);
        }
    }
    return result;
}

bool qlcg_settlement_blocked(const RegionData *reg,float x,float feet,float z,float height,float half) {
    return query_colliders(reg,x,z,feet,feet+height,half,BUILDING_OVERLAP)>0;
}

float qlcg_settlement_landing(const RegionData *reg,float x,float z,float from,float to,float half) {
    return query_colliders(reg,x,z,to,from,half,BUILDING_LANDING);
}

float qlcg_settlement_ceiling(const RegionData *reg,float x,float z,float from,float to,float half) {
    return query_colliders(reg,x,z,from,to,half,BUILDING_CEILING);
}
