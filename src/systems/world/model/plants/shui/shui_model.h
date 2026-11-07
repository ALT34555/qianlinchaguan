/* 水生植物四季形态共用 */
#ifndef QLCG_SHUI_MODEL_H
#define QLCG_SHUI_MODEL_H

#include "../../plant_arch_internal.h"

/* 水生株型 */
enum {
    SHUI_RIBBON = 0, SHUI_SWORD, SHUI_REED, SHUI_SEDGE,
    SHUI_FLOATING, SHUI_LILY, SHUI_LOTUS, SHUI_HYACINTH
};

/* 花穗与果穗 */
enum {
    SHUI_HEAD_NONE = 0, SHUI_HEAD_SPIKE, SHUI_HEAD_PANICLE,
    SHUI_HEAD_SPADIX, SHUI_HEAD_UMBEL, SHUI_HEAD_POD
};

/* 水生建模配置 */
typedef struct {
    int habit, form;
    int cold, heat;
    int bloom_season;
    double winter_leaf;
    int blades, culms, pads;
    double spread, blade_len, blade_width, arch, pad_radius, float_y;
    int bloom_form, seed_form;
    double bloom_size, seed_size;
    double height_scale, spread_scale;
} ShuiProfile;

/* 四季结构量 */
typedef struct {
    double height, blade, density, droop, dry, sprout, stress;
    int bloom, fruit, seed_head, dormant, frozen, standing;
} ShuiSeason;

ShuiSeason qlcg_pa_shui_season(const PdDef *def, int season, const ShuiProfile *p);
void qlcg_pa_build_shui(LpBuilder *b, const PdDef *def, int season, int form, const ShuiProfile *p);

#endif
