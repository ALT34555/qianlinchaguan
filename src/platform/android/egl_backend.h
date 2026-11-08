#ifndef QLCG_EGL_BACKEND_H
#define QLCG_EGL_BACKEND_H
#ifdef __ANDROID__
#include <EGL/egl.h>
#include <android/native_window.h>
#include <stdbool.h>
typedef struct {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    EGLConfig config;
    bool lost;
} QlEgl;
bool qlcg_egl_open(QlEgl *egl,ANativeWindow *window,int width,int height);
bool qlcg_egl_present(QlEgl *egl);
void qlcg_egl_close(QlEgl *egl);
#endif
#endif
