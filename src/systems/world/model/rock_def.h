/* 岩石数据层：对齐 Rocks.ts/types.ts */
#ifndef QLCG_ROCK_DEF_H
#define QLCG_ROCK_DEF_H

#include "lp_math.h"
#include "pd_dict.h"

enum {
    ROCK_MAT_SOLID = 0, ROCK_MAT_MOSS, ROCK_MAT_SNOW, ROCK_MAT_ICE, ROCK_MAT_COUNT
};

enum {
    ROCK_FORM_OUTCROP = 0, ROCK_FORM_BURIED, ROCK_FORM_STACKED, ROCK_FORM_CRUSTED, ROCK_FORM_COUNT
};

enum { ROCK_SIZE_LEGACY = -1, ROCK_SIZE_SMALL, ROCK_SIZE_MEDIUM, ROCK_SIZE_LARGE,
       ROCK_SIZE_GIANT, ROCK_SIZE_MEGA, ROCK_SIZE_COUNT };
enum { ROCKS_PER_SIZE = 16, ROCK_LEGACY_COUNT = 9, ROCK_MEGA_COUNT = 8,
       ROCK_CATALOG_CAPACITY = ROCK_LEGACY_COUNT + ROCK_SIZE_MEGA * ROCKS_PER_SIZE + ROCK_MEGA_COUNT };

/* 运行时构建定义 */
typedef struct {
    char  *id;
    char  *archetype;
    char  *name;
    char  *latin;
    char  *family;
    double seed;
    double height_m;
    int    form;
    unsigned shape_seed;
    int    crust_material;   /* -1 表示无覆被 */
    char   crust_color[40];
    int    has_crust_color;
    LpRgb  crust_rgb;
    PdDict params;           /* 已合并的数值参数 */
    PdDict palette;          /* 槽 -> #rrggbb */
    unsigned build_seed;
    int    season;
} RockBuildDef;

int  qlcg_rock_load(const char *dir);
void qlcg_rock_unload(void);
int  qlcg_rock_count(void);
const char *qlcg_rock_id(int index);
const char *qlcg_rock_name(int index);
int qlcg_rock_size_class(int index);
int qlcg_rock_pick(int size_class, unsigned roll);

/* form/season 0..3；chunk 类型号 */
int  qlcg_rock_build_def(const char *id, int season, int form, int chunk, RockBuildDef *out);
void qlcg_rock_free_def(RockBuildDef *d);

const char *qlcg_rock_family_palette(const char *family, const char *slot);
const char *qlcg_rock_mat_name(int mat);
int         qlcg_rock_mat_index(const char *name);
const char *qlcg_rock_form_name(int form);

#endif
