#ifndef QLCG_WIN32_NATIVE_H
#define QLCG_WIN32_NATIVE_H
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <stdbool.h>
#include <windows.h>
#include <windowsx.h>
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 1
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int *);
typedef BOOL (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int);
extern PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;
extern PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;
bool init_win32_gl(HINSTANCE instance, int width, int height);
LRESULT CALLBACK app_wnd_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
#endif
