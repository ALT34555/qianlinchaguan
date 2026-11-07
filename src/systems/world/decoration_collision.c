#include "decoration_collision.h"
#include "surface_stream.h"
#include "model/model_json.h"
#include "model/plant_material.h"
#include "model/rock_material.h"
#include "model/rock_def.h"
#include "../../core/math_util.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static float max_reach;

const MovementRules *qlcg_movement_rules(void) {
    static MovementRules r = {60, .65f, 1, 2.4f, .15f, .2f, .8f, 0xF4F3F2u};
    static bool loaded;
    if (loaded) return &r;
    loaded = true;
    JsVal *data = qlcg_js_load("content/data/world/movement.json", NULL);
    if (!data) return &r;
#define RULE(key, lo, hi) r.key = qlcg_clampf((float)qlcg_js_num(qlcg_js_get(data, #key), r.key), lo, hi)
    RULE(max_slope_degrees, 0, 90);
    RULE(shrub_speed_factor, 0, 1);
    RULE(fade_seconds, .001f, 60);
    RULE(pulse_seconds, .001f, 60);
    RULE(pulse_depth, 0, 1);
    RULE(feedback_height, 0, 1);
    RULE(feedback_opacity, 0, 1);
#undef RULE
    const char *rgb = qlcg_js_str(qlcg_js_get(data,"feedback_rgb"), "f2f3f4");
    unsigned red, green, blue;
    if (strlen(rgb)==6 && sscanf(rgb,"%2x%2x%2x",&red,&green,&blue)==3)
        r.feedback_rgb = red | (green<<8) | (blue<<16);
    qlcg_js_free(data);
    return &r;
}

float qlcg_block_feedback(BlockFeedback *f, bool blocked, float dt) {
    const MovementRules *r = qlcg_movement_rules();
    float p = qlcg_clampf(f->elapsed/r->fade_seconds,0,1);
    float strength = f->from+(f->to-f->from)*p*p*(3-2*p);
    float target = blocked ? 1.0f : 0.0f;
    if (target != f->to) {
        if (strength<=0 && target>0) f->phase=0;
        f->from=strength; f->to=target; f->elapsed=0;
    }
    f->elapsed += fmaxf(0,dt);
    f->phase = fmodf(f->phase+fmaxf(0,dt),r->pulse_seconds);
    p=qlcg_clampf(f->elapsed/r->fade_seconds,0,1);
    strength=f->from+(f->to-f->from)*p*p*(3-2*p);
    return strength*(1-r->pulse_depth*(1-cosf(f->phase*QLCG_TAU/r->pulse_seconds))*.5f);
}

void qlcg_collision_bounds(DecorationShape *s, const float *v, int count) {
    float lo[3]={FLT_MAX,FLT_MAX,FLT_MAX}, hi[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX};
    memset(s,0,sizeof(*s)); s->kind=COLLISION_NONE;
    if (!v || count<=0) return;
    for (int i=0;i<count;i++) for (int j=0;j<3;j++) {
        lo[j]=fminf(lo[j],v[i*3+j]); hi[j]=fmaxf(hi[j],v[i*3+j]);
    }
    *s=(DecorationShape){(lo[0]+hi[0])*.5f,(lo[2]+hi[2])*.5f,lo[1],hi[1],
                         (hi[0]-lo[0])*.5f,(hi[2]-lo[2])*.5f,HARD_ROCK};
}

bool qlcg_collision_stable(const char *id, int season, DecorationShape *out) {
    static JsVal *data;
    static bool loaded;
    static const char *seasons[]={"spring","summer","autumn","winter"};
    if (!loaded) { data=qlcg_js_load("content/data/world/decoration-collision-bounds.json",NULL); loaded=true; }
    char key[192];
    snprintf(key,sizeof(key),"%s:%s",id,seasons[qlcg_clampi(season,0,3)]);
    const JsVal *a=qlcg_js_get(data,key);
    if (qlcg_js_count(a)!=7) return false;
    float v[7];
    for (int i=0;i<7;i++) v[i]=(float)qlcg_js_num(qlcg_js_at(a,i),0);
    *out=(DecorationShape){v[0],v[1],v[2],v[3],v[4],v[5],(int)v[6]};
    return true;
}

float qlcg_plant_instance_scale(float height, double model_height) {
    return height/(float)(model_height>.01 ? model_height : 1);
}

float qlcg_rock_instance_scale(float width, double model_radius) {
    return qlcg_clampf(width/(float)(2*(model_radius>.05 ? model_radius : 1)),.35f,2.4f);
}

static void place_shape(PropInstance *p, const DecorationShape *src, float scale) {
    p->collider.kind=COLLISION_NONE;
    if (!src || src->kind==COLLISION_NONE) return;
    float c=cosf(p->rot_y), s=sinf(p->rot_y);
    DecorationShape *v=&p->collider;
    *v=*src;
    v->x=p->x+(c*src->x-s*src->z)*scale;
    v->z=p->z+(s*src->x+c*src->z)*scale;
    v->bottom=p->y+src->bottom*scale; v->top=p->y+src->top*scale;
    v->rx=(src->kind==HARD_ROCK ? fabsf(c)*src->rx+fabsf(s)*src->rz : fmaxf(src->rx,src->rz))*scale;
    v->rz=(src->kind==HARD_ROCK ? fabsf(s)*src->rx+fabsf(c)*src->rz : fmaxf(src->rx,src->rz))*scale;
    max_reach=fmaxf(max_reach,fmaxf(fabsf(v->x)+v->rx,fabsf(v->z)+v->rz));
}

void qlcg_region_build_colliders(RegionData *reg, int season, int form) {
    qlcg_plant_material_init(); qlcg_rock_material_init();
    for (int i=0;i<reg->tree_count;i++) {
        PropInstance *p=&reg->trees[i];
        int visual=qlcg_clampi(p->visual_season,0,3);
        int model_form=qlcg_plant_model_form(form,p->shape_variant);
        const LpGeometry *g=qlcg_plant_geometry(p->species,model_form,visual);
        place_shape(p,qlcg_plant_collision_shape(p->species,model_form,visual),
                    qlcg_plant_instance_scale(p->scale_y,g?g->height:1));
    }
    for (int i=0;i<reg->rock_count;i++) {
        PropInstance *p=&reg->rocks[i];
        const RockGeometry *g=qlcg_rock_geometry(p->archetype,0,season,1);
        place_shape(p,qlcg_rock_collision_shape(p->archetype,0,season,1),
                    qlcg_rock_instance_scale(p->scale_x,g?g->radius:1));
    }
}

static bool overlaps(const DecorationShape *s, float x, float z, float half) {
    float dx=fmaxf(0,fabsf(x-s->x)-half), dz=fmaxf(0,fabsf(z-s->z)-half);
    return s->kind==HARD_ROCK ? dx<s->rx && dz<s->rz : dx*dx+dz*dz<s->rx*s->rx;
}

/* 复用流式槽索引查邻块 */
static float query(const RegionData *reg, float x, float y, float z, float height, float half,
                   int mode, float rock_top) {
    float result=mode==1 ? -INFINITY : 0;
    if (!reg) return result;
    int cx0=reg->center_cx, cx1=cx0, cz0=reg->center_cz, cz1=cz0;
    if (reg->stream) {
        cx0+=(int)floorf((x-half-max_reach)/CHUNK_SIZE);
        cx1+=(int)ceilf((x+half+max_reach)/CHUNK_SIZE);
        cz0+=(int)floorf((z-half-max_reach)/CHUNK_SIZE);
        cz1+=(int)ceilf((z+half+max_reach)/CHUNK_SIZE);
    }
    for (int cz=cz0;cz<=cz1;cz++) for (int cx=cx0;cx<=cx1;cx++) {
        const RegionData *c=reg->stream ? reg->stream->slots[qlcg_stream_slot(cx,cz)].data : reg;
        if (!c || c->center_cx!=cx || c->center_cz!=cz) continue;
        float lx=x+(reg->center_cx-cx)*CHUNK_SIZE, lz=z+(reg->center_cz-cz)*CHUNK_SIZE;
        for (int kind=0;kind<2;kind++) {
            const PropInstance *props=kind ? c->rocks : c->trees;
            int n=kind ? c->rock_count : c->tree_count;
            for (int i=0;i<n;i++) {
                const DecorationShape *s=&props[i].collider;
                bool hard=s->kind==HARD_TRUNK || s->kind==HARD_ROCK;
                if (!overlaps(s,lx,lz,half)) continue;
                if (mode==1) {
                    if (!hard || s->top>y+1e-5f || s->top<=height) continue;
                    if (kind && s->top>rock_top+1e-5f &&
                        qlcg_rock_size_class(props[i].archetype)!=ROCK_SIZE_LEGACY) continue;
                    result=fmaxf(result,s->top);
                } else if (y<s->top-1e-5f && y+height>s->bottom+1e-5f) {
                    if ((mode==0 && hard) || (mode==2 && s->kind==SOFT_SHRUB)) return 1;
                    if (mode==3 && s->kind==PLANT_TRIGGER) result++;
                }
            }
        }
    }
    return result;
}

bool qlcg_region_blocked(const RegionData *r,float x,float y,float z,float h,float half) { return qlcg_region_ice_blocked(r,x,y,z,h,half) || query(r,x,y,z,h,half,0,0)>0; }
float qlcg_region_landing(const RegionData *r,float x,float z,float from,float to,float half) { return query(r,x,from,z,to,half,1,from); }
float qlcg_region_step_surface(const RegionData *r,float x,float z,float feet,float step,float half) {
    return query(r,x,feet+step,z,feet-step-1e-5f,half,1,feet+fminf(step,.30f));
}
float qlcg_region_brush_speed(const RegionData *r,float x,float y,float z,float h,float half) {
    return query(r,x,y,z,h,half,2,0)>0 ? qlcg_movement_rules()->shrub_speed_factor : 1;
}
int qlcg_region_plant_contacts(const RegionData *r,float x,float y,float z,float h,float half) { return (int)query(r,x,y,z,h,half,3,0); }
