#include "astronomy_render.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
static const char *vs=
"#version 330 core\n"
"layout(location=0) in vec3 pos; layout(location=1) in vec3 color; layout(location=2) in float size;\n"
"uniform vec3 aEast,aUp,aSouth,uCamFwd,uCamRight,uCamUp; uniform float uAspect,uTanHalfFov;\n"
"out vec3 tint; out vec3 dir; out float edge;\n"
"void main(){ dir=vec3(dot(pos,aEast),dot(pos,aUp),dot(pos,aSouth)); tint=color;edge=size;\n"
"float z=dot(dir,uCamFwd); gl_Position=vec4(dot(dir,uCamRight)/(uAspect*uTanHalfFov),dot(dir,uCamUp)/uTanHalfFov,0.0,z); gl_PointSize=size; }\n";
static const char *fs=
"#version 330 core\n"
"in vec3 tint; in vec3 dir; in float edge; out vec4 frag; uniform int points,foreground,ribbonMode;\n"
"uniform vec3 uSunDir,uMoonDir; uniform float uStar,uCloudCov,uRainAlpha,uSimTime,uSunRadius,uMoonRadius;\n"
"float h(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}\n"
"float n(vec2 p){vec2 i=floor(p),f=fract(p),u=f*f*(3.0-2.0*f);return mix(mix(h(vec3(i,0)),h(vec3(i+vec2(1,0),0)),u.x),mix(h(vec3(i+vec2(0,1),0)),h(vec3(i+vec2(1,1),0)),u.x),u.y);}\n"
"void main(){vec3 d=normalize(dir);if(d.y<=0.0||(foreground==0&&((dot(d,uSunDir)>0.0&&length(cross(d,uSunDir))<sin(uSunRadius))||(dot(d,uMoonDir)>0.0&&length(cross(d,uMoonDir))<sin(uMoonRadius)))))discard;\n"
"float flash=foreground!=0?clamp(max(tint.r,max(tint.g,tint.b))-1.0,0.0,1.0)*.25:0.0;\n"
"float a=max(uStar,flash)*smoothstep(0.0,.18,d.y);vec2 uv=d.xz/(d.y+.18)*1.8+vec2(uSimTime*.012,uSimTime*.007);\n"
"float cl=smoothstep(.62-uCloudCov*.22,.95,n(uv*1.6)*.6+n(uv*3.7+11.0)*.4);\n"
"a*=1.0-cl*clamp(uCloudCov*.55,0.0,.92)*smoothstep(.02,.25,d.y);\n"
"float rc=.5+.25*sin(d.x*12.0+d.z*7.0)+.25*cos(d.z*15.0-d.y*8.0);\n"
"a*=1.0-uRainAlpha*smoothstep(-.12,.06,d.y)*(.88+.12*rc);\n"
"if(points!=0){float r=length(gl_PointCoord-.5)*2.0;a*=1.0-smoothstep(.15,1.0,r);}\n"
"if(ribbonMode!=0)a*=exp(-edge*edge*4.0)*(1.0-smoothstep(.65,1.0,abs(edge)));\n"
"frag=vec4(tint*a,1.0);}\n";
static void v3(GLuint p,const char *name,const float *v) { glUniform3f(qlcg_gl_uniform(p,name),v[0],v[1],v[2]); }
void qlcg_astra_uniforms(GLuint p,const AstraFrame *f) {
    v3(p,"aEast",f->east); v3(p,"aUp",f->up); v3(p,"aSouth",f->south);
    v3(p,"uShadowDir",f->shadow_dir);
    v3(p,"uOcculter",f->occluder);
    glUniform1f(qlcg_gl_uniform(p,"uOcculterRadius"),f->occluder_radius);
    glUniform1f(qlcg_gl_uniform(p,"uSunRadius"),f->sun_radius);
    glUniform1f(qlcg_gl_uniform(p,"uMoonRadius"),f->moon_radius);
    glUniform1f(qlcg_gl_uniform(p,"uShadowRadius"),f->shadow_radius);
    glUniform1f(qlcg_gl_uniform(p,"uPenumbraRadius"),f->penumbra_radius);
    glUniform1f(qlcg_gl_uniform(p,"uSolarCover"),f->solar_cover);
}
static void buffer(GLuint *vao,GLuint *vbo,size_t bytes,const void *data,GLenum usage) {
    glGenVertexArrays(1,vao); glGenBuffers(1,vbo); glBindVertexArray(*vao); glBindBuffer(GL_ARRAY_BUFFER,*vbo);
    glBufferData(GL_ARRAY_BUFFER,bytes,data,usage);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(AstraVertex),(void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(AstraVertex),(void *)offsetof(AstraVertex,color));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,sizeof(AstraVertex),(void *)offsetof(AstraVertex,size));
}
void qlcg_astra_release(AstraRenderer *r) {
    free(r->event_vertices);glDeleteVertexArrays(1,&r->event_vao);glDeleteBuffers(1,&r->event_vbo);
    free(r->catalog); glDeleteTextures(1,&r->texture); glDeleteVertexArrays(1,&r->vao);
    glDeleteVertexArrays(1,&r->moving_vao); glDeleteBuffers(1,&r->vbo); glDeleteBuffers(1,&r->moving_vbo);
    if(r->program) glDeleteProgram(r->program); memset(r,0,sizeof(*r));
}
bool qlcg_astra_upload(AstraRenderer *r,const WorldCreatorOptions *opt) {
    if(!opt->astronomy_enabled) return false;
    if(r->catalog && r->catalog->seed==opt->seed && r->catalog->version==opt->astronomy_version) return true;
    bool observing=r->observing; qlcg_astra_release(r); r->observing=observing;
    r->catalog=malloc(sizeof(AstraCatalog)); unsigned char *pixels=malloc(1024*512*3);
    if(!r->catalog||!pixels) {free(pixels); qlcg_astra_release(r); return false;}
    qlcg_astra_generate(r->catalog,opt->seed,opt->astronomy_version);
    qlcg_astra_background(r->catalog,pixels,1024,512);
    glActiveTexture(GL_TEXTURE0); glGenTextures(1,&r->texture); glBindTexture(GL_TEXTURE_2D,r->texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,1024,512,0,GL_RGB,GL_UNSIGNED_BYTE,pixels); free(pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    buffer(&r->vao,&r->vbo,sizeof(r->catalog->stars)+sizeof(r->catalog->lines),NULL,GL_STATIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(r->catalog->stars),r->catalog->stars);
    glBufferSubData(GL_ARRAY_BUFFER,sizeof(r->catalog->stars),sizeof(r->catalog->lines),r->catalog->lines);
    buffer(&r->moving_vao,&r->moving_vbo,ASTRA_BODIES*sizeof(AstraVertex),NULL,GL_DYNAMIC_DRAW);
    r->event_vertices=malloc(ASTRA_EVENT_VERTICES*sizeof(AstraVertex));
    if(!r->event_vertices){qlcg_astra_release(r);return false;}
    buffer(&r->event_vao,&r->event_vbo,ASTRA_EVENT_VERTICES*sizeof(AstraVertex),NULL,GL_DYNAMIC_DRAW);
    r->program=qlcg_gl_compile_program(vs,fs);
    if(!r->program) { qlcg_astra_release(r); return false; } return true;
}
void qlcg_astra_draw(AstraRenderer *r,const AstraFrame *f,const SkyClimate *sky,
    const float *forward,const float *right,const float *up,float aspect,float tan_fov,float time,
    float cloud_cov,float rain_alpha) {
    if(!r->catalog) return;
    GLuint p=r->program; glUseProgram(p); qlcg_astra_uniforms(p,f);
    glUniform1i(qlcg_gl_uniform(p,"foreground"),0);
    glUniform1i(qlcg_gl_uniform(p,"ribbonMode"),0);
    v3(p,"uCamFwd",forward); v3(p,"uCamRight",right); v3(p,"uCamUp",up);
    v3(p,"uSunDir",sky->sun_dir); v3(p,"uMoonDir",sky->moon_dir);
    if(fabsf(sky->sun_dir[1]-f->sun[1])>.00001f||fabsf(sky->moon_dir[1]-f->moon[1])>.00001f) {
        glUniform1f(qlcg_gl_uniform(p,"uSunRadius"),acosf(.9975f));
        glUniform1f(qlcg_gl_uniform(p,"uMoonRadius"),acosf(.9985f));
    }
    glUniform1f(qlcg_gl_uniform(p,"uAspect"),aspect); glUniform1f(qlcg_gl_uniform(p,"uTanHalfFov"),tan_fov);
    glUniform1f(qlcg_gl_uniform(p,"uStar"),sky->star); glUniform1f(qlcg_gl_uniform(p,"uCloudCov"),cloud_cov);
    glUniform1f(qlcg_gl_uniform(p,"uRainAlpha"),rain_alpha); glUniform1f(qlcg_gl_uniform(p,"uSimTime"),time);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE); glEnable(GL_PROGRAM_POINT_SIZE);
    glBindVertexArray(r->vao); glUniform1i(qlcg_gl_uniform(p,"points"),1);
    glDrawArrays(GL_POINTS,0,r->catalog->star_count);
    if(r->observing) { glUniform1i(qlcg_gl_uniform(p,"points"),0); glDrawArrays(GL_LINES,ASTRA_STARS,r->catalog->line_count); }
    glBindVertexArray(r->moving_vao); glBindBuffer(GL_ARRAY_BUFFER,r->moving_vbo);
    glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(f->bodies),f->bodies); glUniform1i(qlcg_gl_uniform(p,"points"),1);
    glDrawArrays(GL_POINTS,0,ASTRA_BODIES); glDisable(GL_PROGRAM_POINT_SIZE); glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}

void qlcg_astra_draw_events(AstraRenderer *r,AstraEventState *state,const CalendarClock *clock,const AstraFrame *f) {
    if(!r->catalog||!r->event_vertices)return;
    int comet_end=0,n=qlcg_astra_event_vertices(state,clock,f,r->event_vertices,ASTRA_EVENT_VERTICES,&comet_end);
    if(!n)return;
    glUseProgram(r->program);glBindVertexArray(r->event_vao);glBindBuffer(GL_ARRAY_BUFFER,r->event_vbo);
    glBufferSubData(GL_ARRAY_BUFFER,0,n*sizeof(AstraVertex),r->event_vertices);
    glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);glUniform1i(qlcg_gl_uniform(r->program,"points"),0);
    glUniform1i(qlcg_gl_uniform(r->program,"ribbonMode"),1);
    glUniform1i(qlcg_gl_uniform(r->program,"foreground"),0);glDrawArrays(GL_TRIANGLES,0,comet_end);
    glUniform1i(qlcg_gl_uniform(r->program,"foreground"),1);glDrawArrays(GL_TRIANGLES,comet_end,n-comet_end);
    glDisable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
