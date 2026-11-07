/* 物种表/生成定义，同 base Plants.ts */
#ifndef QLCG_PLANT_DEF_H
#define QLCG_PLANT_DEF_H

#include "pd_dict.h"

enum {
    PD_STATE_PLANTED = 0, PD_STATE_NORMAL, PD_STATE_SEEDLING, PD_STATE_SUBADULT,
    PD_STATE_FLOWERING, PD_STATE_FRUITING, PD_STATE_WITHERED, PD_STATE_COUNT
};

enum {
    PD_SEASON_SPRING = 0, PD_SEASON_SUMMER, PD_SEASON_AUTUMN, PD_SEASON_WINTER, PD_SEASON_COUNT
};

typedef struct {
    char  *id;
    char  *name;
    char  *latin;
    char  *climate;
    char  *archetype;
    double seed;
    int    has_seed;
    int    has_tags;
    double scale;
    PdDict params;
    PdDict palette;
    char **tags;
    int    tag_count;
} PdVariant;

typedef struct {
    PdVariant base;
    PdVariant state[PD_STATE_COUNT];
    int       has_state[PD_STATE_COUNT];
    int       states[PD_STATE_COUNT];
    int       state_count;
    char     *declared[PD_STATE_COUNT];
    int       declared_count;
    int       category;   /* 六类编码，-1 未定 */
} PdSpecies;

typedef struct {
    char  *id;
    char  *archetype;
    char  *base_archetype;
    char  *climate;
    char  *name;
    double seed;
    PdDict params;
    PdDict palette;      /* 已解析为 #rrggbb */
    unsigned build_seed;
    int    category;
    int    state;
    int    snow;
    int    bloom;
    int    fruit;
    double scale;
    char **tags;          /* 引用物种表，不拥有 */
    int    tag_count;
    char   bloom_color[8];
    int    has_bloom_color;
} PdDef;

/* 装载 plants 目录，返回物种数，<0 失败 */
int  qlcg_pd_load(const char *dir);
void qlcg_pd_unload(void);
int  qlcg_pd_species_count(void);
const PdSpecies *qlcg_pd_species(int i);
const PdSpecies *qlcg_pd_species_by_id(const char *id);
/* 六类 id → 编码，未知返回 -1 */
int  qlcg_pd_category_code(const char *key);
/* 精确 tag；有返回 1 */
int  qlcg_pd_has_tag(const PdSpecies *sp, const char *tag);
/* 抗性 tag 等级 0..3，缺失返回 -1 */
int  qlcg_pd_tag_level(const PdSpecies *sp, const char *prefix);

/* 同 base buildPlantById */
int  qlcg_pd_build_def(const char *id, int season, PdDef *out);
/* force_normal 忽略 @状态后缀 */
int  qlcg_pd_build_def_ex(const char *id, int season, int force_normal, PdDef *out);
void qlcg_pd_free_def(PdDef *def);

const char *qlcg_pd_state_name(int state);
int         qlcg_pd_state_of(const char *name);
const char *qlcg_pd_season_name(int season);

#endif
