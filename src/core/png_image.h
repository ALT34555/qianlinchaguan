/* PNG 解码与保存：WIC 直通 BGRA */
#ifndef QLCG_PNG_IMAGE_H
#define QLCG_PNG_IMAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

/* 解码为 BGRA，缓冲由调用方 free */
bool qlcg_png_load_bgra(const wchar_t *path, uint8_t **pixels, int *w, int *h);
/* 原样写出 BGRA（保留 alpha），行序自上而下 */
bool qlcg_png_save_bgra(const wchar_t *path, const uint8_t *bgra, int w, int h);

#endif
