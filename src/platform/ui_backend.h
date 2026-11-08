#ifndef QLCG_UI_BACKEND_H
#define QLCG_UI_BACKEND_H
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>
struct UiContext;
bool qlcg_platform_font_init(struct UiContext *ui);
void qlcg_platform_font_shutdown(struct UiContext *ui);
void qlcg_platform_font_rebuild(struct UiContext *ui);
bool qlcg_platform_text_raster(struct UiContext *ui, int face, int px, bool bold,
    const char *text, uint8_t **pixels, int *width, int *height);
void qlcg_platform_teacup(struct UiContext *ui, const wchar_t *path);
#endif
