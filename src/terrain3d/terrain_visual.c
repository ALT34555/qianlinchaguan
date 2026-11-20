/* 画卷与增强光影 */
#include "terrain_visual.h"
#include "terrain_shaders.h"
#include "paint_shaders.h"
#include "paint_clouds.h"
#include "terrain_types.h"
#include "terrain_visibility.h"
#include "terrain_metrics.h"
#include "core/math_util.h"
#include "systems/world/model/plant_material.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAINT_STROKES 16000
#define SHADOW_SIZE 2048

enum {
    PU_PAINTPALETTE, PU_PAINTPAPER, PU_PAINTSKY, PU_PAINTSNOW, PU_PAINTSNOWFAR, PU_PAINTFOG,
    PU_VP, PU_EYE, PU_VIEWPORT, PU_FOGRANGE, PU_PALETTE, PU_PIGMENT, PU_INK, PU_BASEY,
    PU_SNOWAMT, PU_WET, PU_HAZE, PU_DUST, PU_SKYTINT, PU_DAYLIGHT, PU_DETAIL,
    PU_TIME, PU_UNDERWATER, PU_WATERFOG,
    PU_BOUNDARY, PU_BOUNDARYORIGIN, PU_BOUNDARYSIZE, PU_BOUNDARYGUARD, PU_COUNT
};
typedef struct { GLuint program;GLint location[PU_COUNT]; } PaintBindings;
static const char *paint_uniform_names[PU_COUNT]={
    "uPaintPalette","uPaintPaper","uPaintSky","uPaintSnow","uPaintSnowFar","uPaintFog",
    "uVP","uEye","uViewport","uFogRange","uPalette","uPigment","uInk","uBaseY",
    "uSnowAmt","uWet","uHaze","uDust","uSkyTint","uDaylight","uDetail",
    "uTime","uUnderwater","uWaterFog",
    "uBoundary","uBoundaryOrigin","uBoundarySize","uBoundaryGuard"
};
enum {
    PE_TEX, PE_DEPTH, PE_TERRAIN, PE_PROJECTION, PE_PALETTE, PE_TEXEL, PE_CLIPRANGE,
    PE_EDGERANGE, PE_STRENGTH, PE_WASH, PE_LINEWIDTH, PE_INK, PE_INKDEEP, PE_COUNT
};
static const char *paint_edge_names[PE_COUNT]={
    "uTex","uDepth","uTerrain","uProjection","uPalette","uTexel","uClipRange",
    "uEdgeRange","uStrength","uWash","uLineWidth","uInk","uInkDeep"
};
static void paint_bindings_init(PaintBindings *b,GLuint program) {
    b->program=program;
    for(int i=0;i<PU_COUNT;i++)b->location[i]=qlcg_gl_uniform(program,paint_uniform_names[i]);
}
static void paint_float(const PaintBindings *b,int name,float value) {
    GLint location=b->location[name];if(location>=0)glUniform1f(location,value);
}
static void paint_int(const PaintBindings *b,int name,int value) {
    GLint location=b->location[name];if(location>=0)glUniform1i(location,value);
}
static void paint_vec2(const PaintBindings *b,int name,float x,float y) {
    GLint location=b->location[name];if(location>=0)glUniform2f(location,x,y);
}
static void paint_vec3(const PaintBindings *b,int name,const float *value) {
    GLint location=b->location[name];if(location>=0)glUniform3fv(location,1,value);
}
static void paint_matrix(const PaintBindings *b,int name,const float *value) {
    GLint location=b->location[name];if(location>=0)glUniformMatrix4fv(location,1,GL_FALSE,value);
}

typedef struct { float a[3], b[3], style[3]; } PaintStroke;
typedef struct {
    GLuint programs[QL_VIS_COUNT], sky, edge, stroke, vao, stroke_vao, stroke_vbo;
    GLuint fbo, color, depth, terrain, ms_fbo, ms_color, ms_depth, ms_terrain;
    GLint output, viewport[4], depth_func;
    int width, height, samples, requested_samples, stroke_count;
    PaintStroke *strokes;
    PaintBindings bindings[QL_VIS_COUNT+2];
    GLint edge_loc[PE_COUNT];
    GLuint boundary;
    unsigned char boundary_data[QLCG_STREAM_CAP];
    float boundary_origin[2];
    uint64_t revision,mesh;
    double anchor_x, anchor_z;
    int center_x, center_z;
    bool ready, failed, drawing;
} PaintVisual;

typedef struct {
    GLuint fbo, texture, terrain, plant;
    float vp[16], sun[3], center[3], time;
    uint64_t revision, props,mesh;
    double anchor_x, anchor_z;
    bool valid, failed;
} LightVisual;

struct TerrainVisual {
    PaintVisual paint;
    LightVisual light;
    TerrainVisualFrame frame;
    PaintClouds *clouds;
};

static char *compatible_source(const char *source) {
    size_t n=strlen(source)+1;
    char *copy=malloc(n);
    if(!copy) return NULL;
    memcpy(copy,source,n);
    if(qlcg_gl_caps.es) {
        char *p;
        while((p=strstr(copy,"noperspective "))!=NULL) memmove(p,p+14,strlen(p+14)+1);
    }
    return copy;
}

static GLuint paint_program(const char *vs,const char *body,bool clouds) {
    const char *cloud="";
    (void)clouds;
    const char *source=body;
    if(!strncmp(source,"#version",8)) { source=strchr(source,'\n');if(!source)return 0;source++; }
    const char *prefix="layout(location=1) out vec4 qlTerrain;\n#define main paint_material_main\n";
    const char *suffix=body==GB_SURFACE_FS_BODY?
        "\n#undef main\nvoid main(){paint_material_main();qlTerrain=vec4(paintBoundaryTag,0.0,0.0,1.0);}\n":
        body==GB_PLANT_FS_BODY?
        "\n#undef main\nvoid main(){paint_material_main();qlTerrain=vec4(0.0,uIsRock==0?1.0:0.0,0.0,1.0);}\n":
        "\n#undef main\nvoid main(){paint_material_main();qlTerrain=vec4(0.0,0.0,0.0,1.0);}\n";
    size_t n=strlen(GB_LIB_A)+strlen(GB_LIB_B)+strlen(cloud)+strlen(prefix)+strlen(source)+strlen(suffix)+1;
    char *fs=malloc(n),*vertex=compatible_source(vs);
    if(!fs||!vertex) { free(fs);free(vertex);return 0; }
    snprintf(fs,n,"%s%s%s%s%s%s",GB_LIB_A,GB_LIB_B,cloud,prefix,source,suffix);
    char *fragment=compatible_source(fs);
    GLuint program=fragment?qlcg_gl_compile_program(vertex,fragment):0;
    free(fs);free(vertex);free(fragment);
    qlcg_visual_program_init(program);
    return program;
}

static void paint_targets_free(PaintVisual *p) {
    GLuint textures[]={p->color,p->depth,p->terrain};
    GLuint fbos[]={p->fbo,p->ms_fbo};
    GLuint buffers[]={p->ms_color,p->ms_depth,p->ms_terrain};
    qlcg_gl_delete_textures(3,textures);
    qlcg_gl_delete_framebuffers(2,fbos);
    qlcg_gl_delete_renderbuffers(3,buffers);
    p->fbo=p->color=p->depth=p->ms_fbo=p->ms_color=p->ms_depth=0;
    p->terrain=p->ms_terrain=0;
    p->width=p->height=p->samples=0;
}

static bool paint_init(PaintVisual *p) {
    if(p->ready||p->failed) return p->ready;
    p->failed=true;
    p->programs[QL_VIS_TERRAIN]=paint_program(PAINT_TERR_VS,GB_SURFACE_FS_BODY,false);
    p->programs[QL_VIS_PLANT]=paint_program(PAINT_PLANT_VS,GB_PLANT_FS_BODY,false);
    p->programs[QL_VIS_WATER]=paint_program(WATER_VS_SRC,GB_WATER_FS_BODY,false);
    p->programs[QL_VIS_ICE]=paint_program(ICE_VS_SRC,GB_ICE_FS_BODY,false);
    p->programs[QL_VIS_PROP]=paint_program(PROP_VS_SRC,PAINT_PROP_FS_BODY,false);
    p->programs[QL_VIS_FLAG]=paint_program(FLAG_VS_SRC,PAINT_PROP_FS_BODY,false);
    p->programs[QL_VIS_FAR]=paint_program(PAINT_FAR_VS,PAINT_FAR_FS_BODY,false);
    p->programs[QL_VIS_ICE_FLOW]=paint_program(ICEP_VS_SRC,PAINT_ICE_FLOW_FS_BODY,false);
    p->programs[QL_VIS_CRACK]=paint_program(CRACK_VS_SRC,PAINT_CRACK_FS_BODY,false);
    p->programs[QL_VIS_SPRING]=paint_program(SPRING_VS_SRC,SPRING_FS_SRC,false);
    p->programs[QL_VIS_WEATHER]=paint_program(WEATHER_VS_SRC,WEATHER_FS_SRC,false);
    p->sky=paint_program(GB_PAPER_VS,GB_PAPER_FS_BODY,true);
    p->stroke=paint_program(GB_STROKE_VS,GB_STROKE_FS_BODY,false);
    p->edge=qlcg_gl_compile_program(PAINT_FULL_VS,PAINT_EDGE_FS);
    for(int i=0;i<QL_VIS_COUNT;i++) if(!p->programs[i]) return false;
    if(!p->sky||!p->stroke||!p->edge) return false;
    p->strokes=calloc(PAINT_STROKES,sizeof(PaintStroke));
    if(!p->strokes) return false;
    glGenVertexArrays(1,&p->vao);
    glGenVertexArrays(1,&p->stroke_vao);
    glGenBuffers(1,&p->stroke_vbo);
    glBindVertexArray(p->stroke_vao);
    glBindBuffer(GL_ARRAY_BUFFER,p->stroke_vbo);
    glBufferData(GL_ARRAY_BUFFER,PAINT_STROKES*sizeof(PaintStroke),NULL,GL_DYNAMIC_DRAW);
    for(int i=0;i<3;i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i,3,GL_FLOAT,GL_FALSE,sizeof(PaintStroke),(void *)(size_t)(i*12));
        glVertexAttribDivisor(i,1);
    }
    glBindVertexArray(0);
    for(int i=0;i<QL_VIS_COUNT;i++)paint_bindings_init(p->bindings+i,p->programs[i]);
    paint_bindings_init(p->bindings+QL_VIS_COUNT,p->sky);
    paint_bindings_init(p->bindings+QL_VIS_COUNT+1,p->stroke);
    for(int i=0;i<PE_COUNT;i++)p->edge_loc[i]=qlcg_gl_uniform(p->edge,paint_edge_names[i]);
    p->revision=UINT64_MAX;
    p->ready=true;p->failed=false;
    return true;
}

static GLuint paint_texture(int w,int h,GLint format,GLenum layout,GLenum type) {
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,layout,type,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return texture;
}

static bool paint_target(PaintVisual *p,int w,int h,int samples) {
    if(p->fbo&&p->width==w&&p->height==h&&p->samples==samples) return true;
    paint_targets_free(p);
    if(w<1||h<1||w>qlcg_gl_caps.max_texture||h>qlcg_gl_caps.max_texture) return false;
    glActiveTexture(GL_TEXTURE0);
    p->color=paint_texture(w,h,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    p->depth=paint_texture(w,h,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT);
    p->terrain=paint_texture(w,h,GL_RG8,GL_RG,GL_UNSIGNED_BYTE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glGenFramebuffers(1,&p->fbo);glBindFramebuffer(GL_FRAMEBUFFER,p->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,p->color,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,p->depth,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,p->terrain,0);
    const GLenum attachments[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2,attachments);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) { paint_targets_free(p);return false; }
    if(samples>0) {
        glGenFramebuffers(1,&p->ms_fbo);glBindFramebuffer(GL_FRAMEBUFFER,p->ms_fbo);
        glGenRenderbuffers(1,&p->ms_color);glBindRenderbuffer(GL_RENDERBUFFER,p->ms_color);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_RGBA8,w,h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,p->ms_color);
        glGenRenderbuffers(1,&p->ms_depth);glBindRenderbuffer(GL_RENDERBUFFER,p->ms_depth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_DEPTH_COMPONENT24,w,h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,p->ms_depth);
        glGenRenderbuffers(1,&p->ms_terrain);glBindRenderbuffer(GL_RENDERBUFFER,p->ms_terrain);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_RG8,w,h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_RENDERBUFFER,p->ms_terrain);
        glDrawBuffers(2,attachments);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) { paint_targets_free(p);return false; }
    }
    p->width=w;p->height=h;p->samples=samples;
    return true;
}

static bool paint_prepare(PaintVisual *p,int width,int height,int samples) {
    if(p->fbo&&p->width==width&&p->height==height&&p->requested_samples==samples)return true;
    bool ready=paint_target(p,width,height,samples);
    if(!ready&&samples)ready=paint_target(p,width,height,0);
    if(ready)p->requested_samples=samples;return ready;
}
static void light_matrix(float *matrix,const float *eye,const float *center) {
    float f[3]={center[0]-eye[0],center[1]-eye[1],center[2]-eye[2]};
    float len=sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    for(int i=0;i<3;i++) f[i]/=fmaxf(len,0.001f);
    float up[3]={0,1,0};if(fabsf(f[1])>0.97f) { up[1]=0;up[2]=1; }
    float r[3]={f[1]*up[2]-f[2]*up[1],f[2]*up[0]-f[0]*up[2],f[0]*up[1]-f[1]*up[0]};
    len=sqrtf(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]);
    for(int i=0;i<3;i++) r[i]/=fmaxf(len,0.001f);
    float u[3]={r[1]*f[2]-r[2]*f[1],r[2]*f[0]-r[0]*f[2],r[0]*f[1]-r[1]*f[0]};
    memset(matrix,0,16*sizeof(float));
    for(int i=0;i<3;i++) { matrix[i*4]=r[i];matrix[i*4+1]=u[i];matrix[i*4+2]=-f[i]; }
    matrix[12]=-(r[0]*eye[0]+r[1]*eye[1]+r[2]*eye[2]);
    matrix[13]=-(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]);
    matrix[14]=f[0]*eye[0]+f[1]*eye[1]+f[2]*eye[2];matrix[15]=1;
}

static bool light_init(LightVisual *l) {
    if(l->fbo||l->failed) return l->fbo!=0;
    l->failed=true;
    const char *fs="void main(){}\n";
    l->terrain=qlcg_gl_compile_program(TERR_VS_SRC,fs);
    l->plant=qlcg_gl_compile_program(PLANT_VS_SRC,fs);
    if(!l->terrain||!l->plant) return false;
    l->texture=paint_texture(SHADOW_SIZE,SHADOW_SIZE,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL);
    glGenFramebuffers(1,&l->fbo);glBindFramebuffer(GL_FRAMEBUFFER,l->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,l->texture,0);
    GLenum none=GL_NONE;glDrawBuffers(1,&none);glReadBuffer(GL_NONE);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
        qlcg_gl_delete_framebuffers(1,&l->fbo);l->fbo=0;return false;
    }
    l->failed=false;
    return true;
}

static void light_render(TerrainRenderer *tr,const RegionData *reg,const TerrainVisualFrame *f) {
    LightVisual *l=&tr->visual->light;
    if(f->sky.sun_dir[1]<=0) { l->valid=false;return; }
    GLint framebuffer=f->output,depth_func;
    const GLint *viewport=f->viewport;
    if(!light_init(l)) { glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);return; }
    float center[3]={floorf(f->camera[0]/4)*4,f->camera[1]+8,floorf(f->camera[2]/4)*4};
    const float *sun=f->sky.sun_dir;
    float delta=fabsf(sun[0]-l->sun[0])+fabsf(sun[1]-l->sun[1])+fabsf(sun[2]-l->sun[2]);
    if(l->valid&&l->revision==reg->stream->revision&&l->props==tr->props_revision&&l->mesh==tr->mesh_version&&
       l->anchor_x==tr->anchor_x&&l->anchor_z==tr->anchor_z&&delta<0.004f&&
       hypotf(center[0]-l->center[0],center[2]-l->center[2])<8&&fabsf(center[1]-l->center[1])<4&&
       (!f->settings->natural_physics||f->weather.wind<0.1f||fabsf(f->time-l->time)<0.12f)) {
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);return;
    }
    glGetIntegerv(GL_DEPTH_FUNC,&depth_func);
    float eye[3]={center[0]+sun[0]*320,center[1]+sun[1]*320,center[2]+sun[2]*320};
    float view[16],proj[16]={0};light_matrix(view,eye,center);
    proj[0]=proj[5]=1.0f/128;proj[10]=-2.0f/799;proj[14]=-801.0f/799;proj[15]=1;
    qlcg_mat4_mul(proj,view,l->vp);
    TerrainFrustum frustum;qlcg_frustum_set(&frustum,l->vp);
    glBindFramebuffer(GL_FRAMEBUFFER,l->fbo);glViewport(0,0,SHADOW_SIZE,SHADOW_SIZE);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(l->terrain);
    glUniformMatrix4fv(qlcg_gl_uniform(l->terrain,"uMVP"),1,GL_FALSE,l->vp);
    glUniform3f(qlcg_gl_uniform(l->terrain,"uCamPos"),0,0,0);
    glBindBuffer(GL_ARRAY_BUFFER,tr->terrain_vbo);
    int md_count=0;
    for(int i=0;i<reg->stream->count;i++) {
        int slot=reg->stream->active[i];const RegionData *c=reg->stream->slots[slot].data;
        TerrainChunkGPU *g=tr->chunks+slot;
        if(!c||!g->terrain_ready||!qlcg_frustum_bounds(&frustum,&g->bounds)) continue;
        float x=(float)((double)c->center_x-tr->anchor_x),z=(float)((double)c->center_z-tr->anchor_z);
        if(fabsf(x-center[0])>192||fabsf(z-center[2])>192) continue;
        if(g->cliff_vao&&g->cliff_vertex_count) {
            glBindVertexArray(g->cliff_vao);glDrawArrays(GL_TRIANGLES,0,g->cliff_vertex_count);
        }
        tr->md_count[md_count]=tr->terrain_idx_count;tr->md_index[md_count]=0;
        tr->md_base[md_count++]=g->terrain_slot*(CHUNK_CELLS+1)*(CHUNK_CELLS+1);
    }
    glBindVertexArray(tr->terrain_vao);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,tr->terrain_ebo);
    glBindBuffer(GL_ARRAY_BUFFER,tr->terrain_vbo);
    if(md_count&&glMultiDrawElementsBaseVertex) {
        glMultiDrawElementsBaseVertex(GL_TRIANGLES,tr->md_count,GL_UNSIGNED_INT,
            (const void *const*)tr->md_index,md_count,tr->md_base);
    } else for(int i=0;i<md_count;i++) {
            glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(TerrainVert),
                (void *)((size_t)tr->md_base[i]*sizeof(TerrainVert)));
            glDrawElements(GL_TRIANGLES,tr->terrain_idx_count,GL_UNSIGNED_INT,0);
    }
    glBindVertexArray(tr->terrain_vao);glBindBuffer(GL_ARRAY_BUFFER,tr->terrain_vbo);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(TerrainVert),0);
    glUseProgram(l->plant);
    glUniformMatrix4fv(qlcg_gl_uniform(l->plant,"uMVP"),1,GL_FALSE,l->vp);
    glUniform3f(qlcg_gl_uniform(l->plant,"uCamPos"),0,0,0);
    glUniform1f(qlcg_gl_uniform(l->plant,"uTime"),f->time);
    GLint height=qlcg_gl_uniform(l->plant,"uHeight"),shrub=qlcg_gl_uniform(l->plant,"uShrub");
    GLint grass=qlcg_gl_uniform(l->plant,"uGrass");
    for(int kind=0;kind<2;kind++) {
        PlantBatch *batches=kind?tr->rocks:tr->plants;
        int count=kind?tr->rock_batch_count:tr->plant_batch_count;
        glUniform1f(qlcg_gl_uniform(l->plant,"uEnable"),!kind&&f->settings->natural_physics?1:0);
        for(int i=0;i<count;i++) {
            PlantBatch *b=batches+i;if(!b->vao||b->inst_count<=0) continue;
            int visible=qlcg_batch_visible(b,l->vp,&frustum,QL_VIEW_SHADOW);
            if(!visible)continue;
            glUniform1f(height,kind?1:b->height);
            glUniform1f(shrub,b->category==PLANT_CAT_GUAN?1:0);
            glUniform1f(grass,b->category==PLANT_CAT_CAO?1:0);
            if(visible)glDrawElementsInstanced(GL_TRIANGLES,b->idx_count,GL_UNSIGNED_INT,0,visible);
        }
    }
    memcpy(l->sun,sun,sizeof(l->sun));memcpy(l->center,center,sizeof(l->center));
    l->mesh=tr->mesh_version;l->revision=reg->stream->revision;l->props=tr->props_revision;l->time=f->time;
    l->anchor_x=tr->anchor_x;l->anchor_z=tr->anchor_z;l->valid=true;
    glBindVertexArray(0);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glDepthFunc(depth_func);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
}

bool qlcg_visual_paint_ready(TerrainRenderer *tr) {
    if(!tr->visual)tr->visual=calloc(1,sizeof(*tr->visual));
    return tr->visual&&paint_init(&tr->visual->paint);
}
bool qlcg_visual_paint_target_ready(TerrainRenderer *tr,int width,int height,int samples) {
    if(tr->visual){PaintVisual *p=&tr->visual->paint;if(p->fbo&&p->width==width&&p->height==height&&p->requested_samples==samples)return true;}
    GLint output,active,texture;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&output);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    bool ready=qlcg_visual_paint_ready(tr)&&paint_prepare(&tr->visual->paint,width,height,samples);
    glBindFramebuffer(GL_FRAMEBUFFER,output);glBindTexture(GL_TEXTURE_2D,texture);glActiveTexture(active);
    return ready;
}
static void paint_boundary_prepare(TerrainRenderer *tr,const RegionData *reg) {
    PaintVisual *p=&tr->visual->paint;const RegionStream *st=reg->stream;
    if(!st||!tr->chunks)return;
    unsigned char cells[QLCG_STREAM_CAP]={0},flags[QLCG_STREAM_CAP]={0};
    const int side=QLCG_STREAM_SIDE,half=side/2;
    for(int i=0;i<st->count;i++) {
        int slot=st->active[i];const RegionData *c=st->slots[slot].data;
        if(!c||!tr->chunks[slot].terrain_ready)continue;
        int x=c->center_cx-st->center_cx,z=c->center_cz-st->center_cz;
        if(abs(x)>half||abs(z)>half||x*x+z*z>(st->radius+.5f)*(st->radius+.5f))continue;
        cells[(z+half)*side+x+half]=1;
    }
    for(int z=0;z<side;z++)for(int x=0;x<side;x++) {
        int i=z*side+x;if(!cells[i]){flags[i]=255;continue;}
        flags[i]=((x==0||!cells[i-1])?1:0)|((x==side-1||!cells[i+1])?2:0)|
            ((z==0||!cells[i-side])?4:0)|((z==side-1||!cells[i+side])?8:0)|
            ((x==0||z==0||!cells[i-side-1])?16:0)|
            ((x==side-1||z==0||!cells[i-side+1])?32:0)|
            ((x==0||z==side-1||!cells[i+side-1])?64:0)|
            ((x==side-1||z==side-1||!cells[i+side+1])?128:0);
    }
    bool fresh=!p->boundary;
    if(fresh)glGenTextures(1,&p->boundary);
    glActiveTexture(GL_TEXTURE0+7);glBindTexture(GL_TEXTURE_2D,p->boundary);
    if(fresh||memcmp(flags,p->boundary_data,sizeof(flags))) {
        GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8,side,side,0,GL_RED,GL_UNSIGNED_BYTE,flags);
        glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        memcpy(p->boundary_data,flags,sizeof(flags));
    }
    p->boundary_origin[0]=(float)((st->center_cx-half)*(double)CHUNK_SIZE-tr->anchor_x);
    p->boundary_origin[1]=(float)((st->center_cz-half)*(double)CHUNK_SIZE-tr->anchor_z);
    glActiveTexture(GL_TEXTURE0);
}

void qlcg_visual_begin(TerrainRenderer *tr,const RegionData *reg,const GameSettings *s,
    const SkyClimate *sky,const WeatherProfile *weather,const float *camera,const float *vp,
    float fog_near,float fog_far,float clip_far,float time,bool underwater,TerrainVisualFrame *f) {
    memset(f,0,sizeof(*f));
    f->settings=s;f->sky=*sky;f->weather=*weather;
    memcpy(f->camera,camera,sizeof(f->camera));memcpy(f->vp,vp,sizeof(f->vp));
    f->fog[0]=fog_near;f->fog[1]=fog_far;f->clip_far=clip_far;f->time=time;f->underwater=underwater;
    GLint viewport[4],output;glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_FRAMEBUFFER_BINDING,&output);
    f->width=viewport[2];f->height=viewport[3];
    f->output=output;memcpy(f->viewport,viewport,sizeof(viewport));
    if(!tr->visual&&(s->scroll_style||s->enhanced_lighting||s->decorative_clouds)) tr->visual=calloc(1,sizeof(*tr->visual));
    if(!tr->visual) return;
    PaintVisual *p=&tr->visual->paint;
    if(s->scroll_style) f->paint=paint_init(p);
    if(f->paint&&(!tr->reflection_pass||!p->boundary))paint_boundary_prepare(tr,reg);
    if(f->paint)qlcg_paint_uniforms(&f->colors,s->scroll_palette,f->sky.tint,f->sky.daylight,
        fmaxf(f->weather.rain,tr->rain_override),
        s->weather_atmosphere?qlcg_clampf((1-f->weather.fog)*1.15f,0,1):0,
        fmaxf(tr->plant_season==3?.86f:0,f->weather.snow),
        s->weather_atmosphere?f->weather.dust:0,f->fog);
    f->enhanced=s->enhanced_lighting&&!f->paint;
    if(f->enhanced&&!tr->reflection_pass){qlcg_profile_begin(QL_GPU_SHADOW);light_render(tr,reg,f);qlcg_profile_end(QL_GPU_SHADOW);}
    glBindFramebuffer(GL_FRAMEBUFFER,output);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    if(f->paint&&!tr->reflection_pass) {
        int samples=s->antialias?qlcg_clampi(qlcg_gl_caps.max_samples,0,4):0;
        if(samples<2) samples=0;
        bool ready=paint_prepare(p,f->width,f->height,samples);
        if(ready) {
            tr->msaa_active=p->ms_fbo!=0;qlcg_metrics.samples=p->samples;
            p->output=output;memcpy(p->viewport,viewport,sizeof(viewport));p->drawing=true;
            glGetIntegerv(GL_DEPTH_FUNC,&p->depth_func);glDepthFunc(GL_LEQUAL);
            glBindFramebuffer(GL_FRAMEBUFFER,p->ms_fbo?p->ms_fbo:p->fbo);
            glViewport(0,0,f->width,f->height);glDepthMask(GL_TRUE);
            glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        } else {
            f->paint=false;glBindFramebuffer(GL_FRAMEBUFFER,output);
            glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        }
    }
    tr->visual->frame=*f;
}

void qlcg_visual_resume(TerrainRenderer *tr,const TerrainVisualFrame *f) {
    if(tr->visual) tr->visual->frame=*f;
}

GLuint qlcg_visual_program(const TerrainRenderer *tr,GLuint native,TerrainVisualPass pass) {
    return tr->visual&&tr->visual->frame.paint?tr->visual->paint.programs[pass]:native;
}

void qlcg_visual_program_init(GLuint program) {
    if(!program)return;
    const char *names[]={"uShadowTex","uGroundTex0","uGroundTex1","uAtlas","uAstralMap","uWaterReflection0","uWaterReflection1"};
    const int slots[]={6,0,1,0,0,4,5};
    glUseProgram(program);
    for(int i=0;i<7;i++)glUniform1i(qlcg_gl_uniform(program,names[i]),slots[i]);
}

void qlcg_visual_bind(TerrainRenderer *tr,GLuint program) {
    glUseProgram(program);
    const float *beam = tr->equipment_beam;
    float visible = tr->reflection_pass ? 0.0f : 1.0f;
    static const char *light_uniforms[]={"uLotLights[0]","uLotLights[1]","uLotLights[2]","uLotLights[3]"};
    int lights=tr->reflection_pass?0:tr->lot_light_count;
    glUniform1i(qlcg_gl_uniform(program,"uLotLightCount"),lights);
    for(int i=0;i<lights;i++) {
        const float *p=tr->lot_lights[i];
        glUniform4f(qlcg_gl_uniform(program,light_uniforms[i]),p[0],p[1],p[2],p[3]);
    }
    glUniform4f(qlcg_gl_uniform(program,"uEquipmentBeam"),beam[0],beam[1],beam[2],beam[3]*visible);
    glUniform3fv(qlcg_gl_uniform(program,"uEquipmentOriginDelta"),1,tr->equipment_origin_delta);
    glUniform1f(qlcg_gl_uniform(program,"uEquipmentNightVision"),tr->equipment_night_vision*visible);
    glUniform1f(qlcg_gl_uniform(program,"uEquipmentAquaticVision"),tr->equipment_aquatic_vision*visible);
    if(!tr->visual)return;
    const TerrainVisualFrame *f=&tr->visual->frame;
    const GameSettings *s=f->settings;
    if(f->paint) {
        PaintVisual *p=&tr->visual->paint;PaintBindings temporary,*bindings=NULL;
        for(int i=0;i<QL_VIS_COUNT+2;i++)if(p->bindings[i].program==program){bindings=p->bindings+i;break;}
        if(!bindings){paint_bindings_init(&temporary,program);bindings=&temporary;}
        if(bindings->location[PU_BOUNDARY]>=0) {
            glActiveTexture(GL_TEXTURE0+7);glBindTexture(GL_TEXTURE_2D,p->boundary);glActiveTexture(GL_TEXTURE0);
            paint_int(bindings,PU_BOUNDARY,7);
            paint_vec2(bindings,PU_BOUNDARYORIGIN,p->boundary_origin[0],p->boundary_origin[1]);
            paint_float(bindings,PU_BOUNDARYSIZE,p->boundary?(float)CHUNK_SIZE:0);
            paint_float(bindings,PU_BOUNDARYGUARD,
                fmaxf(1.5f,s->scroll_line_width*qlcg_clampf(f->height/900.0f,.75f,2)*2.75f)*2.5f+2);
        }
        paint_int(bindings,PU_PAINTPALETTE,s->scroll_palette+1);
        paint_vec3(bindings,PU_PAINTPAPER,f->colors.paper);
        paint_vec3(bindings,PU_PAINTSKY,f->colors.sky);
        paint_vec3(bindings,PU_PAINTSNOW,f->colors.snow);
        paint_vec3(bindings,PU_PAINTSNOWFAR,f->colors.snow_far);
        paint_vec2(bindings,PU_PAINTFOG,f->colors.fog[0],f->colors.fog[1]);
        paint_matrix(bindings,PU_VP,f->vp);
        paint_vec3(bindings,PU_EYE,f->camera);
        paint_vec2(bindings,PU_VIEWPORT,(float)f->width,(float)f->height);
        paint_vec2(bindings,PU_FOGRANGE,f->fog[0],f->fog[1]);
        paint_int(bindings,PU_PALETTE,s->scroll_palette);
        paint_float(bindings,PU_PIGMENT,s->scroll_pigment);
        paint_float(bindings,PU_INK,s->scroll_ink);
        paint_float(bindings,PU_BASEY,f->camera[1]-1.7f);
        float snow=fmaxf(tr->plant_season==3?0.86f:0,f->weather.snow);
        paint_float(bindings,PU_SNOWAMT,snow);
        paint_float(bindings,PU_WET,fmaxf(f->weather.rain,tr->rain_override));
        paint_float(bindings,PU_HAZE,s->weather_atmosphere?qlcg_clampf((1-f->weather.fog)*1.15f,0,1):0);
        paint_float(bindings,PU_DUST,s->weather_atmosphere?f->weather.dust:0);
        paint_vec3(bindings,PU_SKYTINT,f->sky.tint);
        paint_float(bindings,PU_DAYLIGHT,f->sky.daylight);
        paint_float(bindings,PU_DETAIL,1);
        paint_float(bindings,PU_TIME,f->time);
        paint_int(bindings,PU_UNDERWATER,f->underwater);
        paint_vec3(bindings,PU_WATERFOG,f->sky.horizon);
        return;
    }
    glUniform1i(qlcg_gl_uniform(program,"uEnhanced"),f->enhanced);
    if(!f->enhanced) return;
    const SkyClimate *sky=&f->sky;
    float elevation=qlcg_clampf(sky->sun_dir[1],0,1),warm=powf(1-elevation,1.4f);
    float daylight=qlcg_clampf(sky->daylight,0,1);
    float intensity=(0.55f+0.20f*elevation)*
        qlcg_smoothf(0,0.06f,elevation);
    glUniform3f(qlcg_gl_uniform(program,"uSunColor"),(1.06f+0.42f*warm)*intensity,
        (0.97f-0.03f*warm)*intensity,(0.84f-0.38f*warm)*intensity);
    glUniform3f(qlcg_gl_uniform(program,"uSkyAmb"),0.24f+0.06f*(1-daylight),0.28f+0.05f*(1-daylight),0.36f+0.03f*(1-daylight));
    glUniform3f(qlcg_gl_uniform(program,"uGroundAmb"),0.14f,0.12f,0.09f);
    glUniform1f(qlcg_gl_uniform(program,"uWet"),qlcg_clampf(fmaxf(f->weather.rain,tr->rain_override),0,1));
    glUniform1f(qlcg_gl_uniform(program,"uSnowAmt"),fmaxf(tr->plant_season==3?0.85f:0,f->weather.snow));
    glUniform1f(qlcg_gl_uniform(program,"uHaze"),s->weather_atmosphere?qlcg_clampf(sky->cloud_cov*0.5f,0,1):0);
    LightVisual *l=&tr->visual->light;
    glUniform1i(qlcg_gl_uniform(program,"uShadowOn"),l->valid&&sky->sun_dir[1]>0.01f);
    glUniform1i(qlcg_gl_uniform(program,"uDebugShadow"),0);
    glUniformMatrix4fv(qlcg_gl_uniform(program,"uShadowVP"),1,GL_FALSE,l->vp);
    glUniform2f(qlcg_gl_uniform(program,"uShadowTexel"),1.0f/SHADOW_SIZE,1.0f/SHADOW_SIZE);
    glActiveTexture(GL_TEXTURE0+6);glBindTexture(GL_TEXTURE_2D,l->texture);
    glActiveTexture(GL_TEXTURE0);
}

void qlcg_visual_clouds_prepare(TerrainRenderer *tr,const RegionData *reg,const MacroWorld *world) {
    if(!tr->visual||tr->reflection_pass||!tr->visual->frame.settings->decorative_clouds)return;
    if(!tr->visual->clouds)tr->visual->clouds=qlcg_clouds_create();
    const TerrainVisualFrame *f=&tr->visual->frame;
    qlcg_clouds_update(tr->visual->clouds,world,&tr->weather_sys,tr->weather_unix_ms,
        f->time,(double)reg->center_x+tr->x,(double)reg->center_z+tr->z);
}

void qlcg_visual_clouds_bind(TerrainRenderer *tr,GLuint program) {
    const TerrainVisualFrame *f=tr->visual?&tr->visual->frame:NULL;
    bool enabled=f&&f->settings->decorative_clouds&&!f->underwater;
    glUniform1f(qlcg_gl_uniform(program,"uCloudAmount"),enabled?1:0);
    if(enabled)qlcg_clouds_bind(tr->visual->clouds,program,
        tr->anchor_x+f->camera[0],tr->anchor_z+f->camera[2],f->camera[1]);
}

bool qlcg_visual_sky(TerrainRenderer *tr,const float *forward,const float *right,const float *up,
    float aspect,float tan_half,const AstraFrame *astronomy) {
    if(!tr->visual||!tr->visual->frame.paint) return false;
    const TerrainVisualFrame *f=&tr->visual->frame;
    GLuint program=tr->visual->paint.sky;qlcg_visual_bind(tr,program);
    glDepthMask(GL_FALSE);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);
    glUniform3fv(qlcg_gl_uniform(program,"uCamFwd"),1,forward);
    glUniform3fv(qlcg_gl_uniform(program,"uCamRight"),1,right);
    glUniform3fv(qlcg_gl_uniform(program,"uCamUp"),1,up);
    glUniform1f(qlcg_gl_uniform(program,"uAspect"),aspect);
    glUniform1f(qlcg_gl_uniform(program,"uTanHalf"),tan_half);
    glUniform3fv(qlcg_gl_uniform(program,"uSunDir"),1,f->sky.sun_dir);
    glUniform3fv(qlcg_gl_uniform(program,"uMoonDir"),1,f->sky.moon_dir);
    glUniform1f(qlcg_gl_uniform(program,"uMoonAmt"),f->sky.moon);
    qlcg_visual_clouds_bind(tr,program);
    glUniform1f(qlcg_gl_uniform(program,"uFlash"),f->sky.flash);
    glUniform1f(qlcg_gl_uniform(program,"uSunMoonAmt"),f->weather.id==900?1:0);
    glUniform1i(qlcg_gl_uniform(program,"uLunarPhase"),astronomy&&f->weather.id<900);
    if(astronomy) qlcg_astra_uniforms(program,astronomy);
    glBindVertexArray(tr->visual->paint.vao);glDrawArrays(GL_TRIANGLES,0,3);
    glDepthMask(GL_TRUE);
    return true;
}

void qlcg_visual_strokes(TerrainRenderer *tr,const RegionData *reg) {
    (void)tr;(void)reg;
}

void qlcg_visual_end(TerrainRenderer *tr) {
    if(!tr->visual||!tr->visual->paint.drawing) return;
    qlcg_profile_begin(QL_GPU_PAINT);
    PaintVisual *p=&tr->visual->paint;
    const TerrainVisualFrame *f=&tr->visual->frame;
    if(p->ms_fbo) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,p->ms_fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,p->fbo);
        GLenum attachment=GL_COLOR_ATTACHMENT0;glReadBuffer(attachment);glDrawBuffers(1,&attachment);
        glBlitFramebuffer(0,0,p->width,p->height,0,0,p->width,p->height,GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT,GL_NEAREST);
        attachment=GL_COLOR_ATTACHMENT1;glReadBuffer(attachment);glDrawBuffers(1,&attachment);
        glBlitFramebuffer(0,0,p->width,p->height,0,0,p->width,p->height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        const GLenum attachments[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};glDrawBuffers(2,attachments);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,p->output);
    glViewport(p->viewport[0],p->viewport[1],p->viewport[2],p->viewport[3]);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    glUseProgram(p->edge);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,p->color);
    glUniform1i(p->edge_loc[PE_TEX],0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,p->depth);
    glUniform1i(p->edge_loc[PE_DEPTH],1);
    glActiveTexture(GL_TEXTURE0+2);glBindTexture(GL_TEXTURE_2D,p->terrain);
    glUniform1i(p->edge_loc[PE_TERRAIN],2);
    float tan_half=tanf(f->settings->fov*QLCG_PI/360.0f);
    glUniform2f(p->edge_loc[PE_PROJECTION],tan_half*p->width/p->height,tan_half);
    glUniform1i(p->edge_loc[PE_PALETTE],f->settings->scroll_palette);
    glUniform2f(p->edge_loc[PE_TEXEL],1.0f/p->width,1.0f/p->height);
    glUniform2f(p->edge_loc[PE_CLIPRANGE],0.15f,f->clip_far);
    glUniform2f(p->edge_loc[PE_EDGERANGE],6*CHUNK_SIZE,8*CHUNK_SIZE);
    glUniform1f(p->edge_loc[PE_STRENGTH],f->settings->scroll_ink);
    glUniform1f(p->edge_loc[PE_WASH],0.56f);
    glUniform1f(p->edge_loc[PE_LINEWIDTH],f->settings->scroll_line_width*qlcg_clampf(p->height/900.0f,0.75f,2));
    const float ink[3][3]={{0.14f,0.19f,0.17f},{0.21f,0.16f,0.13f},{31/255.0f,28/255.0f,26/255.0f}};
    const float deep[3][3]={{0.08f,0.11f,0.10f},{0.12f,0.09f,0.08f},{17/255.0f,16/255.0f,14/255.0f}};
    int palette=qlcg_clampi(f->settings->scroll_palette,0,2);
    glUniform3fv(p->edge_loc[PE_INK],1,ink[palette]);
    glUniform3fv(p->edge_loc[PE_INKDEEP],1,deep[palette]);
    glBindVertexArray(p->vao);glDrawArrays(GL_TRIANGLES,0,3);
    glBindVertexArray(0);glActiveTexture(GL_TEXTURE0);glDepthMask(GL_TRUE);
    glDepthFunc(p->depth_func);
    p->drawing=false;qlcg_profile_end(QL_GPU_PAINT);
}

void qlcg_visual_reset(TerrainRenderer *tr) {
    if(!tr->visual) return;
    qlcg_clouds_free(tr->visual->clouds);tr->visual->clouds=NULL;
    tr->visual->light.valid=false;
    tr->visual->paint.revision=UINT64_MAX;
    tr->visual->paint.drawing=false;
}

void qlcg_visual_free(TerrainRenderer *tr) {
    if(!tr->visual) return;
    PaintVisual *p=&tr->visual->paint;LightVisual *l=&tr->visual->light;
    qlcg_clouds_free(tr->visual->clouds);
    paint_targets_free(p);free(p->strokes);
    qlcg_gl_delete_textures(1,&p->boundary);
    for(int i=0;i<QL_VIS_COUNT;i++) qlcg_gl_delete_program(p->programs[i]);
    qlcg_gl_delete_program(p->sky);qlcg_gl_delete_program(p->edge);qlcg_gl_delete_program(p->stroke);
    GLuint vaos[]={p->vao,p->stroke_vao};qlcg_gl_delete_vertex_arrays(2,vaos);
    qlcg_gl_delete_buffers(1,&p->stroke_vbo);
    qlcg_gl_delete_framebuffers(1,&l->fbo);qlcg_gl_delete_textures(1,&l->texture);
    qlcg_gl_delete_program(l->terrain);qlcg_gl_delete_program(l->plant);
    free(tr->visual);tr->visual=NULL;
}
