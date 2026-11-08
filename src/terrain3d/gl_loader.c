#include "gl_loader.h"
#include <stdio.h>
#include <string.h>

void qlcg_gl_program_point_size(bool enabled) {
#ifdef _WIN32
    if (enabled) glEnable(GL_PROGRAM_POINT_SIZE);
    else glDisable(GL_PROGRAM_POINT_SIZE);
#else
    (void)enabled;
#endif
}

void qlcg_gl_multisample(bool enabled) {
#ifdef _WIN32
    if (enabled) glEnable(GL_MULTISAMPLE);
    else glDisable(GL_MULTISAMPLE);
#else
    (void)enabled;
#endif
}


static void forget_uniforms(GLuint program);
static bool context_ready;

static GLuint compile_shader(GLenum type,const char *source) {
    const char *body=source;
    if(!strncmp(body,"#version",8)) { body=strchr(body,'\n'); if(!body) return 0; body++; }
    char header[192];
    snprintf(header,sizeof(header),"%s\n%s#define QLCG_FLOAT_LINEAR %d\n",
        qlcg_gl_caps.es?"#version 300 es":"#version 330 core",
        qlcg_gl_caps.es?"precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n":"",
        qlcg_gl_caps.float_linear?1:0);
    const char *parts[]={header,body};
    GLuint shader=glCreateShader(type); if(!shader) return 0;
    glShaderSource(shader,2,parts,NULL); glCompileShader(shader);
    GLint ok=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok) {
        char log[4096]; glGetShaderInfoLog(shader,sizeof(log),NULL,log);
        fprintf(stderr,"GL shader %x: %s\n",type,log); glDeleteShader(shader); return 0;
    }
    return shader;
}
GLuint qlcg_gl_compile_program(const char *vs_src,const char *fs_src) {
    GLuint vs=compile_shader(GL_VERTEX_SHADER,vs_src),fs=compile_shader(GL_FRAGMENT_SHADER,fs_src);
    if(!vs || !fs) { if(vs) glDeleteShader(vs); if(fs) glDeleteShader(fs); return 0; }
    GLuint program=glCreateProgram();
    glAttachShader(program,vs); glAttachShader(program,fs); glLinkProgram(program);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok=0; glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok) {
        char log[4096]; glGetProgramInfoLog(program,sizeof(log),NULL,log);
        fprintf(stderr,"GL program: %s\n",log); glDeleteProgram(program); return 0;
    }
    forget_uniforms(program);
    return program;
}

/* —— uniform 位置缓存 —— */
typedef struct { GLuint prog; const void *name; GLint loc; } UniEntry;
static UniEntry s_uni[512];
static int s_uni_n;
static void forget_uniforms(GLuint program) {
    for(int i=0;i<s_uni_n;) {
        if(s_uni[i].prog==program) s_uni[i]=s_uni[--s_uni_n]; else i++;
    }
}


GLint qlcg_gl_uniform(GLuint prog, const char *name) {
    for (int i = 0; i < s_uni_n; i++)
        if (s_uni[i].prog == prog && s_uni[i].name == (const void *)name) return s_uni[i].loc;
    GLint loc = glGetUniformLocation(prog, name);
    if (s_uni_n < (int)(sizeof(s_uni) / sizeof(s_uni[0]))) {
        s_uni[s_uni_n].prog = prog;
        s_uni[s_uni_n].name = (const void *)name;
        s_uni[s_uni_n].loc = loc;
        s_uni_n++;
    }
    return loc;
}

QlGlCaps qlcg_gl_caps;
void qlcg_gl_context_reset(void) { context_ready=false; s_uni_n=0; memset(&qlcg_gl_caps,0,sizeof(qlcg_gl_caps)); }
static bool extension(const char *name) {
    GLint count=0; glGetIntegerv(GL_NUM_EXTENSIONS,&count);
    for(GLint i=0;i<count;i++) {
        const char *value=(const char *)glGetStringi(GL_EXTENSIONS,(GLuint)i);
        if(value && !strcmp(value,name)) return true;
    }
    return false;
}
bool qlcg_gl_context_init(void) {
    qlcg_gl_context_reset();
    const char *version=(const char *)glGetString(GL_VERSION);
    if(!version) return false;
    qlcg_gl_caps.es=strstr(version,"OpenGL ES")!=NULL;
    GLint major=0; glGetIntegerv(GL_MAJOR_VERSION,&major); if(major<3) return false;
    snprintf(qlcg_gl_caps.version,sizeof(qlcg_gl_caps.version),"%s",version);
    snprintf(qlcg_gl_caps.vendor,sizeof(qlcg_gl_caps.vendor),"%s",glGetString(GL_VENDOR));
    snprintf(qlcg_gl_caps.renderer,sizeof(qlcg_gl_caps.renderer),"%s",glGetString(GL_RENDERER));
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&qlcg_gl_caps.max_texture);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&qlcg_gl_caps.max_renderbuffer);
    glGetIntegerv(GL_MAX_SAMPLES,&qlcg_gl_caps.max_samples);
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS,&qlcg_gl_caps.max_attributes);
    qlcg_gl_caps.float_linear=!qlcg_gl_caps.es || extension("GL_OES_texture_float_linear");
    qlcg_gl_caps.float_color=!qlcg_gl_caps.es || extension("GL_EXT_color_buffer_float");
    qlcg_gl_caps.half_float_color=qlcg_gl_caps.float_color || extension("GL_EXT_color_buffer_half_float");
    qlcg_gl_caps.base_vertex=glMultiDrawElementsBaseVertex!=NULL;
    fprintf(stdout,"GL %s / %s; texture=%d samples=%d floatLinear=%d baseVertex=%d\n",
        qlcg_gl_caps.renderer,qlcg_gl_caps.version,qlcg_gl_caps.max_texture,qlcg_gl_caps.max_samples,
        qlcg_gl_caps.float_linear,qlcg_gl_caps.base_vertex);
    qlcg_gl_caps.max_anisotropy=1;
    if(extension("GL_EXT_texture_filter_anisotropic"))
        glGetFloatv(0x84FF,&qlcg_gl_caps.max_anisotropy);
    context_ready=qlcg_gl_caps.max_attributes>=16;
    return context_ready;
}
void qlcg_gl_bgra_to_rgba(uint8_t *pixels,size_t count) {
    for(size_t i=0;i<count;i++) { uint8_t red=pixels[i*4+2]; pixels[i*4+2]=pixels[i*4]; pixels[i*4]=red; }
}

void qlcg_gl_delete_textures(GLsizei count,const GLuint *names) { if(context_ready) glDeleteTextures(count,names); }

void qlcg_gl_delete_buffers(GLsizei count,const GLuint *names) { if(context_ready) glDeleteBuffers(count,names); }

void qlcg_gl_delete_vertex_arrays(GLsizei count,const GLuint *names) { if(context_ready) glDeleteVertexArrays(count,names); }

void qlcg_gl_delete_framebuffers(GLsizei count,const GLuint *names) { if(context_ready) glDeleteFramebuffers(count,names); }

void qlcg_gl_delete_renderbuffers(GLsizei count,const GLuint *names) { if(context_ready) glDeleteRenderbuffers(count,names); }

void qlcg_gl_delete_program(GLuint program) { forget_uniforms(program); if(context_ready && program) glDeleteProgram(program); }
