#ifdef _WIN32
#include "platform/win32/native.h"
#include "terrain3d/gl_loader.h"
#include <stdio.h>

PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = NULL;
PFNWGLSWAPINTERVALEXTPROC         wglSwapIntervalEXT         = NULL;
PFNGLGENBUFFERSPROC               glGenBuffers               = NULL;
PFNGLGETSTRINGIPROC              glGetStringi              = NULL;
PFNGLDELETEBUFFERSPROC            glDeleteBuffers            = NULL;
PFNGLBINDBUFFERPROC               glBindBuffer               = NULL;
PFNGLBUFFERDATAPROC               glBufferData               = NULL;
PFNGLBUFFERSUBDATAPROC            glBufferSubData            = NULL;
PFNGLCOPYBUFFERSUBDATAPROC        glCopyBufferSubData        = NULL;
PFNGLGETBUFFERPARAMETERIVPROC     glGetBufferParameteriv     = NULL;
PFNGLGENVERTEXARRAYSPROC          glGenVertexArrays          = NULL;
PFNGLDELETEVERTEXARRAYSPROC       glDeleteVertexArrays       = NULL;
PFNGLBINDVERTEXARRAYPROC          glBindVertexArray          = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC  glEnableVertexAttribArray  = NULL;
PFNGLVERTEXATTRIBPOINTERPROC      glVertexAttribPointer      = NULL;
PFNGLVERTEXATTRIBDIVISORPROC      glVertexAttribDivisor      = NULL;
PFNGLCREATESHADERPROC             glCreateShader             = NULL;
PFNGLSHADERSOURCEPROC             glShaderSource             = NULL;
PFNGLCOMPILESHADERPROC            glCompileShader            = NULL;
PFNGLGETSHADERIVPROC              glGetShaderiv              = NULL;
PFNGLGETSHADERINFOLOGPROC         glGetShaderInfoLog         = NULL;
PFNGLDELETESHADERPROC             glDeleteShader             = NULL;
PFNGLCREATEPROGRAMPROC            glCreateProgram            = NULL;
PFNGLATTACHSHADERPROC             glAttachShader             = NULL;
PFNGLLINKPROGRAMPROC              glLinkProgram              = NULL;
PFNGLGETPROGRAMIVPROC             glGetProgramiv             = NULL;
PFNGLGETPROGRAMINFOLOGPROC        glGetProgramInfoLog        = NULL;
PFNGLUSEPROGRAMPROC               glUseProgram               = NULL;
PFNGLDELETEPROGRAMPROC            glDeleteProgram            = NULL;
PFNGLGETUNIFORMLOCATIONPROC       glGetUniformLocation       = NULL;
PFNGLUNIFORM1IPROC                glUniform1i                = NULL;
PFNGLUNIFORM1FPROC                glUniform1f                = NULL;
PFNGLUNIFORM2FPROC                glUniform2f                = NULL;
PFNGLUNIFORM3FPROC                glUniform3f                = NULL;
PFNGLUNIFORM4FPROC                glUniform4f                = NULL;
PFNGLUNIFORMMATRIX4FVPROC         glUniformMatrix4fv         = NULL;
PFNGLACTIVETEXTUREPROC            glActiveTexture            = NULL;
PFNGLDRAWARRAYSINSTANCEDPROC      glDrawArraysInstanced      = NULL;
PFNGLDRAWELEMENTSINSTANCEDPROC    glDrawElementsInstanced    = NULL;
PFNGLMULTIDRAWELEMENTSBASEVERTEXPROC glMultiDrawElementsBaseVertex = NULL;
PFNGLGENERATEMIPMAPPROC           glGenerateMipmap           = NULL;
PFNGLGENFRAMEBUFFERSPROC          glGenFramebuffers          = NULL;
PFNGLDELETEFRAMEBUFFERSPROC       glDeleteFramebuffers       = NULL;
PFNGLBINDFRAMEBUFFERPROC          glBindFramebuffer          = NULL;
PFNGLFRAMEBUFFERTEXTURE2DPROC     glFramebufferTexture2D     = NULL;
PFNGLCHECKFRAMEBUFFERSTATUSPROC   glCheckFramebufferStatus   = NULL;
PFNGLBLITFRAMEBUFFERPROC          glBlitFramebuffer          = NULL;
PFNGLGENRENDERBUFFERSPROC          glGenRenderbuffers          = NULL;
PFNGLBINDRENDERBUFFERPROC          glBindRenderbuffer          = NULL;
PFNGLRENDERBUFFERSTORAGEPROC       glRenderbufferStorage       = NULL;
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC glRenderbufferStorageMultisample = NULL;
PFNGLFRAMEBUFFERRENDERBUFFERPROC   glFramebufferRenderbuffer   = NULL;
PFNGLDELETERENDERBUFFERSPROC       glDeleteRenderbuffers       = NULL;

/* 获取单个 OpenGL 扩展函数指针 */
static void *get_gl_proc(HMODULE gl_dll, const char *name) {
    void *p = (void *)wglGetProcAddress(name);
    if (!p || p == (void *)0x1 || p == (void *)0x2 || p == (void *)0x3 || p == (void *)-1) {
        if (gl_dll) p = (void *)GetProcAddress(gl_dll, name);
    }
    return p;
}

/* 加载 OpenGL 3.3 核心函数 */
bool qlcg_gl_load_functions(void) {
    HMODULE gl_dll = GetModuleHandleA("opengl32.dll");
#define LOAD_GL(type, var) var = (type)get_gl_proc(gl_dll, #var)
    LOAD_GL(PFNWGLCREATECONTEXTATTRIBSARBPROC, wglCreateContextAttribsARB);
    LOAD_GL(PFNWGLSWAPINTERVALEXTPROC,         wglSwapIntervalEXT);
    LOAD_GL(PFNGLGENBUFFERSPROC,               glGenBuffers);
    LOAD_GL(PFNGLGETSTRINGIPROC,              glGetStringi);
    LOAD_GL(PFNGLDELETEBUFFERSPROC,            glDeleteBuffers);
    LOAD_GL(PFNGLBINDBUFFERPROC,               glBindBuffer);
    LOAD_GL(PFNGLBUFFERDATAPROC,               glBufferData);
    LOAD_GL(PFNGLBUFFERSUBDATAPROC,            glBufferSubData);
    LOAD_GL(PFNGLCOPYBUFFERSUBDATAPROC,        glCopyBufferSubData);
    LOAD_GL(PFNGLGETBUFFERPARAMETERIVPROC,     glGetBufferParameteriv);
    LOAD_GL(PFNGLGENVERTEXARRAYSPROC,          glGenVertexArrays);
    LOAD_GL(PFNGLDELETEVERTEXARRAYSPROC,       glDeleteVertexArrays);
    LOAD_GL(PFNGLBINDVERTEXARRAYPROC,          glBindVertexArray);
    LOAD_GL(PFNGLENABLEVERTEXATTRIBARRAYPROC,  glEnableVertexAttribArray);
    LOAD_GL(PFNGLVERTEXATTRIBPOINTERPROC,      glVertexAttribPointer);
    LOAD_GL(PFNGLVERTEXATTRIBDIVISORPROC,      glVertexAttribDivisor);
    LOAD_GL(PFNGLCREATESHADERPROC,             glCreateShader);
    LOAD_GL(PFNGLSHADERSOURCEPROC,             glShaderSource);
    LOAD_GL(PFNGLCOMPILESHADERPROC,            glCompileShader);
    LOAD_GL(PFNGLGETSHADERIVPROC,              glGetShaderiv);
    LOAD_GL(PFNGLGETSHADERINFOLOGPROC,         glGetShaderInfoLog);
    LOAD_GL(PFNGLDELETESHADERPROC,             glDeleteShader);
    LOAD_GL(PFNGLCREATEPROGRAMPROC,            glCreateProgram);
    LOAD_GL(PFNGLATTACHSHADERPROC,             glAttachShader);
    LOAD_GL(PFNGLLINKPROGRAMPROC,              glLinkProgram);
    LOAD_GL(PFNGLGETPROGRAMIVPROC,             glGetProgramiv);
    LOAD_GL(PFNGLGETPROGRAMINFOLOGPROC,        glGetProgramInfoLog);
    LOAD_GL(PFNGLUSEPROGRAMPROC,               glUseProgram);
    LOAD_GL(PFNGLDELETEPROGRAMPROC,            glDeleteProgram);
    LOAD_GL(PFNGLGETUNIFORMLOCATIONPROC,       glGetUniformLocation);
    LOAD_GL(PFNGLUNIFORM1IPROC,                glUniform1i);
    LOAD_GL(PFNGLUNIFORM1FPROC,                glUniform1f);
    LOAD_GL(PFNGLUNIFORM2FPROC,                glUniform2f);
    LOAD_GL(PFNGLUNIFORM3FPROC,                glUniform3f);
    LOAD_GL(PFNGLUNIFORM4FPROC,                glUniform4f);
    LOAD_GL(PFNGLUNIFORMMATRIX4FVPROC,         glUniformMatrix4fv);
    LOAD_GL(PFNGLACTIVETEXTUREPROC,            glActiveTexture);
    LOAD_GL(PFNGLDRAWARRAYSINSTANCEDPROC,      glDrawArraysInstanced);
    LOAD_GL(PFNGLDRAWELEMENTSINSTANCEDPROC,    glDrawElementsInstanced);
    LOAD_GL(PFNGLMULTIDRAWELEMENTSBASEVERTEXPROC, glMultiDrawElementsBaseVertex);
    LOAD_GL(PFNGLGENERATEMIPMAPPROC,           glGenerateMipmap);
    LOAD_GL(PFNGLGENFRAMEBUFFERSPROC,          glGenFramebuffers);
    LOAD_GL(PFNGLDELETEFRAMEBUFFERSPROC,       glDeleteFramebuffers);
    LOAD_GL(PFNGLBINDFRAMEBUFFERPROC,          glBindFramebuffer);
    LOAD_GL(PFNGLFRAMEBUFFERTEXTURE2DPROC,     glFramebufferTexture2D);
    LOAD_GL(PFNGLCHECKFRAMEBUFFERSTATUSPROC,   glCheckFramebufferStatus);
    LOAD_GL(PFNGLBLITFRAMEBUFFERPROC,          glBlitFramebuffer);
    LOAD_GL(PFNGLGENRENDERBUFFERSPROC,         glGenRenderbuffers);
    LOAD_GL(PFNGLBINDRENDERBUFFERPROC,         glBindRenderbuffer);
    LOAD_GL(PFNGLRENDERBUFFERSTORAGEPROC,      glRenderbufferStorage);
    LOAD_GL(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample);
    LOAD_GL(PFNGLFRAMEBUFFERRENDERBUFFERPROC,  glFramebufferRenderbuffer);
    LOAD_GL(PFNGLDELETERENDERBUFFERSPROC,      glDeleteRenderbuffers);
#undef LOAD_GL
    return glGenBuffers && glCreateProgram && glBindVertexArray;
}
#endif
