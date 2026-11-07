#ifndef QLCG_UI_SPRITE_H
#define QLCG_UI_SPRITE_H

#include "ui_core.h"

/* Sprite9 分段模式 */
enum {
    SP_MAX_SEG   = 16,
    SP_MAX_ITEMS = 128
};
enum {
    SP_MODE_FIXED   = 0,
    SP_MODE_STRETCH = 1,
    SP_MODE_REPEAT  = 2
};

typedef struct {
    short         a, b;
    unsigned char mode;
} SpriteSeg;

/* 每张源图独占一张纹理，四层各占其四分之一 */
typedef struct SpriteDef {
    char      name[64];
    GLuint    tex;
    int       tex_w, tex_h;              /* 纹理像素尺寸 */
    int       layer_x[4], layer_y[4];    /* 四层在纹理内的左上角 */
    int       w, h;                      /* 单象限原始尺寸 */
    SpriteSeg xseg[SP_MAX_SEG];
    int       xn;
    SpriteSeg yseg[SP_MAX_SEG];
    int       yn;
    int       cx0, cx1, cy0, cy1;
} SpriteDef;

/* 运行时扫描并加载全部源图 */
bool ui_sprite_load_all(UiContext *ui);

/* 释放全部已加载纹理 */
void ui_sprite_release(void);

/* 轮询源图变动并自动热重载 */
void ui_sprite_poll_reload(UiContext *ui);

/* 运行时精灵数量与按索引访问 */
int              ui_sprite_count(void);
const SpriteDef *ui_sprite_at(int index);

/* 四层叠加：底板/主色/装饰/原样 */
void ui_sprite4(UiContext *ui, const struct SpriteDef *sp, float x, float y, float w, float h,
                uint32_t c_surface, uint32_t c_primary, uint32_t c_accent);

/* 四层全白单色剪影 */
void ui_sprite_plain(UiContext *ui, const struct SpriteDef *sp, float x, float y, float w, float h);

/* 单层绘制：layer 0..3 */
void ui_sprite_layer(UiContext *ui, const struct SpriteDef *sp, int layer,
                     float x, float y, float w, float h, uint32_t tint);

/* 图标：取原样层内容区等比居中 */
void ui_sprite_icon(UiContext *ui, const struct SpriteDef *sp, float x, float y, float size,
                    uint32_t tint);

/* 多色图标：三层令牌染色加原样细节层 */
void ui_sprite_icon4(UiContext *ui, const struct SpriteDef *sp, float x, float y, float size,
                     uint32_t c_surface, uint32_t c_primary, uint32_t c_accent);

/* 多色图标绕中心旋转 rad 弧度 */
void ui_sprite_icon4_rot(UiContext *ui, const struct SpriteDef *sp, float x, float y, float size,
                         float rad, uint32_t c_surface, uint32_t c_primary, uint32_t c_accent);

/* 按名字查 sprite，找不到返回 NULL */
const struct SpriteDef *ui_sprite_by_name(const char *name);

#endif
