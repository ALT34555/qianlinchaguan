/* 农作物共用建模接口 */
#ifndef QLCG_NONG_MODEL_H
#define QLCG_NONG_MODEL_H

#include "../../plant_arch_internal.h"

/* 作物株型 */
typedef enum {
    CROP_CANE,
    CROP_BRASSICA,
    CROP_FIBER_HERB,
    CROP_BOLL_SHRUB,
    CROP_GRAIN,
    CROP_STEM_HERB,
    CROP_ALLIUM,
    CROP_ROSETTE,
    CROP_FLOATING,
    CROP_WOODY
} CropHabit;

typedef enum {
    CROP_NONE, CROP_WHEAT, CROP_RICE, CROP_MILLET,
    CROP_BAST, CROP_BARK, CROP_POD, CROP_SESAME, CROP_CAPSULE,
    CROP_ALLIUM_HEAD, CROP_PERILLA_HEAD, CROP_WATER_FRUIT,
    CROP_SILIQUE, CROP_CARROT, CROP_RADISH, CROP_MULBERRY
} CropProduce;

typedef enum {
    CROP_LEAF_OVATE, CROP_LEAF_PALMATE, CROP_LEAF_TRIFOLIATE,
    CROP_LEAF_FLAT, CROP_LEAF_TUBE, CROP_LEAF_KEEL,
    CROP_LEAF_CUP, CROP_LEAF_FEATHER, CROP_LEAF_LOBED
} CropLeaf;

/* 作物建模配置 */
typedef struct {
    CropHabit habit;
    CropProduce produce;
    CropLeaf leaf_form;
    int cold, heat;
    int bloom_season;
    double winter_leaf;
    int stems, branches;
    double height_scale, spread_scale;
    double stem_r, leaf_len, leaf_w;
    double bloom_size, fruit_size;
    int fiber;
} CropProfile;

/* 农作物株型变体 */
static inline int qlcg_pa_crop_shape(const PdDef *def) {
    return (int)pd_num(&def->params, "shapeVariant", 0);
}

void qlcg_pa_build_crop(LpBuilder *b, const PdDef *def, int season, int form,
                        const CropProfile *p);

#endif
