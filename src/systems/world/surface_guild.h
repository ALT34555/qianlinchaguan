/* 区块内植物丛：世界坐标决定，跨区块一致 */
#ifndef QLCG_SURFACE_GUILD_H
#define QLCG_SURFACE_GUILD_H

#include "planet_macro.h"
#include "climate_season.h"

#define QLCG_GUILD_BOX_MAX 16

/* 丛的圆盘轮廓；半径随角度起伏 */
typedef struct {
    float    cx, cz;   /* 世界中心 */
    float    r;        /* 基准半径 */
    uint32_t seed;     /* 轮廓噪声 */
    int16_t  species;  /* 优势丛主物种 */
    uint8_t  mono;
} QlcgGuildShape;

/* 查询丛格；返回丛号，空格返回 -1 */
int qlcg_guild_query(const MacroWorld *w, int cx, int cz, QlcgGuildShape *out);

/* 生成丛植株并填 shapes；返回写入的形状数 */
int qlcg_guild_scatter(RegionData *reg, const MacroWorld *w,
                       int season_idx, const GameSettings *settings,
                       const QlcgClimateSeason *cs,
                       QlcgGuildShape *shapes, int shape_cap);

/* 世界点是否落在任一丛内；pad 为额外边距 */
int qlcg_guild_inside(const QlcgGuildShape *shapes, int count,
                      float wx, float wz, float pad);

#endif /* QLCG_SURFACE_GUILD_H */
