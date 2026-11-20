/* 植物丛表：混生丛与单物种优势丛 */
#ifndef QLCG_PLANT_GUILD_H
#define QLCG_PLANT_GUILD_H

#include <stdint.h>

/* 尺寸档 */
enum {
    QLCG_GUILD_TIER_GROUND = 0,
    QLCG_GUILD_TIER_SHRUB,
    QLCG_GUILD_TIER_TREE
};

/* 铺法 */
enum {
    QLCG_GUILD_LAYOUT_GRID = 0,
    QLCG_GUILD_LAYOUT_ROWS,
    QLCG_GUILD_LAYOUT_SCATTER
};

/* 环位：Chebyshev 半径占半边长 */
enum {
    QLCG_GUILD_RING_CORE = 0,
    QLCG_GUILD_RING_INNER,
    QLCG_GUILD_RING_OUTER,
    QLCG_GUILD_RING_ANY
};

enum { GUILD_GROUND, GUILD_ON_PLANT, GUILD_ON_WOOD };

/* 混生丛成员 */
typedef struct {
    int      species;   /* 物种号，-1 无效 */
    uint16_t n_min, n_max;
    uint8_t  ring;
    uint8_t  clump, attachment;
} QlcgGuildMember;

/* 一个丛的定义 */
typedef struct {
    const char      *id;
    const char      *name;
    uint32_t         zone_mask;      /* bit i = 气候区 i */
    int16_t          species;        /* 优势丛主物种；混生丛 -1 */
    int16_t          rock;
    int16_t          member_count;
    int16_t          n_min, n_max;   /* 优势丛散点株数 */
    uint8_t          tier;
    uint8_t          mono;
    uint8_t          layout;
    uint8_t          grass_tufts;
    float            spacing, jitter, row_gap, plant_gap, min_gap;
    float            scale_lo, scale_hi, weight, grass_scale;
    const QlcgGuildMember *members;
} QlcgGuildDef;

/* 读 guilds.json，返回丛数 */
int  qlcg_pg_load(const char *dir);
void qlcg_pg_unload(void);
int  qlcg_pg_count(void);
const QlcgGuildDef *qlcg_pg_guild(int index);

/* mono 取 -1/0/1，返回个数 */
int  qlcg_pg_zone_guilds(int zone, int mono, int *out, int cap);
/* 该区散生池物种，返回个数 */
int  qlcg_pg_zone_pool(int zone, int *out, int cap);
/* 按权重选丛；无可用返回 -1 */
int  qlcg_pg_pick(int zone, int mono, uint32_t roll);

#endif /* QLCG_PLANT_GUILD_H */
