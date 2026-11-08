/* 远景贴图：四季图集与内容 UV */
#include "prop_sprites.h"
#include "core/png_image.h"
#include "core/math_util.h"
#include "../systems/world/model/plant_material.h"
#include "../systems/world/model/plant_def.h"
#include "../systems/world/model/rock_material.h"
#include "../systems/world/model/rock_def.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/platform.h"

#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_UNPACK_ALIGNMENT
#define GL_UNPACK_ALIGNMENT 0x0CF5
#endif

static const char *k_cat_dirs[PLANT_CAT_COUNT] = { "cao", "guan", "hua", "nong", "shu", "shui" };

static bool make_path(char *out, size_t cap, const char *root, bool rock,
                      int cat, const char *id) {
    if (!out || !root || !id) return false;
    if (rock) return snprintf(out, cap, "%s/rocks/%s.png", root, id) > 0;
    if (cat < 0 || cat >= PLANT_CAT_COUNT) return false;
    return snprintf(out, cap, "%s/plants/%s/%s.png", root, k_cat_dirs[cat], id) > 0;
}

static bool load_png_utf8(const char *path, uint8_t **px, int *w, int *h) {
    wchar_t wpath[512];
    if (qlcg_utf8_to_wide(path, wpath, 512) <= 0) return false;
    return qlcg_png_load_bgra(wpath, px, w, h);
}

/* 单季内容包围盒；整格透明返回 false */
static bool cell_bbox(const uint8_t *px, int w, int h, int season,
                      int *x0, int *y0, int *x1, int *y1) {
    int top = season * QLCG_SPRITE_TILE;
    *x0 = QLCG_SPRITE_TILE; *y0 = QLCG_SPRITE_TILE;
    *x1 = -1; *y1 = -1;
    for (int y = 0; y < QLCG_SPRITE_TILE; y++) {
        for (int x = 0; x < QLCG_SPRITE_TILE; x++) {
            const uint8_t *p = px + ((size_t)(top + y) * (size_t)w + (size_t)x) * 4u;
            if (p[3] < 8) continue;
            if (x < *x0) *x0 = x;
            if (y < *y0) *y0 = y;
            if (x > *x1) *x1 = x;
            if (y > *y1) *y1 = y;
        }
    }
    return *x1 >= 0;
}

/* 预乘 alpha 写入图集：透明像素不贡献颜色，mip 平均不再发黑 */
static void copy_cell(uint8_t *atlas, const uint8_t *px, int w, int season,
                      int cx, int cy) {
    int top = season * QLCG_SPRITE_TILE;
    for (int y = 0; y < QLCG_SPRITE_TILE; y++) {
        const uint8_t *src = px + ((size_t)(top + y) * (size_t)w) * 4u;
        uint8_t *dst = atlas + (((size_t)(cy + y) * QLCG_SPRITE_ATLAS + (size_t)cx) * 4u);
        for (int x = 0; x < QLCG_SPRITE_TILE; x++) {
            uint32_t a = src[x * 4 + 3];
            dst[x * 4 + 0] = (uint8_t)((uint32_t)src[x * 4 + 0] * a / 255u);
            dst[x * 4 + 1] = (uint8_t)((uint32_t)src[x * 4 + 1] * a / 255u);
            dst[x * 4 + 2] = (uint8_t)((uint32_t)src[x * 4 + 2] * a / 255u);
            dst[x * 4 + 3] = (uint8_t)a;
        }
    }
}

/* 图像坐标 → 图集 UV（含半 texel 内缩） */
static void set_uv(PropUvRect *r, int cx, int cy, int x0, int y0, int x1, int y1, int atlas_height) {
    const float inv = 1.0f / (float)QLCG_SPRITE_ATLAS;
    const float inv_y = 1.0f / (float)atlas_height;
    float u0 = (float)(cx + x0) * inv;
    float u1 = (float)(cx + x1 + 1) * inv;
    float v1 = (float)(atlas_height - (cy + y0)) * inv_y;
    float v0 = (float)(atlas_height - (cy + y1 + 1)) * inv_y;
    u0 += 0.5f * inv; u1 -= 0.5f * inv;
    v0 += 0.5f * inv_y; v1 -= 0.5f * inv_y;
    if (u1 <= u0 || v1 <= v0) { u0 = v0 = 0.0f; u1 = v1 = 0.0f; }
    r->u0 = u0; r->v0 = v0; r->u1 = u1; r->v1 = v1;
    r->fw = (float)(x1 - x0 + 1) / (float)QLCG_SPRITE_TILE;
    r->fh = (float)(y1 - y0 + 1) / (float)QLCG_SPRITE_TILE;
}

static bool tile_source(const uint8_t *px, int w, int season,
                        int tile, PropUvRect *out, uint8_t *atlas, int atlas_height) {
    int cols = QLCG_SPRITE_ATLAS / QLCG_SPRITE_STRIDE;
    int cx = (tile % cols) * QLCG_SPRITE_STRIDE + QLCG_SPRITE_PAD;
    int cy = (tile / cols) * QLCG_SPRITE_STRIDE + QLCG_SPRITE_PAD;
    int x0, y0, x1, y1;
    if (!cell_bbox(px, w, QLCG_SPRITE_TILE * PD_SEASON_COUNT, season, &x0, &y0, &x1, &y1))
        return false;
    copy_cell(atlas, px, w, season, cx, cy);
    set_uv(out, cx, cy, x0, y0, x1, y1, atlas_height);
    return true;
}

bool qlcg_prop_sprites_init(PropSprites *ps, const char *root) {
    if (!ps) return false;
    memset(ps, 0, sizeof(*ps));
    qlcg_plant_material_init();
    qlcg_rock_material_init();
    int plants = qlcg_plant_species_count();
    int rocks = qlcg_rock_material_count();
    if (plants <= 0 && rocks <= 0) return false;
    int tiles = (plants + rocks) * PD_SEASON_COUNT;
    int cols = QLCG_SPRITE_ATLAS / QLCG_SPRITE_STRIDE;
    int rows = (tiles + cols - 1) / cols;
    int atlas_height = rows * QLCG_SPRITE_STRIDE;
    if (atlas_height < QLCG_SPRITE_ATLAS) atlas_height = QLCG_SPRITE_ATLAS;
    GLint max_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    if (atlas_height > max_size || QLCG_SPRITE_ATLAS > max_size) {
        fprintf(stderr, "[sprite] 图集槽位不足：%d 张\n", tiles);
        return false;
    }
    size_t bytes = (size_t)QLCG_SPRITE_ATLAS * atlas_height * 4u;
    uint8_t *atlas = (uint8_t *)calloc(1, bytes);
    ps->plant_uv = (PropUvRect *)calloc((size_t)plants * PD_SEASON_COUNT, sizeof(PropUvRect));
    ps->rock_uv  = (PropUvRect *)calloc((size_t)rocks * PD_SEASON_COUNT, sizeof(PropUvRect));
    if (!atlas || !ps->plant_uv || !ps->rock_uv) {
        free(atlas); free(ps->plant_uv); free(ps->rock_uv);
        memset(ps, 0, sizeof(*ps));
        return false;
    }
    int loaded = 0, missing = 0;
    double t0 = qlcg_time_ms();
    for (int i = 0; i < plants; i++) {
        const char *id = qlcg_plant_species_id(i);
        int cat = qlcg_plant_species_category(i);
        char path[512];
        uint8_t *px = NULL;
        int w = 0, h = 0;
        if (!id || !make_path(path, sizeof(path), root, false, cat, id) ||
            !load_png_utf8(path, &px, &w, &h) ||
            w != QLCG_SPRITE_TILE || h != QLCG_SPRITE_TILE * PD_SEASON_COUNT) {
            missing++;
            free(px);
            continue;
        }
        for (int s = 0; s < PD_SEASON_COUNT; s++) {
            int tile = i * PD_SEASON_COUNT + s;
            if (tile_source(px, w, s, tile, &ps->plant_uv[i * PD_SEASON_COUNT + s], atlas, atlas_height))
                loaded++;
        }
        free(px);
    }
    for (int i = 0; i < rocks; i++) {
        const char *id = qlcg_rock_id(i);
        char path[512];
        uint8_t *px = NULL;
        int w = 0, h = 0;
        if (!id || !make_path(path, sizeof(path), root, true, 0, id) ||
            !load_png_utf8(path, &px, &w, &h) ||
            w != QLCG_SPRITE_TILE || h != QLCG_SPRITE_TILE * PD_SEASON_COUNT) {
            missing++;
            free(px);
            continue;
        }
        for (int s = 0; s < PD_SEASON_COUNT; s++) {
            int tile = (plants + i) * PD_SEASON_COUNT + s;
            if (tile_source(px, w, s, tile, &ps->rock_uv[i * PD_SEASON_COUNT + s], atlas, atlas_height))
                loaded++;
        }
        free(px);
    }
    ps->plant_count = plants;
    ps->rock_count = rocks;
    if (!loaded) {
        free(atlas); free(ps->plant_uv); free(ps->rock_uv);
        memset(ps, 0, sizeof(*ps));
        fprintf(stderr, "[sprite] 无可用贴图（缺 %d 张）\n", missing);
        return false;
    }
    /* 行序翻成 OpenGL 自下而上 */
    uint8_t *row = (uint8_t *)malloc((size_t)QLCG_SPRITE_ATLAS * 4u);
    if (row) {
        size_t stride = (size_t)QLCG_SPRITE_ATLAS * 4u;
        for (int y = 0; y < atlas_height / 2; y++) {
            uint8_t *a = atlas + (size_t)y * stride;
            uint8_t *b = atlas + (size_t)(atlas_height - 1 - y) * stride;
            memcpy(row, a, stride);
            memcpy(a, b, stride);
            memcpy(b, row, stride);
        }
        free(row);
    }
    glGenTextures(1, &ps->tex);
    glBindTexture(GL_TEXTURE_2D, ps->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    qlcg_gl_bgra_to_rgba(atlas,(size_t)QLCG_SPRITE_ATLAS*atlas_height);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, QLCG_SPRITE_ATLAS, atlas_height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 5);
    glGenerateMipmap(GL_TEXTURE_2D);
    free(atlas);
    ps->ready = true;
    printf("[sprite] 图集就绪：%d 格，缺 %d 张，%dx%d，%.0f ms\n",
           loaded, missing, QLCG_SPRITE_ATLAS, atlas_height, qlcg_time_ms() - t0);
    return true;
}

void qlcg_prop_sprites_free(PropSprites *ps) {
    if (!ps) return;
    if (ps->tex) qlcg_gl_delete_textures(1, &ps->tex);
    free(ps->plant_uv);
    free(ps->rock_uv);
    memset(ps, 0, sizeof(*ps));
}

bool qlcg_prop_sprites_plant_uv(const PropSprites *ps, int species, int season, PropUvRect *out) {
    if (!ps || !ps->ready || !out) return false;
    if (species < 0 || species >= ps->plant_count) return false;
    if (season < 0 || season >= PD_SEASON_COUNT) season = 1;
    const PropUvRect *r = &ps->plant_uv[species * PD_SEASON_COUNT + season];
    *out = *r;
    return r->u1 > r->u0 && r->v1 > r->v0;
}

bool qlcg_prop_sprites_rock_uv(const PropSprites *ps, int archetype, int season, PropUvRect *out) {
    if (!ps || !ps->ready || !out) return false;
    if (archetype < 0 || archetype >= ps->rock_count) return false;
    if (season < 0 || season >= PD_SEASON_COUNT) season = 1;
    const PropUvRect *r = &ps->rock_uv[archetype * PD_SEASON_COUNT + season];
    *out = *r;
    return r->u1 > r->u0 && r->v1 > r->v0;
}

bool qlcg_prop_sprites_can_far(const PropSprites *ps) {
    return ps && ps->ready && ps->tex != 0;
}
