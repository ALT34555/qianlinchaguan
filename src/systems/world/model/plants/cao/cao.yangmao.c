#include "cao_model.h"

/* 羊茅草：冷季细叶草团，冬常绿 */
void qlcg_plant_cao_yangmao(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_TUSSOCK, .cold = 1, .heat = 3,
        .bloom_season = PD_SEASON_SPRING, .winter_leaf = 0.75,
        .blades = 30, .leaf_len = 0.90, .leaf_width = 0.022,
        .arch = 0.95, .plump = 0.90, .leaf_form = HERB_LEAF_LINEAR,
        .bloom_form = HERB_HEAD_NONE, .fruit_form = HERB_HEAD_NONE,
        .bloom_size = 0.05, .fruit_size = 0.02,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.height_scale = 0.85;
        profile.spread_scale = 0.76;
        profile.blades = 36;
        profile.leaf_width = 0.018;
        profile.arch = 1.08;
        break;
    case HERB_SHAPE_B:
        profile.height_scale = 1.14;
        profile.spread_scale = 1.28;
        profile.blades = 25;
        profile.leaf_width = 0.028;
        profile.arch = 0.78;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
