#include "water_reflection.h"
#include "terrain_visual.h"
#include "core/math_util.h"
#include <math.h>
#include <string.h>

static void release(WaterReflection *p) {
    if(p->framebuffer) qlcg_gl_delete_framebuffers(1,&p->framebuffer);
    if(p->texture) qlcg_gl_delete_textures(1,&p->texture);
    if(p->depth) qlcg_gl_delete_renderbuffers(1,&p->depth);
    memset(p,0,sizeof(*p));
}
void qlcg_reflection_free(TerrainRenderer *tr) {
    for(int k=0;k<2;k++) release(tr->reflection_targets+k);
}
static int target(WaterReflection *p,int width,int height) {
    if(p->framebuffer&&p->width==width&&p->height==height) return 1;
    release(p);p->width=width;p->height=height;
    glGenFramebuffers(1,&p->framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,p->framebuffer);
    glGenTextures(1,&p->texture);glBindTexture(GL_TEXTURE_2D,p->texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,p->texture,0);
    glGenRenderbuffers(1,&p->depth);glBindRenderbuffer(GL_RENDERBUFFER,p->depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,p->depth);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) { release(p);return 0; }
    return 1;
}
static bool same_plane(const WaterReflection *a,const WaterReflection *b) {
    if(!a->valid||!b->valid||a->kind!=b->kind) return false;
    float tolerance=a->kind==WATER_MIRROR_RIVER?.55f:.20f;
    if(fabsf(a->y-b->y)>tolerance) return false;
    if(a->kind==WATER_MIRROR_SEA||a->kind==WATER_MIRROR_STILL) return true;
    float dx=a->x-b->x,dz=a->z-b->z;
    float span=a->kind==WATER_MIRROR_RIVER?144:64;
    return dx*dx+dz*dz<span*span;
}
static bool preferred(const WaterReflection *a,const WaterReflection *b) {
    return !b->valid||a->score>b->score||(a->score==b->score&&a->seed<b->seed);
}
static void offer(WaterReflection choices[2],WaterReflection candidate) {
    for(int k=0;k<2;k++) {
        bool same_river_budget=choices[k].valid&&choices[k].kind==WATER_MIRROR_RIVER&&
            candidate.kind==WATER_MIRROR_RIVER;
        if(!same_river_budget&&!same_plane(choices+k,&candidate)) continue;
        if(!preferred(&candidate,choices+k)) return;
        choices[k]=candidate;
        if(k==1&&preferred(choices+1,choices)) {
            choices[1]=choices[0];
            choices[0]=candidate;
        }
        return;
    }
    if(preferred(&candidate,choices)) {
        choices[1]=choices[0];
        choices[0]=candidate;
    } else if(preferred(&candidate,choices+1)) choices[1]=candidate;
}
void qlcg_reflection_collect(WaterReflection choices[2],const WaterReflection old[2],const RegionData *c,
    const TerrainChunkGPU *chunk,float ox,float oz,const float camera[3],const float forward[3],const float mvp[16]) {
    if(c->water_form==WATER_FORM_CUTOFF||!chunk->water_idx_count) return;
    for(int k=0;k<WATER_MIRROR_TYPES;k++) {
        const WaterMirrorPatch *s=chunk->water_patches+k;
        if(s->area<=0) continue;
        float x=ox+s->x,z=oz+s->z,y=s->y;
        float dx=x-camera[0],dy=y-camera[1],dz=z-camera[2],d2=dx*dx+dy*dy+dz*dz;
        float extent_m=CHUNK_SIZE*.72f;
        if(d2>512*512||camera[1]<y+.1f||dx*forward[0]+dy*forward[1]+dz*forward[2]<-extent_m) continue;
        float clip[4],extent[4];
        for(int j=0;j<4;j++) {
            clip[j]=mvp[j]*x+mvp[4+j]*y+mvp[8+j]*z+mvp[12+j];
            extent[j]=extent_m*hypotf(mvp[j],mvp[8+j]);
        }
        if(clip[3]+extent[3]<=0||fabsf(clip[0])>clip[3]+extent[0]+extent[3]||
           fabsf(clip[1])>clip[3]+extent[1]+extent[3]) continue;
        WaterReflection p={0};
        p.seed=(uint32_t)c->center_cx*0x9e3779b9u^(uint32_t)c->center_cz*0x85ebca6bu^(uint32_t)k;
        p.x=x;p.y=y;p.z=z;p.kind=k+1;p.valid=1;
        p.radius=p.kind==WATER_MIRROR_RIVER?120:p.kind==WATER_MIRROR_POOL?84:1;
        float facing=fmaxf(.16f,fabsf(dy)/sqrtf(fmaxf(1,d2)));
        p.score=s->area*facing/fmaxf(16,d2);
        if(p.score<.0015f) continue;
        for(int j=0;j<2;j++) if(same_plane(old+j,&p)) { p.score*=1.2f;break; }
        offer(choices,p);
    }
}
void qlcg_reflection_clip(float p[16],const float view[16],float camera_y,float plane) {
    float n[4]={view[4],view[5],view[6],camera_y-plane-.03f};
    float q[4]={copysignf(1,n[0])/p[0],copysignf(1,n[1])/p[5],-1,(1+p[10])/p[14]};
    float dot=0;for(int k=0;k<4;k++) dot+=n[k]*q[k];
    if(fabsf(dot)<.001f) return;
    float scale=2/dot;p[2]=n[0]*scale;p[6]=n[1]*scale;p[10]=n[2]*scale+1;p[14]=n[3]*scale;
}
void qlcg_reflection_render(TerrainRenderer *tr,const WaterReflection choices[2],const RegionData *reg,
    const MacroWorld *w,const CalendarClock *clock,int utc,const GameSettings *s,int width,int height) {
    if(!s->mirror_reflections||(!choices[0].valid&&!choices[1].valid)) {
        for(int k=0;k<2;k++)tr->reflection_targets[k].valid=0;
        return;
    }
    GLint framebuffer,viewport[4];glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);glGetIntegerv(GL_VIEWPORT,viewport);
    float scale=fminf(.5f,1024.0f/fmaxf(width,height));
    int rw=(int)fmaxf(1,floorf(width*scale)),rh=(int)fmaxf(1,floorf(height*scale));
    float shake=tr->cam_shake;tr->cam_shake=0;
    bool msaa=tr->msaa_active;tr->msaa_active=false;
    for(int k=0;k<2;k++) {
        WaterReflection *p=tr->reflection_targets+k;p->valid=0;
        if(!choices[k].valid) continue;
        glActiveTexture(GL_TEXTURE0);
        if(!target(p,rw,rh)) continue;
        p->x=choices[k].x;p->y=choices[k].y;p->z=choices[k].z;p->radius=choices[k].radius;
        p->seed=choices[k].seed;p->kind=choices[k].kind;
        glBindFramebuffer(GL_FRAMEBUFFER,p->framebuffer);glViewport(0,0,rw,rh);
        glDepthMask(GL_TRUE);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        tr->reflection_plane=p->y;tr->reflection_pass=k+1;
        qlcg_terrain_render(tr,reg,w,clock,utc,s,rw,rh);p->valid=1;
    }
    tr->reflection_pass=0;tr->cam_shake=shake;tr->msaa_active=msaa;
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
}
void qlcg_reflection_uniforms(TerrainRenderer *tr) {
    GLuint program=qlcg_visual_program(tr,tr->water_prog,QL_VIS_WATER);
    static const char *matrices[2]={"uWaterMirrorMatrix0","uWaterMirrorMatrix1"};
    static const char *bounds[2]={"uWaterMirrorBounds0","uWaterMirrorBounds1"};
    static const char *kinds[2]={"uWaterMirrorKind0","uWaterMirrorKind1"};
    for(int k=0;k<2;k++) {
        WaterReflection *p=tr->reflection_targets+k;glActiveTexture(GL_TEXTURE0+4+k);glBindTexture(GL_TEXTURE_2D,p->texture);
        glUniformMatrix4fv(qlcg_gl_uniform(program,matrices[k]),1,GL_FALSE,p->matrix);
        glUniform4f(qlcg_gl_uniform(program,bounds[k]),p->x,p->y,p->z,p->valid?p->radius:0);
        glUniform1i(qlcg_gl_uniform(program,kinds[k]),p->valid?p->kind:0);
    }
    glActiveTexture(GL_TEXTURE0);
}
