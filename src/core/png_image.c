/* PNG 解码与保存：WIC 直通 BGRA */
#define COBJMACROS
#include "png_image.h"
#include <wincodec.h>
#include <stdlib.h>
#include <string.h>

bool qlcg_png_load_bgra(const wchar_t *path, uint8_t **pixels, int *w, int *h) {
    if (!path || !pixels || !w || !h) return false;
    *pixels = NULL;
    *w = *h = 0;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *factory = NULL;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IWICImagingFactory, (void **)&factory)) && factory) {
        IWICBitmapDecoder *decoder = NULL;
        if (SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(
                factory, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) && decoder) {
            IWICBitmapFrameDecode *frame = NULL;
            if (SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) && frame) {
                IWICFormatConverter *conv = NULL;
                if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &conv)) && conv) {
                    if (SUCCEEDED(IWICFormatConverter_Initialize(
                            conv, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppBGRA,
                            WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) {
                        UINT iw = 0, ih = 0;
                        IWICFormatConverter_GetSize(conv, &iw, &ih);
                        if (iw > 0 && ih > 0) {
                            size_t bytes = (size_t)iw * (size_t)ih * 4u;
                            uint8_t *buf = (uint8_t *)malloc(bytes);
                            if (buf && SUCCEEDED(IWICBitmapSource_CopyPixels(
                                    (IWICBitmapSource *)conv, NULL, iw * 4u, (UINT)bytes, buf))) {
                                *pixels = buf;
                                *w = (int)iw;
                                *h = (int)ih;
                                ok = true;
                            } else {
                                free(buf);
                            }
                        }
                    }
                    IWICFormatConverter_Release(conv);
                }
                IWICBitmapFrameDecode_Release(frame);
            }
            IWICBitmapDecoder_Release(decoder);
        }
        IWICImagingFactory_Release(factory);
    }
    return ok;
}

bool qlcg_png_save_bgra(const wchar_t *path, const uint8_t *bgra, int w, int h) {
    if (!path || !bgra || w <= 0 || h <= 0) return false;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *factory = NULL;
    IWICStream *stream = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *props = NULL;
    const UINT stride = (UINT)w * 4u;
    const UINT bytes = stride * (UINT)h;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IWICImagingFactory, (LPVOID *)&factory)) && factory) {
        if (SUCCEEDED(IWICImagingFactory_CreateStream(factory, &stream)) && stream &&
            SUCCEEDED(IWICStream_InitializeFromFilename(stream, path, GENERIC_WRITE)) &&
            SUCCEEDED(IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder)) && encoder &&
            SUCCEEDED(IWICBitmapEncoder_Initialize(encoder, (IStream *)stream, WICBitmapEncoderNoCache)) &&
            SUCCEEDED(IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &props)) && frame &&
            SUCCEEDED(IWICBitmapFrameEncode_Initialize(frame, props)) &&
            SUCCEEDED(IWICBitmapFrameEncode_SetSize(frame, (UINT)w, (UINT)h))) {
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
            IWICBitmapFrameEncode_SetPixelFormat(frame, &fmt);
            if (SUCCEEDED(IWICBitmapFrameEncode_WritePixels(frame, (UINT)h, stride, bytes,
                                                            (BYTE *)bgra))) {
                IWICBitmapFrameEncode_Commit(frame);
                IWICBitmapEncoder_Commit(encoder);
                ok = true;
            }
        }
    }
    if (props) IPropertyBag2_Release(props);
    if (frame) IWICBitmapFrameEncode_Release(frame);
    if (encoder) IWICBitmapEncoder_Release(encoder);
    if (stream) IWICStream_Release(stream);
    if (factory) IWICImagingFactory_Release(factory);
    return ok;
}
