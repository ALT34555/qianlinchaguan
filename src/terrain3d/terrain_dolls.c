#include "systems/world/world_lot.h"
#include "../systems/world/model/building_model.h"
#include "terrain_dolls.h"
#include "terrain_tools.h"
#include "terrain_settlement.h"
#include "../systems/dolls/doll_sim.h"
#include "../systems/dolls/doll_life.h"
#include "../systems/dolls/doll_watch.h"
#include "../systems/world/world_civ_economy.h"
#include "../systems/world/world_farm_surface.h"
#include "../systems/world/world_settlement.h"
#include "../systems/world/model/doll_model.h"
#include "../systems/world/faction_naming.h"
#include "../systems/calendar/calendar.h"
#include "../platform/platform.h"
#include "../core/math_util.h"
#include "../core/i18n.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DOLL_VISIBLE_CAP=24, DOLL_CAMPS=32, DOLL_SPEECH_CAP=DOLL_CAMPS*QLCG_LIFE_RESIDENTS };
enum { DOLL_NECK_BONE=4, DOLL_LEFT_ARM_BONE=8, DOLL_RIGHT_ARM_BONE=12 };
typedef struct {
    const TerritoryFaction *faction;
    TerrainDollRef ref;
    QlcgDollSample sample;
    float x,y,z,distance;
    float main[3],trim[3],rotation[2];
    bool seated;
    char name[QLCG_DOLL_NAME_BYTES];
} DollActor;
typedef struct { float distance;int index; } DollOrder;
typedef struct {
    QlcgDollWatch watch;
    QlcgDollGait gait;
    QlcgDollConversation conversation;
    uint32_t id;
    float x,z,yaw;
    bool valid,inside,owned,crossed_border,field_contact;
} DollMotion;
typedef struct {
    DollMotion residents[QLCG_LIFE_RESIDENTS];
    uint64_t frame,serial;
    int slot;
} CampMotion;
struct TerrainDolls {
    GLuint program,vao,vbo,ebo;
    struct { GLint mvp,sun,camera,fog_color,tint,fog,light,underwater,main_color,trim_color,actor,yaw,scale,bones,vertical; } uniforms;
    const MacroWorld *world;
    const RegionData *region;
    uint64_t revision;
    double real_seconds;
    double player_x,player_z,clock_ms;
    float roam_seconds;
    uint64_t frame;
    QlcgDollSpeech speech[DOLL_SPEECH_CAP];
    int speech_head,speech_count;
    uint8_t motion_index[QLCG_STREAM_CAP];
    CampMotion motion[2][DOLL_CAMPS];
    int count;
    DollActor actors[DOLL_VISIBLE_CAP];
    float poses[DOLL_VISIBLE_CAP][QLCG_DOLL_BONE_COUNT*16];
    TerrainDollRef host_ref;
    float host_main[3],host_trim[3];
    QlcgDollGait avatar_gait;
    double avatar_x,avatar_z,avatar_time;
    const MacroWorld *avatar_world;
};

static void faction_color(uint32_t packed,float rgb[3]);

static bool same_doll(const TerrainDollRef *a,const TerrainDollRef *b) {
    return a->id && a->id==b->id && a->plane_x==b->plane_x && a->plane_z==b->plane_z &&
           !strncmp(a->camp_uid,b->camp_uid,sizeof(a->camp_uid));
}

static const char *DOLL_VS=
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;layout(location=1) in vec3 aNormal;\n"
    "layout(location=2) in vec3 aColor;layout(location=3) in vec4 aWeight;\n"
    "layout(location=4) in vec4 aBone;layout(location=5) in float aKind;\n"
    "layout(location=6) in float aShade;\n"
    "uniform mat4 uMVP;uniform mat4 uBones[23];uniform vec3 uActor;\n"
    "uniform vec2 uYaw;uniform vec3 uMain,uTrim;uniform float uScale,uVertical;\n"
    "out vec3 vPos,vNormal,vColor;\n"
    "void main(){ivec4 i=ivec4(aBone);\n"
    "mat4 skin=uBones[i.x]*aWeight.x+uBones[i.y]*aWeight.y+uBones[i.z]*aWeight.z+uBones[i.w]*aWeight.w;\n"
    "vec3 p=(skin*vec4(aPos,1)).xyz*uScale,n=mat3(skin)*aNormal;\n"
    "p.y*=uVertical;n.y/=uVertical;\n"
    "vPos=vec3(p.x*uYaw.x+p.z*uYaw.y,p.y,-p.x*uYaw.y+p.z*uYaw.x)+uActor;\n"
    "vNormal=vec3(n.x*uYaw.x+n.z*uYaw.y,n.y,-n.x*uYaw.y+n.z*uYaw.x);\n"
    "vColor=aKind<.5?aColor:(aKind<1.5?uMain:uTrim)*aShade;\n"
    "gl_Position=uMVP*vec4(vPos,1);}\n";

static const char *DOLL_FS=
    "#version 330 core\n"
    "in vec3 vPos,vNormal,vColor;out vec4 FragColor;\n"
    "uniform vec3 uSun,uCamera,uFogColor,uTint;uniform vec2 uFog;\n"
    "uniform float uLight;uniform int uUnderwater;\n"
    "void main(){float d=max(dot(normalize(vNormal),uSun),0);\n"
    "float daylight=clamp(uSun.y*1.4+.45,.2,1.05);\n"
    "vec3 lit=max(vec3(0),vColor*((.48+d*.68)*daylight*uLight*uTint+.035));\n"
    "vec3 color=mix(lit*12.92,1.055*pow(lit,vec3(1./2.4))-.055,step(vec3(.0031308),lit));\n"
    "float distance=length(vPos-uCamera);float fog=uUnderwater!=0?clamp((distance-1)/35.,0.,1.):clamp((distance-uFog.x)/max(.001,uFog.y-uFog.x),0.,1.);\n"
    "FragColor=vec4(mix(color,uFogColor,fog),1);}\n";

static bool setup(struct TerrainDolls *d) {
    if(d->program) return true;
    const QlcgDollModel *model=qlcg_doll_model();
    d->program=qlcg_gl_compile_program(DOLL_VS,DOLL_FS);
    if(!d->program) return false;
    d->uniforms.mvp=qlcg_gl_uniform(d->program,"uMVP");
    d->uniforms.sun=qlcg_gl_uniform(d->program,"uSun");
    d->uniforms.camera=qlcg_gl_uniform(d->program,"uCamera");
    d->uniforms.fog_color=qlcg_gl_uniform(d->program,"uFogColor");
    d->uniforms.tint=qlcg_gl_uniform(d->program,"uTint");
    d->uniforms.fog=qlcg_gl_uniform(d->program,"uFog");
    d->uniforms.light=qlcg_gl_uniform(d->program,"uLight");
    d->uniforms.underwater=qlcg_gl_uniform(d->program,"uUnderwater");
    d->uniforms.main_color=qlcg_gl_uniform(d->program,"uMain");
    d->uniforms.trim_color=qlcg_gl_uniform(d->program,"uTrim");
    d->uniforms.actor=qlcg_gl_uniform(d->program,"uActor");
    d->uniforms.yaw=qlcg_gl_uniform(d->program,"uYaw");
    d->uniforms.scale=qlcg_gl_uniform(d->program,"uScale");
    d->uniforms.bones=qlcg_gl_uniform(d->program,"uBones[0]");
    d->uniforms.vertical=qlcg_gl_uniform(d->program,"uVertical");
    glGenVertexArrays(1,&d->vao);glGenBuffers(1,&d->vbo);glGenBuffers(1,&d->ebo);
    glBindVertexArray(d->vao);glBindBuffer(GL_ARRAY_BUFFER,d->vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)((size_t)model->vertex_count*sizeof(QlcgDollVertex)),model->vertices,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,d->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)((size_t)model->index_count*sizeof(uint32_t)),model->indices,GL_STATIC_DRAW);
    const int components[7]={3,3,3,4,4,1,1};
    const size_t offsets[7]={offsetof(QlcgDollVertex,position),offsetof(QlcgDollVertex,normal),offsetof(QlcgDollVertex,color),offsetof(QlcgDollVertex,skin_weights),offsetof(QlcgDollVertex,skin_indices),offsetof(QlcgDollVertex,color_kind),offsetof(QlcgDollVertex,cloth_shade)};
    for(int i=0;i<7;i++) {
        glEnableVertexAttribArray((GLuint)i);
        glVertexAttribPointer((GLuint)i,components[i],i==4||i==5?GL_UNSIGNED_BYTE:GL_FLOAT,GL_FALSE,sizeof(QlcgDollVertex),(void *)offsets[i]);
    }
    return true;
}

static void watch_field(const MacroWorld *owner,const QlcgDollCamp *camp,const DollMotion *motion,QlcgDollWatchContext *c) {
    const TerritoryFaction *f=camp->faction;
    float nearest=INFINITY;
    for(int i=0;i<camp->plot_count;i++) {
        QlcgFarmPlot plot=camp->plots[i];
        float dx=(float)((int64_t)plot.cx-f->cx)*CHUNK_SIZE;
        if(!qlcg_world_flat(owner)&&owner->circumference>0) dx=remainderf(dx,owner->circumference);
        plot.x+=dx;plot.z+=(float)((int64_t)plot.cz-f->cz)*CHUNK_SIZE;
        QlcgFarmSurface surface;
        if(!qlcg_farm_surface_plot(&surface,&plot)) continue;
        float padding=motion->watch.mode==DOLL_STATE_PROTECT?10:8;
        if(!qlcg_farm_surface_inside(&surface,c->player_x,c->player_z,padding)) continue;
        float x=c->player_x-plot.x,z=c->player_z-plot.z;
        float square=x*x+z*z;
        if(square>=nearest) continue;
        nearest=square;c->field=true;
        float rx=x*surface.cosine+z*surface.sine,rz=-x*surface.sine+z*surface.cosine;
        float width=plot.width*.5f,depth=plot.depth*.5f;
        if(fabsf(rx)-width>=fabsf(rz)-depth) {rx=copysignf(width+.65f,rx);rz=qlcg_clampf(rz,-depth,depth);}
        else {rz=copysignf(depth+.65f,rz);rx=qlcg_clampf(rx,-width,width);}
        c->field_x=plot.x+rx*surface.cosine-rz*surface.sine;
        c->field_z=plot.z+rx*surface.sine+rz*surface.cosine;
    }
}

static void move_watch(DollMotion *motion,QlcgDollSample *sample,const MacroWorld *owner,const QlcgDollCamp *camp,
                       const RegionData *reg,float origin_x,float origin_z,float px,float pz,
                       bool owned,bool roaming,float dt) {
    float distance=hypotf(px,pz);
    bool nearby=distance<=(motion->watch.mode==DOLL_STATE_APPROACH?57:54);
    QlcgDollWatchContext c={.player_x=px,.player_z=pz,.nearby=nearby,.roaming=roaming};
    if(owned&&distance<80) watch_field(owner,camp,motion,&c);
    motion->crossed_border=owned&&nearby&&(motion->crossed_border||
        (!motion->owned&&motion->watch.mode==DOLL_STATE_APPROACH));
    c.inside=owned&&(owner->chunk_claims||c.field||motion->crossed_border||distance<=(motion->inside?38:36));
    motion->inside=c.inside;
    motion->owned=owned;
    for(int i=0;i<camp->part_count&&c.obstacle_count<QLCG_WATCH_OBSTACLES;i++){
        const QlcgSettlementPart *p=camp->parts+i;
        const QlcgBuildingModel *model=qlcg_lot_model(p->model);
        if(!model) continue;
        if(model->kind==QLCG_BUILDING_ROOM) {
            float half=3.85f*p->scale+.4f;
            c.obstacles[c.obstacle_count++]=(QlcgWatchObstacle){p->x,p->z,half,half};
        } else if(model->kind==QLCG_BUILDING_YARD) {
            float center[2],half[2];
            if(qlcg_settlement_yard_footprint(p,center,half))
                c.obstacles[c.obstacle_count++]=(QlcgWatchObstacle){center[0],center[1],half[0]+.4f,half[1]+.4f};
        }
    }
    QlcgDollWatch previous=motion->watch;
    float previous_x=previous.active?previous.x:sample->x;
    float previous_z=previous.active?previous.z:sample->z;
    qlcg_doll_watch_step(&motion->watch,&c,dt,sample);
    if(sample->moving) {
        float ground=qlcg_region_height_at(reg,origin_x+sample->x,origin_z+sample->z);
        float water=qlcg_region_water_at(reg,origin_x+sample->x,origin_z+sample->z);
        float old_ground=qlcg_region_height_at(reg,origin_x+previous_x,origin_z+previous_z);
        if(!isfinite(ground)||water>ground+.23f||fabsf(ground-old_ground)>.42f||
           qlcg_settlement_blocked(reg,origin_x+sample->x,ground,origin_z+sample->z,1.7f,.3f)) {
            sample->x=motion->watch.x=previous_x;sample->z=motion->watch.z=previous_z;
            motion->watch.speed=0;sample->moving=false;
        }
    }
    motion->field_contact=sample->state==DOLL_STATE_PROTECT&&(motion->field_contact||
        hypotf(px-sample->x,pz-sample->z)<4||hypotf(c.field_x-sample->x,c.field_z-sample->z)<.6f);
}

static void follow_pose(DollMotion *motion,QlcgDollSample *sample,float dt) {
    if(!motion->valid||dt<=0 || (motion->watch.active && sample->state==DOLL_STATE_WATCH)) return;
    float dx=sample->x-motion->x,dz=sample->z-motion->z;
    float distance=hypotf(dx,dz);
    if(distance>.01f&&distance<6) {
        float part=fminf(1,dt*3.8f/distance);
        sample->x=motion->x+dx*part;
        sample->z=motion->z+dz*part;
        if(part<1) {
            sample->moving=true;sample->state=DOLL_STATE_WALK;sample->yaw=atan2f(dx,dz);
        }
    }
}

static int compare_order(const void *left,const void *right) {
    const DollOrder *a=left,*b=right;
    if(a->distance<b->distance) return -1;
    if(a->distance>b->distance) return 1;
    return a->index-b->index;
}

static void prepare(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w) {
    struct TerrainDolls *d=tr->dolls;
    double now=qlcg_time_ms()/1000,elapsed=now-d->real_seconds;
    double player_x=(double)reg->center_x+tr->x,player_z=(double)reg->center_z+tr->z;
    double player_delta=hypot(player_x-d->player_x,player_z-d->player_z);
    bool same_scene=d->world==w&&player_delta<8&&fabs(tr->weather_unix_ms-d->clock_ms)<90000;
    bool continuous=same_scene&&elapsed>=0&&elapsed<.4;
    if(d->world!=w) d->speech_count=d->speech_head=0;
    float dt=continuous&&!tr->dolls_paused?fminf(.1f,(float)elapsed):0;
    float dialogue_dt=same_scene&&elapsed>0?(float)elapsed:0;
    d->roam_seconds=!continuous?0:player_delta>.22*elapsed?1.2f:fmaxf(0,d->roam_seconds-dt);
    d->count=0;d->world=w;d->region=reg;d->real_seconds=now;d->clock_ms=tr->weather_unix_ms;
    d->player_x=player_x;d->player_z=player_z;
    uint64_t frame=++d->frame;
    CampMotion *current=d->motion[frame&1],*last=d->motion[(frame-1)&1];
    int slots[DOLL_CAMPS];const TerritoryFaction *factions[DOLL_CAMPS];
    DollActor candidates[DOLL_CAMPS*QLCG_LIFE_RESIDENTS];
    DollOrder order[DOLL_CAMPS*QLCG_LIFE_RESIDENTS];int count=0;
    QlcgCivTerritory player_territory={0};
    bool player_checked=false;
    int camps=qlcg_terrain_settlement_visible(tr,reg,w,slots,factions,DOLL_CAMPS);
    for(int i=0;i<camps;i++) {
        int slot=slots[i];
        const RegionData *chunk=reg->stream->slots[slot].data;
        const TerritoryFaction *f=factions[i];
        float origin_x=(float)((int64_t)chunk->center_cx-reg->center_cx)*CHUNK_SIZE;
        float origin_z=(float)((int64_t)chunk->center_cz-reg->center_cz)*CHUNK_SIZE;
        float camp_x=origin_x-tr->x,camp_z=origin_z-tr->z;
        if(camp_x*camp_x+camp_z*camp_z>264*264) continue;
        uint64_t serial=reg->stream->slots[slot].serial;
        CampMotion *old=last+d->motion_index[slot];
        CampMotion *motion=current+i;
        *motion=same_scene&&old->frame==frame-1&&old->slot==slot&&old->serial==serial?*old:(CampMotion){0};
        motion->frame=frame;motion->slot=slot;motion->serial=serial;
        d->motion_index[slot]=(uint8_t)i;
        QlcgCivTerritory territory;
        bool claimed=qlcg_civ_territory_at(w,chunk->center_cx,chunk->center_cz,&territory)&&territory.faction==f;
        const MacroWorld *owner=claimed?territory.owner:w;
        int offset=qlcg_world_flat(owner)?0:qlcg_timezone_offset_min(f->wx,owner->circumference);
        const QlcgDollLifeCamp *life=qlcg_doll_life_camp(owner,f);
        QlcgDollCamp camp;
        qlcg_doll_prepare_camp(owner,f,&camp);
        int residents=life?QLCG_LIFE_RESIDENTS:qlcg_civ_npc_count(owner,f);
        for(int j=0;j<residents;j++) {
            if (life && !(life->active_slots&(1u<<j))) continue;
            DollActor actor={0};actor.faction=f;
            DollMotion *state=motion->residents+j;
            const QlcgDollLifeResident *resident=life?life->residents+j:NULL;
            const char *name=resident?resident->name:(!j && !f->is_player?f->leader_name:NULL);
            if (name) memcpy(actor.name,name,sizeof(actor.name));
            bool sampled=resident?qlcg_doll_life_sample(&camp,resident,tr->weather_unix_ms,offset,&actor.sample):
                qlcg_doll_sample_resident(&camp,j,j%QLCG_DOLL_RESIDENTS,qlcg_doll_resident_id(f,j),tr->weather_unix_ms,offset,&actor.sample);
            if (sampled) {
                actor.ref=(TerrainDollRef){.id=actor.sample.id,.plane_x=owner->opt.plane_x,.plane_z=owner->opt.plane_z};
                memcpy(actor.ref.camp_uid,f->uid,sizeof(actor.ref.camp_uid));
                if (same_doll(&tr->controlled_doll,&actor.ref)) {
                    d->host_ref=actor.ref;
                    faction_color(f->main_color,d->host_main);
                    faction_color(f->trim_color,d->host_trim);
                    state->valid=false;
                    continue;
                }
            }
            if(!sampled||actor.sample.indoor) {
                state->valid=false;continue;
            }
            if(!state->valid||state->id!=actor.sample.id)
                *state=(DollMotion){.id=actor.sample.id,.gait={.phase=(actor.sample.id&255)/256.f}};
            bool player_near=camp_x*camp_x+camp_z*camp_z<80*80;
            if(resident) {
                actor.seated=resident->task==DOLL_LIFE_TEA&&resident->target==DOLL_LIFE_CHAIR&&
                    life->chair_owner==resident->id&&!resident->moving&&
                    hypotf(resident->x-life->chair_x,resident->z-life->chair_z)<.08f;
                if(resident->task==DOLL_LIFE_TEA&&resident->target==DOLL_LIFE_TEA_TABLE&&!resident->moving)
                    actor.sample.state=DOLL_STATE_WATCH;
                follow_pose(state,&actor.sample,dt);
                if(actor.sample.moving) actor.seated=false;
            }
            bool on_watch=!resident||(life->guard_owner==resident->id&&
                (resident->task==DOLL_LIFE_ROUTINE||resident->task==DOLL_LIFE_SHIFT));
            if(claimed&&on_watch&&actor.sample.state==DOLL_STATE_WATCH&&(player_near||state->watch.active)) {
                if(!player_checked&&player_near) {
                    int cx=reg->center_cx+(int)floorf((tr->x+CHUNK_SIZE*.5f)/CHUNK_SIZE);
                    int cz=reg->center_cz+(int)floorf((tr->z+CHUNK_SIZE*.5f)/CHUNK_SIZE);
                    qlcg_civ_territory_at(w,cx,cz,&player_territory);
                    player_checked=true;
                }
                move_watch(state,&actor.sample,owner,&camp,reg,origin_x,origin_z,-camp_x,-camp_z,
                           player_territory.faction==f,d->roam_seconds>0,dt);
            } else {
                state->watch.active=false;
                state->inside=state->owned=state->crossed_border=state->field_contact=false;
            }
            float travelled=state->valid&&actor.sample.moving&&dt>0?
                hypotf(actor.sample.x-state->x,actor.sample.z-state->z):0;
            if(travelled>fmaxf(.18f,dt*7)) travelled=0;
            qlcg_doll_update_gait(&state->gait,&actor.sample,travelled,dt);
            if(state->valid && dt>0 && !actor.seated) {
                float turn=remainderf(actor.sample.yaw-state->yaw,QLCG_TAU);
                actor.sample.yaw=state->yaw+qlcg_clampf(turn,-6.5f*dt,6.5f*dt);
            }
            state->valid=true;state->x=actor.sample.x;state->z=actor.sample.z;state->yaw=actor.sample.yaw;
            actor.x=origin_x+actor.sample.x;actor.z=origin_z+actor.sample.z;
            float dx=actor.x-tr->x,dz=actor.z-tr->z;
            actor.distance=dx*dx+dz*dz;
            if(!tr->dolls_paused&&d->speech_count<DOLL_SPEECH_CAP) {
                int tail=(d->speech_head+d->speech_count)%DOLL_SPEECH_CAP;
                if(qlcg_doll_dialogue_step(&state->conversation,actor.sample.type,actor.sample.day_kind,
                    actor.sample.state,state->field_contact,actor.distance,dialogue_dt,d->speech+tail)) d->speech_count++;
            }
            if(actor.distance>256*256) continue;
            order[count]=(DollOrder){actor.distance,count};
            candidates[count++]=actor;
        }
    }
    qsort(order,(size_t)count,sizeof(*order),compare_order);
    for(int i=0;i<count&&d->count<DOLL_VISIBLE_CAP;i++) {
        DollActor actor=candidates[order[i].index];actor.y=qlcg_region_height_at(reg,actor.x,actor.z);
        if(!isfinite(actor.y))continue;
        int same=-1;
        for(int j=0;j<d->count;j++)if(d->actors[j].faction==actor.faction){same=j;break;}
        if(same>=0) for(int j=0;j<3;j++) {actor.main[j]=d->actors[same].main[j];actor.trim[j]=d->actors[same].trim[j];}
        else {faction_color(actor.faction->main_color,actor.main);faction_color(actor.faction->trim_color,actor.trim);}
        actor.rotation[0]=cosf(actor.sample.yaw);actor.rotation[1]=sinf(actor.sample.yaw);
        if(actor.seated) qlcg_doll_chair_pose(qlcg_doll_render_phase(&actor.sample,d->real_seconds),
            QLCG_DOLL_CHAIR_HEIGHT/actor.sample.scale,d->poses[d->count]);
        else qlcg_doll_render_pose(&actor.sample,d->real_seconds,d->poses[d->count]);
        d->actors[d->count++]=actor;
    }
    d->revision=d->count?(uint64_t)(d->real_seconds*10):0;
}

static void faction_color(uint32_t packed,float rgb[3]) {
    for(int i=0;i<3;i++) {
        float value=(float)((packed>>(16-i*8))&255)/255;
        rgb[i]=value<=.04045f?value/12.92f:powf((value+.055f)/1.055f,2.4f);
    }
}

static void avatar_bone_point(const float bones[],int bone,float p[3]) {
    const float *rest=qlcg_doll_model()->bones[bone].inverse_bind;
    const float *skin=bones+16*bone;
    for (int i=0;i<3;++i) p[i]=skin[12+i]-skin[i]*rest[12]-skin[4+i]*rest[13]-skin[8+i]*rest[14];
}

static void avatar_branch(float bones[],int root,float angle) {
    float pivot[3],turn[16],c=cosf(angle),s=sinf(angle);
    avatar_bone_point(bones,root,pivot);
    qlcg_mat4_identity(turn);
    turn[5]=turn[10]=c;turn[6]=s;turn[9]=-s;
    turn[13]=pivot[1]*(1-c)+s*pivot[2];
    turn[14]=pivot[2]*(1-c)-s*pivot[1];
    for (int i=root;i<root+3;++i) qlcg_mat4_mul(turn,bones+16*i,bones+16*i);
}

static void draw_avatar(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w,
                        const TerrainVisualFrame *frame,struct TerrainDolls *d) {
    const GameSettings *s=frame->settings;
    float body=tr->controlled_doll_height>0 ? tr->controlled_doll_height : fmaxf(.4f,s->profile.height_cm*.01f);
    float feet=tr->y-(body*.94f+tr->step_offset-tr->crouch_offset);
    float vertical=qlcg_clampf(1-tr->crouch_offset/body,.63f,1);
    float cy=-cosf(tr->yaw), sy=sinf(tr->yaw);
    QlcgDollSample sample={.state=DOLL_STATE_WALK,.scale=body/qlcg_doll_model()->height};
    double now=qlcg_time_ms()*.001, x=(double)reg->center_x+tr->x,z=(double)reg->center_z+tr->z;
    float dt=(float)(now-d->avatar_time);
    float travelled=(float)hypot(x-d->avatar_x,z-d->avatar_z);
    if (!tr->reflection_pass) {
        if (d->avatar_world!=w || dt>.4f || travelled>4) {
            d->avatar_gait=(QlcgDollGait){0};travelled=0;
        }
        qlcg_doll_update_gait(&d->avatar_gait,&sample,travelled,qlcg_clampf(dt,0,.1f));
        d->avatar_x=x;d->avatar_z=z;d->avatar_time=now;d->avatar_world=w;
    } else qlcg_doll_update_gait(&d->avatar_gait,&sample,0,0);
    float bones[QLCG_DOLL_BONE_COUNT*16];
    qlcg_doll_render_pose(&sample,now,bones);
    avatar_branch(bones,DOLL_NECK_BONE,-qlcg_clampf(tr->pitch,-.58f,.58f));
    float origin[3]={(float)(x-tr->anchor_x),feet,(float)(z-tr->anchor_z)};
    float wrists[2][3];
    const int arm[]={DOLL_RIGHT_ARM_BONE,DOLL_LEFT_ARM_BONE};
    for (int i=0;i<2;++i) {
        InventoryItem item=inventory_equipped(&w->inventory,i?INV_SLOT_OFF_HAND:INV_SLOT_MAIN_HAND);
        float swing=qlcg_terrain_hand_swing(tr,i,now*1000,&item);
        float move=tr->in_water ? -.55f-.35f*sinf((float)now*5+i*QLCG_PI)
                  : !tr->on_ground ? -.32f : 0;
        avatar_branch(bones,arm[i],move-(item ? .46f : 0)-1.4f*fmaxf(0,swing));
        float p[3];avatar_bone_point(bones,arm[i]+2,p);
        wrists[i][0]=origin[0]+sample.scale*(p[0]*cy+p[2]*sy);
        wrists[i][1]=origin[1]+sample.scale*p[1]*vertical;
        wrists[i][2]=origin[2]+sample.scale*(-p[0]*sy+p[2]*cy);
    }
    float main[3],trim[3];
    if (same_doll(&tr->controlled_doll,&d->host_ref)) {
        memcpy(main,d->host_main,sizeof(main));memcpy(trim,d->host_trim,sizeof(trim));
    } else {
        faction_color(s->profile.main_color,main);faction_color(s->profile.trim_color,trim);
    }
    glUniform3fv(d->uniforms.main_color,1,main);glUniform3fv(d->uniforms.trim_color,1,trim);
    glUniform3fv(d->uniforms.actor,1,origin);glUniform2f(d->uniforms.yaw,cy,sy);
    glUniform1f(d->uniforms.scale,sample.scale);glUniform1f(d->uniforms.vertical,vertical);
    glUniformMatrix4fv(d->uniforms.bones,QLCG_DOLL_BONE_COUNT,GL_FALSE,bones);
    glDrawElements(GL_TRIANGLES,qlcg_doll_model()->index_count,GL_UNSIGNED_INT,0);
    qlcg_terrain_tools_world_draw(tr,w,frame->vp,wrists,sample.scale,frame->sky.light_scale);
}

void qlcg_terrain_dolls_draw(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w,const TerrainVisualFrame *frame) {
    bool citizens=w->opt.artificial_enabled&&reg->stream;
    bool avatar=tr->external_camera;
    if(!citizens && tr->dolls) {
        tr->dolls->count=0;tr->dolls->revision=0;tr->dolls->world=NULL;tr->dolls->speech_count=0;
    }
    if(!citizens && !avatar) return;
    if(!tr->dolls) tr->dolls=calloc(1,sizeof(*tr->dolls));
    if(!tr->dolls) return;
    struct TerrainDolls *d=tr->dolls;
    if(citizens && !tr->reflection_pass) prepare(tr,reg,w);
    bool draw_citizens=d->count && d->world==w && d->region==reg;
    if((!draw_citizens && !avatar)||!setup(d)) return;
    GLuint p=d->program;glUseProgram(p);glBindVertexArray(d->vao);
    glUniformMatrix4fv(d->uniforms.mvp,1,GL_FALSE,frame->vp);
    glUniform3fv(d->uniforms.sun,1,frame->sky.sun_dir);
    glUniform3fv(d->uniforms.camera,1,frame->camera);
    glUniform3fv(d->uniforms.fog_color,1,frame->sky.horizon);
    glUniform3fv(d->uniforms.tint,1,frame->sky.tint);
    glUniform2f(d->uniforms.fog,frame->fog[0],frame->fog[1]);
    glUniform1f(d->uniforms.light,frame->sky.light_scale);
    glUniform1i(d->uniforms.underwater,frame->underwater?1:0);
    glUniform1f(d->uniforms.vertical,1);
    for(int i=0;draw_citizens&&i<d->count;i++) {
        const DollActor *a=d->actors+i;
        if (same_doll(&tr->controlled_doll,&a->ref)) continue;
        glUniform3fv(d->uniforms.main_color,1,a->main);glUniform3fv(d->uniforms.trim_color,1,a->trim);
        glUniform3f(d->uniforms.actor,(float)((double)reg->center_x-tr->anchor_x)+a->x,a->y,(float)((double)reg->center_z-tr->anchor_z)+a->z);
        glUniform2f(d->uniforms.yaw,a->rotation[0],a->rotation[1]);
        glUniform1f(d->uniforms.scale,a->sample.scale);
        glUniformMatrix4fv(d->uniforms.bones,QLCG_DOLL_BONE_COUNT,GL_FALSE,d->poses[i]);
        glDrawElements(GL_TRIANGLES,qlcg_doll_model()->index_count,GL_UNSIGNED_INT,0);
    }
    if (avatar) draw_avatar(tr,reg,w,frame,d);
}

bool qlcg_terrain_doll_pose(const TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w,
                            const TerrainDollRef *ref,float pose[5]) {
    const struct TerrainDolls *d=tr->dolls;
    if(!d||d->world!=w||d->region!=reg||!ref||!pose) return false;
    for(int i=0;i<d->count;++i) {
        const DollActor *a=d->actors+i;
        if(!same_doll(ref,&a->ref)) continue;
        pose[0]=a->x;pose[1]=a->y;pose[2]=a->z;pose[3]=a->sample.yaw;
        pose[4]=a->sample.scale*qlcg_doll_model()->height;
        return true;
    }
    return false;
}

uint64_t qlcg_terrain_dolls_revision(const TerrainRenderer *tr) {return tr->dolls?tr->dolls->revision:0;}

bool qlcg_terrain_dolls_watch_response(const TerrainRenderer *tr,const TerrainDollRef *resident,
                                        int *state,float position[3]) {
    const struct TerrainDolls *d=tr->dolls;
    if(!d||!resident->id) return false;
    for(int i=0;i<d->count;i++) {
        const DollActor *actor=&d->actors[i];
        const TerrainDollRef *ref=&actor->ref;
        if(ref->id!=resident->id||ref->plane_x!=resident->plane_x||ref->plane_z!=resident->plane_z||
           strncmp(ref->camp_uid,resident->camp_uid,sizeof(ref->camp_uid))) continue;
        if(actor->sample.state<DOLL_STATE_APPROACH||actor->sample.state>DOLL_STATE_PATROL) return false;
        *state=actor->sample.state;
        position[0]=actor->x;position[1]=actor->y;position[2]=actor->z;
        return true;
    }
    return false;
}

bool qlcg_terrain_dolls_take_speech(TerrainRenderer *tr,QlcgDollSpeech *speech) {
    struct TerrainDolls *d=tr->dolls;
    if(!d||!d->speech_count) return false;
    *speech=d->speech[d->speech_head];
    d->speech_head=(d->speech_head+1)%DOLL_SPEECH_CAP;
    d->speech_count--;
    return true;
}

bool qlcg_terrain_dolls_probe(const TerrainRenderer *tr,const RegionData *reg,const MacroWorld *w,const float dir[3],float limit,AimProbeResult *result) {
    const struct TerrainDolls *d=tr->dolls;
    if(!d||!w->opt.artificial_enabled||d->world!=w||d->region!=reg) return false;
    const DollActor *hit=NULL;float distance=limit;
    for(int i=0;i<d->count;i++) {
        const DollActor *a=d->actors+i;
        if (same_doll(&tr->controlled_doll,&a->ref)) continue;
        float dx=tr->x-a->x,dz=tr->z-a->z,dy=tr->y-a->y;
        float c=a->rotation[0],s=a->rotation[1],low=0,high=distance;
        float scale=a->sample.scale;
        const float origin[3]={dx*c-dz*s,dy,dx*s+dz*c};
        const float ray[3]={dir[0]*c-dir[2]*s,dir[1],dir[0]*s+dir[2]*c};
        const float (*bounds)[3]=a->seated?qlcg_doll_chair_bounds():qlcg_doll_action_bounds(qlcg_doll_render_action(&a->sample));
        bool intersects=true;
        for(int axis=0;axis<3;axis++) {
            float minimum=bounds[0][axis]*scale,maximum=bounds[1][axis]*scale;
            if(fabsf(ray[axis])<1e-7f) {if(origin[axis]<minimum||origin[axis]>maximum)intersects=false;continue;}
            float a0=(minimum-origin[axis])/ray[axis],b0=(maximum-origin[axis])/ray[axis];
            low=fmaxf(low,fminf(a0,b0));high=fminf(high,fmaxf(a0,b0));
            if(low>high) intersects=false;
        }
        if(intersects&&low<distance) {distance=low;hit=a;}
    }
    if(!hit) return false;
    result->kind=AIM_HIT_DOLL;result->dist_m=distance;
    result->doll=hit->ref;
    result->lx=tr->x+dir[0]*distance;result->ly=tr->y+dir[1]*distance;result->lz=tr->z+dir[2]*distance;
    result->wx=reg->center_x+result->lx;result->wz=reg->center_z+result->lz;
    if (hit->name[0]) snprintf(result->title,sizeof(result->title),"%s",hit->name);
    else snprintf(result->title,sizeof(result->title),"%s · %08X",qlcg_i18n_content_text(hit->sample.type_name?hit->sample.type_name:"村民"),hit->sample.id);
    snprintf(result->detail1,sizeof(result->detail1),"%s · %s",qlcg_i18n_content_text(qlcg_doll_role_name(hit->sample.role)),qlcg_i18n_content_text(qlcg_doll_state_name(hit->sample.state)));
    snprintf(result->detail2,sizeof(result->detail2),"%s · %.1f m",qlcg_faction_display_name(hit->faction),distance);
    return true;
}

void qlcg_terrain_dolls_free(TerrainRenderer *tr) {
    if(!tr->dolls) return;
    qlcg_gl_delete_vertex_arrays(1,&tr->dolls->vao);
    GLuint buffers[2]={tr->dolls->vbo,tr->dolls->ebo};qlcg_gl_delete_buffers(2,buffers);
    qlcg_gl_delete_program(tr->dolls->program);free(tr->dolls);tr->dolls=NULL;
}
