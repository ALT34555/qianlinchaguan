#ifdef _WIN32
#include "platform/win32/native.h"
#include "platform/platform.h"
/* 帧缓冲存 BMP/PNG，无桌面环境可验证画面 */
#define COBJMACROS
#include "core/screenshot.h"
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_PACK_ALIGNMENT
#define GL_PACK_ALIGNMENT 0x0D05
#endif

bool qlcg_screenshot_bmp(const wchar_t *path, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    size_t row = (size_t)w * 4u;
    size_t size = row * (size_t)h;
    uint8_t *pixels = (uint8_t *)malloc(size);
    if (!pixels) return false;
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels);

    /* 导出像素设为不透明 */
    for (size_t i = 3; i < size; i += 4) pixels[i] = 255;

    uint32_t file_size = (uint32_t)(54u + size);
    uint8_t header[54];
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    memcpy(&header[2], &file_size, 4);
    uint32_t offset = 54;
    memcpy(&header[10], &offset, 4);
    uint32_t dib = 40;
    memcpy(&header[14], &dib, 4);
    int32_t iw = (int32_t)w, ih = (int32_t)h;
    memcpy(&header[18], &iw, 4);
    memcpy(&header[22], &ih, 4);
    uint16_t planes = 1, bpp = 32;
    memcpy(&header[26], &planes, 2);
    memcpy(&header[28], &bpp, 2);
    memcpy(&header[34], &file_size, 4);

    FILE *fp = NULL;
    if (_wfopen_s(&fp, path, L"wb") != 0 || !fp) {
        free(pixels);
        return false;
    }
    fwrite(header, 1, sizeof(header), fp);
    fwrite(pixels, 1, size, fp);
    fclose(fp);
    free(pixels);
    return true;
}

/* 抓一帧 BGRA 像素 */
static uint8_t *grab_pixels(int w, int h) {
    size_t row = (size_t)w * 4u;
    size_t size = row * (size_t)h;
    uint8_t *pixels = (uint8_t *)malloc(size);
    if (!pixels) return NULL;
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    for (size_t i = 3; i < size; i += 4) pixels[i] = 255;
    return pixels;
}

bool qlcg_screenshot_png(const wchar_t *path, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    uint8_t *pixels = grab_pixels(w, h);
    if (!pixels) return false;

    /* glReadPixels 自下向上，PNG 需翻转 */
    {
        size_t row = (size_t)w * 4u;
        uint8_t *flipped = (uint8_t *)malloc(row * (size_t)h);
        if (!flipped) {
            free(pixels);
            return false;
        }
        for (int y = 0; y < h; y++) {
            memcpy(flipped + (size_t)y * row, pixels + (size_t)(h - 1 - y) * row, row);
        }
        free(pixels);
        pixels = flipped;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IWICImagingFactory *factory = NULL;
    IWICStream *stream = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *props = NULL;
    const UINT stride = (UINT)w * 4u;
    const UINT bytes = stride * (UINT)h;

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
            if (SUCCEEDED(IWICBitmapFrameEncode_WritePixels(frame, (UINT)h, stride, bytes, pixels))) {
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
    free(pixels);
    return ok;
}

/* 按扩展名分派保存格式 */
bool qlcg_screenshot_save(const wchar_t *path, int w, int h) {
    const wchar_t *dot = wcsrchr(path, L'.');
    if (dot && (_wcsicmp(dot, L".png") == 0)) return qlcg_screenshot_png(path, w, h);
    return qlcg_screenshot_bmp(path, w, h);
}

#endif
