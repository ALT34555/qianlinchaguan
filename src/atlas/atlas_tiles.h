#ifndef QLCG_ATLAS_TILES_H
#define QLCG_ATLAS_TILES_H

#include "atlas_view.h"

/* base 步长 2，边缘各留一个纹素 */
#define ATLAS_TILE_SAMPLE_PX 64
#define ATLAS_TILE_PX        (ATLAS_TILE_SAMPLE_PX + 2)
#define ATLAS_TILE_GRID      16
#define ATLAS_TILE_SLOTS     (ATLAS_TILE_GRID * ATLAS_TILE_GRID)
#define ATLAS_TILE_ATLAS_PX  (ATLAS_TILE_PX * ATLAS_TILE_GRID)
#define ATLAS_TILE_MIN_LEVEL 5
#define ATLAS_TILE_MAX_LEVEL 24
/* 瓦片屏幕目标像素，对齐base TILE=128 */
#define ATLAS_TILE_SCREEN_PX 128.0f
/* 层级迟滞：相对目标尺寸的判定区间，取 √2 两侧防抖 */
#define ATLAS_TILE_HYST_HI   1.45f
#define ATLAS_TILE_HYST_LO   0.69f

struct AtlasTileCache;

/* 供渲染绑定：图集、元数据与当前层级 */
typedef struct {
    GLuint tex;
    GLuint meta;
    int    level;
    GLuint water;
    float river_mix;
} AtlasTileView;

void                   qlcg_atlas_tiles_destroy(struct AtlasTileCache *tc);

/* 世界纹理重传后，瓦片内容过期 */
void                   qlcg_atlas_tiles_invalidate(struct AtlasTileCache *tc);

/* 每帧推进烘焙；返回本帧是否改动了图集内容 */
bool                   qlcg_atlas_tiles_update(AtlasRenderer *ar,
                                               const MacroWorld *w,
                                               float vp_x, float vp_y, float vp_w, float vp_h,
                                               double budget_ms);

/* 取本帧渲染所需的图集状态 */
void                   qlcg_atlas_tiles_view(const struct AtlasTileCache *tc, AtlasTileView *out);

/* 图例统计：当前层级可见瓦片的就绪数与总数 */
void                   qlcg_atlas_tiles_stats(const struct AtlasTileCache *tc,
                                             int *out_ready, int *out_total, int *out_level);

#endif /* QLCG_ATLAS_TILES_H */
