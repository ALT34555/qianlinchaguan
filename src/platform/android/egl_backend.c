#ifdef __ANDROID__
#include "egl_backend.h"
#include "terrain3d/gl_loader.h"
#include <string.h>
#include <stdio.h>

QlMultiDrawBaseVertex glMultiDrawElementsBaseVertex;
bool qlcg_gl_load_functions(void) { return qlcg_gl_context_init(); }
void qlcg_egl_close(QlEgl *e) {
    if(e->display!=EGL_NO_DISPLAY) {
        eglMakeCurrent(e->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(e->surface!=EGL_NO_SURFACE) eglDestroySurface(e->display,e->surface);
        if(e->context!=EGL_NO_CONTEXT) eglDestroyContext(e->display,e->context);
        eglTerminate(e->display);
    }
    memset(e,0,sizeof(*e)); qlcg_gl_context_reset();
}
bool qlcg_egl_open(QlEgl *e,ANativeWindow *window,int width,int height) {
    memset(e,0,sizeof(*e));
    e->display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(e->display==EGL_NO_DISPLAY || !eglInitialize(e->display,NULL,NULL) || !eglBindAPI(EGL_OPENGL_ES_API)) goto failed;
    const EGLint attributes[]={EGL_SURFACE_TYPE,window?EGL_WINDOW_BIT:EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,
        EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_STENCIL_SIZE,8,EGL_NONE};
    EGLint count=0;
    if(!eglChooseConfig(e->display,attributes,&e->config,1,&count) || !count) goto failed;
    const EGLint context[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    e->context=eglCreateContext(e->display,e->config,EGL_NO_CONTEXT,context);
    if(e->context==EGL_NO_CONTEXT) goto failed;
    if(window) {
        EGLint format=0; eglGetConfigAttrib(e->display,e->config,EGL_NATIVE_VISUAL_ID,&format);
        if(ANativeWindow_setBuffersGeometry(window,0,0,format)) goto failed;
        e->surface=eglCreateWindowSurface(e->display,e->config,window,NULL);
    } else {
        const EGLint size[]={EGL_WIDTH,width,EGL_HEIGHT,height,EGL_NONE};
        e->surface=eglCreatePbufferSurface(e->display,e->config,size);
    }
    if(e->surface==EGL_NO_SURFACE || !eglMakeCurrent(e->display,e->surface,e->surface,e->context)) goto failed;
    if(!qlcg_gl_load_functions()) goto failed;
    eglSwapInterval(e->display,1);
    return true;
failed:
    fprintf(stderr,"EGL initialization failed: %x\n",eglGetError()); qlcg_egl_close(e); return false;
}
bool qlcg_egl_present(QlEgl *e) {
    if(eglSwapBuffers(e->display,e->surface)) return true;
    e->lost=eglGetError()==EGL_CONTEXT_LOST; return false;
}
#endif
