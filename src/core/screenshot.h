#ifndef QLCG_SCREENSHOT_H
#define QLCG_SCREENSHOT_H

#include "../terrain3d/gl_loader.h"

/* 帧缓冲存 BMP，无需桌面环境即可验证 */
bool qlcg_screenshot_bmp(const wchar_t *path, int w, int h);

/* 存成 PNG（WIC 编码，体积更小） */
bool qlcg_screenshot_png(const wchar_t *path, int w, int h);

/* 按扩展名分派 */
bool qlcg_screenshot_save(const wchar_t *path, int w, int h);

#endif /* QLCG_SCREENSHOT_H */
