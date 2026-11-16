/* 气候区：热量带×冬季档×湿润档 */
#ifndef QLCG_CLIMATE_ZONE_H
#define QLCG_CLIMATE_ZONE_H

#include "planet_macro.h"

#define QLCG_ZONE_UNKNOWN 0xFFu

/* 20 个气候区，顺序固定，可入表 */
enum {
    QLCG_ZONE_TROPICAL_RAINFOREST = 0,  /* 热带雨林 */
    QLCG_ZONE_TROPICAL_MONSOON,         /* 热带季雨林 */
    QLCG_ZONE_TROPICAL_SAVANNA,         /* 热带草原 */
    QLCG_ZONE_TROPICAL_DESERT,          /* 热带荒漠 */
    QLCG_ZONE_SUBTROPICAL_EVERGREEN,    /* 亚热常绿阔叶林 */
    QLCG_ZONE_SUBTROPICAL_MONSOON,      /* 亚热季风林 */
    QLCG_ZONE_SUBTROPICAL_DECIDUOUS,    /* 亚热落叶混交林 */
    QLCG_ZONE_SUBTROPICAL_STEPPE,       /* 亚热草原 */
    QLCG_ZONE_SUBTROPICAL_DESERT,       /* 亚热荒漠 */
    QLCG_ZONE_TEMPERATE_RAINFOREST,     /* 温带雨林 */
    QLCG_ZONE_TEMPERATE_DECIDUOUS,      /* 温带落叶林 */
    QLCG_ZONE_TEMPERATE_MONSOON,        /* 温带季风林 */
    QLCG_ZONE_TEMPERATE_STEPPE,         /* 温带草原 */
    QLCG_ZONE_TEMPERATE_DESERT,         /* 温带荒漠 */
    QLCG_ZONE_BOREAL_MIXED,             /* 寒温带混交林 */
    QLCG_ZONE_TAIGA,                    /* 北方针叶林 */
    QLCG_ZONE_BOREAL_STEPPE,            /* 寒温带草原 */
    QLCG_ZONE_TUNDRA,                   /* 苔原 */
    QLCG_ZONE_COLD_DESERT,              /* 寒漠 */
    QLCG_ZONE_ICE_CAP,                  /* 冰盖 */
    QLCG_ZONE_COUNT
};

/* 分区元数据：三维档位与舆图图层色 */
typedef struct {
    const char *key;      /* i18n 键 climate.zone.* */
    uint8_t heat;         /* 0热 1亚热 2温 3寒 4冰封 */
    uint8_t winter;       /* 0无冬 1暖冬 2冷冬 3严冬 4极冬 */
    uint8_t wet;          /* 0荒漠 1半干 2半湿 3湿润 4季风 */
    uint8_t rgb[3];       /* 舆图气候层颜色 */
} QlcgClimateZoneInfo;

/* 名称键与元数据；越界回退 unknown */
const char *qlcg_climate_zone_key(int zone);
const QlcgClimateZoneInfo *qlcg_climate_zone_info(int zone);

/* 纯函数：求气候区 */
int qlcg_climate_zone_of(float tmean, float amp, float moisture,
                         float rainfall, float monsoon);

/* 生成期填充每格 */
void qlcg_climate_zone_build(MacroWorld *w);
void qlcg_climate_zone_release(MacroWorld *w);

/* 查询入口，均为常数时间 */
int  qlcg_climate_zone_cell(const MacroWorld *w, int cell);
int  qlcg_climate_zone_at(const MacroWorld *w, float wx, float wz);
int  qlcg_climate_zone_chunk(const MacroWorld *w, int cx, int cz);
int  qlcg_climate_zone_world(const MacroWorld *w, double wx, double wz);
void qlcg_climate_zone_row(const MacroWorld *w, int row, uint8_t *out);

#endif /* QLCG_CLIMATE_ZONE_H */
