#ifdef __ANDROID__
#include "core/screenshot.h"
#include "core/png_image.h"
#include <stdlib.h>

bool qlcg_screenshot_png(const wchar_t *path,int width,int height) {
    if(width<1 || height<1 || (uint64_t)width*height>16777216)return false;
    size_t bytes=(size_t)width*height*4;
    uint8_t *raw=malloc(bytes),*pixels=malloc(bytes);
    if(!raw || !pixels) { free(raw);free(pixels);return false; }
    glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,raw);
    for(int y=0;y<height;y++)for(int x=0;x<width;x++) {
        size_t a=((size_t)y*width+x)*4,b=((size_t)(height-1-y)*width+x)*4;
        pixels[b]=raw[a+2];pixels[b+1]=raw[a+1];pixels[b+2]=raw[a];pixels[b+3]=255;
    }
    bool ok=qlcg_png_save_bgra(path,pixels,width,height);
    free(raw);free(pixels);return ok;
}
bool qlcg_screenshot_save(const wchar_t *path,int width,int height) {
    return qlcg_screenshot_png(path,width,height);
}
bool qlcg_screenshot_bmp(const wchar_t *path,int width,int height) {
    (void)path;(void)width;(void)height;return false;
}
#endif
