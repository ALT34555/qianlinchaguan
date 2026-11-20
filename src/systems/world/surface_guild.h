/* 区块内植物丛：世界坐标决定，跨区块一致 */
#ifndef QLCG_SURFACE_GUILD_H
#define QLCG_SURFACE_GUILD_H

#include "planet_macro.h"
#include "climate_season.h"
#include "model/plant_guild.h"

#define QLCG_GUILD_BOX_MAX 16

/* 丛的圆盘轮廓；半径随角度起伏 */
typedef struct {
    float    cx, cz;   /* 世界中心 */
    float    r;        /* 基准半径 */
    uint32_t seed;     /* 轮廓噪声 */
    int16_t  species;  /* 优势丛主物种 */
    uint8_t  mono;
    uint8_t  ambient;
} QlcgGuildShape;

float qlcg_guild_radius(const QlcgGuildDef *def, uint32_t roll);
bool qlcg_guild_place(RegionData *reg, const MacroWorld *w, const GameSettings *settings,
                      const QlcgClimateSeason *cs, const QlcgGuildDef *def,
                      const QlcgGuildShape *shape, int season, int *plants, int *tris);

/* 查询丛格；返回丛号，空格返回 -1 */
int qlcg_guild_query(const MacroWorld *w, int cx, int cz, QlcgGuildShape *out);

/* cs 为空时仅收集轮廓 */
int qlcg_guild_scatter(RegionData *reg, const MacroWorld *w,
                       int season_idx, const GameSettings *settings,
                       const QlcgClimateSeason *cs,
                       QlcgGuildShape *shapes, int shape_cap);

int qlcg_aquatic_guild_site(const RegionData *reg, float x, float z, int kind,
                           float *bed, float *depth);
int qlcg_aquatic_guild_grass(RegionData *reg, float x, float z, int kind, int zone,
                            float clearance, uint32_t seed);

/* 世界点是否落在任一丛内；pad 为额外边距 */
int qlcg_guild_inside(const QlcgGuildShape *shapes, int count,
                      float wx, float wz, float pad);

#endif /* QLCG_SURFACE_GUILD_H */
