#ifdef _WIN32
#define COBJMACROS
#include "platform/win32/native.h"
#include "platform/paths.h"
#include "ui/ui_core.h"
#include <wincodec.h>
#include <stdlib.h>
void qlcg_platform_teacup(UiContext *ui, const wchar_t *path) {
    wchar_t resolved[4096];
    if(!qlcg_path_wide(path,false,resolved,4096)) return ;
    path=resolved;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *factory = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (LPVOID *)&factory);
    if (FAILED(hr) || !factory) return;

    IWICBitmapDecoder *decoder = NULL;
    hr = IWICImagingFactory_CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr) && decoder) {
        IWICBitmapFrameDecode *frame = NULL;
        if (SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) && frame) {
            UINT w = 0, h = 0;
            IWICBitmapFrameDecode_GetSize(frame, &w, &h);
            IWICBitmapScaler *scaler = NULL;
            IWICBitmapSource *source = (IWICBitmapSource *)frame;
            if (w > 0 && h > 0 &&
                SUCCEEDED(IWICImagingFactory_CreateBitmapScaler(factory, &scaler)) && scaler) {
                if (SUCCEEDED(IWICBitmapScaler_Initialize(scaler, source,
                        512, 512, WICBitmapInterpolationModeFant))) {
                    source = (IWICBitmapSource *)scaler;
                }
            }
            IWICFormatConverter *conv = NULL;
            if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &conv)) && conv) {
                /* 缩放后再固定上传格式 */
                if (SUCCEEDED(IWICFormatConverter_Initialize(conv, source,
                        &GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, NULL, 0.0,
                        WICBitmapPaletteTypeCustom))) {
                    IWICFormatConverter_GetSize(conv, &w, &h);
                    if (w > 0 && h > 0) {
                        uint8_t *buf = (uint8_t *)malloc((size_t)w * (size_t)h * 4);
                        if (buf && SUCCEEDED(IWICFormatConverter_CopyPixels(conv, NULL, w * 4, w * h * 4, buf))) {
                            glBindTexture(GL_TEXTURE_2D, ui->atlas_tex);
                            glTexSubImage2D(GL_TEXTURE_2D, 0, 4, 4, (GLsizei)w, (GLsizei)h,
                                            GL_RGBA, GL_UNSIGNED_BYTE, buf);
                            ui->has_teacup = true;
                            ui->cup_w  = (int)w;
                            ui->cup_h  = (int)h;
                            ui->cup_u0 = 4.0f / (float)UI_ATLAS_W;
                            ui->cup_v0 = 4.0f / (float)UI_ATLAS_H;
                            ui->cup_u1 = (float)(4 + w) / (float)UI_ATLAS_W;
                            ui->cup_v1 = (float)(4 + h) / (float)UI_ATLAS_H;
                            ui->shelf_y = (int)h + 12;
                        }
                        free(buf);
                    }
                }
                IWICFormatConverter_Release(conv);
            }
            if (scaler) IWICBitmapScaler_Release(scaler);
            IWICBitmapFrameDecode_Release(frame);
        }
        IWICBitmapDecoder_Release(decoder);
    }
    IWICImagingFactory_Release(factory);
}
bool ui_load_png_texture(const wchar_t *path, GLuint *tex, int *out_w, int *out_h) {
    wchar_t resolved[4096];
    if(!qlcg_path_wide(path,false,resolved,4096)) return false;
    path=resolved;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *factory = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void **)&factory);
    if (FAILED(hr) || !factory) return false;

    bool ok = false;
    IWICBitmapDecoder *decoder = NULL;
    hr = IWICImagingFactory_CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr) && decoder) {
        IWICBitmapFrameDecode *frame = NULL;
        if (SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) && frame) {
            IWICFormatConverter *conv = NULL;
            if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &conv)) && conv) {
                if (SUCCEEDED(IWICFormatConverter_Initialize(conv, (IWICBitmapSource *)frame,
                        &GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, NULL, 0.0,
                        WICBitmapPaletteTypeCustom))) {
                    UINT w = 0, h = 0;
                    IWICFormatConverter_GetSize(conv, &w, &h);
                    if (w > 0 && h > 0) {
                        uint8_t *buf = (uint8_t *)malloc((size_t)w * (size_t)h * 4);
                        if (buf && SUCCEEDED(IWICBitmapSource_CopyPixels(conv, NULL, w * 4,
                                                                        w * h * 4, buf))) {
                            glGenTextures(1, tex);
                            glBindTexture(GL_TEXTURE_2D, *tex);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                            /* 转换与上传格式一致 */
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0,
                                         GL_RGBA, GL_UNSIGNED_BYTE, buf);
                            if (out_w) *out_w = (int)w;
                            if (out_h) *out_h = (int)h;
                            ok = true;
                        }
                        free(buf);
                    }
                }
                IWICFormatConverter_Release(conv);
            }
            IWICBitmapFrameDecode_Release(frame);
        }
        IWICBitmapDecoder_Release(decoder);
    }
    IWICImagingFactory_Release(factory);
    return ok;
}
#endif
