#include "cao_model.h"

/* 芭茅：高秆丛生，秋抽芒穗 */
void qlcg_plant_cao_bamao(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_TUSSOCK, .cold = 3, .heat = 1,
        .bloom_season = PD_SEASON_AUTUMN, .winter_leaf = 0.35,
        .blades = 36, .leaf_len = 1.15, .leaf_width = 0.055,
        .arch = 1.20, .plump = 1.00, .leaf_form = HERB_LEAF_LINEAR,
        .bloom_form = HERB_HEAD_PANICLE, .fruit_form = HERB_HEAD_PANICLE,
        .bloom_size = 0.30, .fruit_size = 0.05,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.height_scale = 1.16;
        profile.spread_scale = 0.74;
        profile.blades = 30;
        profile.arch = 1.36;
        break;
    case HERB_SHAPE_B:
        profile.height_scale = 0.86;
        profile.spread_scale = 1.32;
        profile.blades = 41;
        profile.arch = 0.86;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
