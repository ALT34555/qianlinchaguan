#ifdef __ANDROID__
#include "platform/ui_backend.h"
#include "core/png_image.h"
#include "ui/ui_core.h"
#include <stdlib.h>

static uint8_t *rgba(const wchar_t *path,int *width,int *height) {
    uint8_t *pixels=NULL;
    if(!qlcg_png_load_bgra(path,&pixels,width,height)) return NULL;
    for(size_t i=0;i<(size_t)*width**height;i++) { uint8_t t=pixels[4*i]; pixels[4*i]=pixels[4*i+2]; pixels[4*i+2]=t; }
    return pixels;
}
bool ui_load_png_texture(const wchar_t *path,GLuint *texture,int *out_width,int *out_height) {
    int width,height; uint8_t *pixels=rgba(path,&width,&height); if(!pixels) return false;
    glGenTextures(1,texture); glBindTexture(GL_TEXTURE_2D,*texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    if(out_width) *out_width=width; if(out_height) *out_height=height; free(pixels); return true;
}
void qlcg_platform_teacup(UiContext *ui,const wchar_t *path) {
    int width,height; uint8_t *pixels=rgba(path,&width,&height); if(!pixels) return;
    uint8_t *scaled=malloc(512*512*4); if(!scaled) { free(pixels); return; }
    for(int y=0;y<512;y++) for(int x=0;x<512;x++) {
        size_t source=((size_t)(y*height/512)*width+x*width/512)*4;
        for(int c=0;c<4;c++) scaled[((size_t)y*512+x)*4+c]=pixels[source+c];
    }
    glBindTexture(GL_TEXTURE_2D,ui->atlas_tex); glTexSubImage2D(GL_TEXTURE_2D,0,4,4,512,512,GL_RGBA,GL_UNSIGNED_BYTE,scaled);
    ui->has_teacup=true; ui->cup_w=ui->cup_h=512; ui->cup_u0=4.f/UI_ATLAS_W; ui->cup_v0=4.f/UI_ATLAS_H;
    ui->cup_u1=516.f/UI_ATLAS_W; ui->cup_v1=516.f/UI_ATLAS_H; ui->shelf_y=524;
    free(scaled); free(pixels);
}
#endif
