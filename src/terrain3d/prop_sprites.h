/* 远景贴图：四季图集与内容 UV */
#ifndef QLCG_PROP_SPRITES_H
#define QLCG_PROP_SPRITES_H

#include "gl_loader.h"
#include <stdbool.h>

#define QLCG_SPRITE_TILE   64
#define QLCG_SPRITE_PAD    4
#define QLCG_SPRITE_ATLAS  2048
#define QLCG_SPRITE_STRIDE (QLCG_SPRITE_TILE + QLCG_SPRITE_PAD * 2)

typedef struct {
    float u0, v0, u1, v1;
    float fw, fh;   /* 内容包围盒占取景框的比例 */
} PropUvRect;

typedef struct {
    GLuint     tex;
    int        plant_count;
    int        rock_count;
    PropUvRect *plant_uv;
    PropUvRect *rock_uv;
    bool       ready;
} PropSprites;

/* 按物种表与岩石表逐张装载并拼图集 */
bool qlcg_prop_sprites_init(PropSprites *ps, const char *root);
void qlcg_prop_sprites_free(PropSprites *ps);
/* 缺图返回 false */
bool qlcg_prop_sprites_plant_uv(const PropSprites *ps, int species, int season, PropUvRect *out);
bool qlcg_prop_sprites_rock_uv(const PropSprites *ps, int archetype, int season, PropUvRect *out);
bool qlcg_prop_sprites_can_far(const PropSprites *ps);   /* 至少一张可用 */

#endif
