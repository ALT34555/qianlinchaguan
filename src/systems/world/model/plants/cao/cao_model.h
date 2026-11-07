/* 草本科共用建模接口 */
#ifndef QLCG_CAO_MODEL_H
#define QLCG_CAO_MODEL_H

#include "../../plant_arch_internal.h"

/* 草本株型 */
enum {
    HERB_TUSSOCK = 0, HERB_STOLON, HERB_FERN, HERB_FORB, HERB_BRAMBLE,
    HERB_STRAP, HERB_ROSETTE, HERB_CUSHION, HERB_SUAEDA, HERB_SALICORNIA
};

/* 叶片形态 */
enum {
    HERB_LEAF_LINEAR = 0, HERB_LEAF_STRAP, HERB_LEAF_ROUND, HERB_LEAF_FERN, HERB_LEAF_SCALE
};

/* 花序与果序 */
enum {
    HERB_HEAD_NONE = 0, HERB_HEAD_SPIKE, HERB_HEAD_PANICLE, HERB_HEAD_RACEME,
    HERB_HEAD_CYME, HERB_HEAD_DIGITATE, HERB_HEAD_CUP
};

enum { HERB_SHAPE_A = 1, HERB_SHAPE_B = 2 };

static inline int qlcg_pa_herb_shape(const PdDef *def) {
    return (int)pd_num(&def->params, "shapeVariant", 0);
}

/* 草本建模配置 */
typedef struct {
    int habit;
    int cold, heat;
    int bloom_season;
    double winter_leaf;
    int blades, stems, leaves;
    double height_scale, spread_scale;
    int cushion_lobes;
    double leaf_len, leaf_width, arch, plump;
    int leaf_form, bloom_form, fruit_form;
    double bloom_size, fruit_size;
} HerbProfile;

void qlcg_pa_build_herb(LpBuilder *b, const PdDef *def, int season, int form, const HerbProfile *p);

#endif
