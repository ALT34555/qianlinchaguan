#ifndef QLCG_UI_FONT_MOD_H
#define QLCG_UI_FONT_MOD_H

#include "ui_core.h"

#define UI_FONT_MOD_MAX 64

typedef struct {
    char    id[96];       /* 稳定标识，存进设置；如 mod:xxx.ttf */
    wchar_t family[64];   /* 传给 CreateFontW 的真实族名（非清单别名） */
    wchar_t label[48];    /* 设置页显示名 */
    wchar_t file[260];    /* 字体文件路径 */
} UiModFont;

typedef struct {
    UiModFont items[UI_FONT_MOD_MAX];
    int       count;
} UiModFontList;

/* 扫目录注册字体，返回数量；list 可 NULL */
int ui_font_mod_scan(const wchar_t *dir, UiModFontList *list);

/* 按 id 查找，找不到返回 NULL */
const UiModFont *ui_font_mod_find(const UiModFontList *list, const char *id);

/* 释放已注册的字体资源 */
void ui_font_mod_unload(void);

#endif /* QLCG_UI_FONT_MOD_H */
