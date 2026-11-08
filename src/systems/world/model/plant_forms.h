/* 植物几何：对齐 base PlantForms.ts */
#ifndef QLCG_PLANT_FORMS_H
#define QLCG_PLANT_FORMS_H

#include "../../../core/config.h"
#include "lp_builder.h"

enum {
    PLANT_MODEL_VARIANT_1 = VEG_FORM_RICH + 1,
    PLANT_MODEL_VARIANT_2,
    PLANT_VARIANT_COUNT = 3
};

static inline int qlcg_plant_model_form(int setting, unsigned variant) {
    if (setting == VEG_FORM_SIMPLE) return VEG_FORM_SIMPLE;
    if (setting == VEG_FORM_RICH && variant > 0 && variant < PLANT_VARIANT_COUNT)
        return VEG_FORM_RICH + (int)variant;
    return VEG_FORM_NORMAL;
}

static inline int qlcg_plant_model_variant(int form) {
    return form >= PLANT_MODEL_VARIANT_1 && form <= PLANT_MODEL_VARIANT_2
        ? form - VEG_FORM_RICH : 0;
}

static inline int qlcg_plant_category_has_variants(int category) {
    return category == PLANT_CAT_CAO || category == PLANT_CAT_SHU ||
           category == PLANT_CAT_GUAN || category == PLANT_CAT_SHUI ||
           category == PLANT_CAT_HUA || category == PLANT_CAT_NONG;
}

/* 返回1成功；out 由调用方释放 */
int qlcg_plant_build(const char *id, int season, int form, LpGeometry *out);

#endif
