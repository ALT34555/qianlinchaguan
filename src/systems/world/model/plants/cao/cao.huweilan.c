#include "cao_model.h"

/* 虎尾兰：二列剑叶，夏抽总状葶 */
void qlcg_plant_cao_huweilan(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_STRAP, .cold = 4, .heat = 1,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.55,
        .blades = 16, .leaf_len = 1.00, .leaf_width = 0.105,
        .arch = 1.00, .plump = 1.10, .leaf_form = HERB_LEAF_STRAP,
        .bloom_form = HERB_HEAD_RACEME, .fruit_form = HERB_HEAD_NONE,
        .bloom_size = 0.10, .fruit_size = 0.03,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.height_scale = 1.16;
        profile.spread_scale = 0.82;
        profile.blades = 13;
        profile.leaf_width = 0.083;
        profile.arch = 1.13;
        break;
    case HERB_SHAPE_B:
        profile.height_scale = 0.79;
        profile.spread_scale = 1.30;
        profile.blades = 19;
        profile.leaf_width = 0.129;
        profile.arch = 0.83;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
