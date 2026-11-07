#include "cao_model.h"

/* 旱棘丛：曲折刺茎，春花冬露骨 */
void qlcg_plant_cao_hanji(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_BRAMBLE, .cold = 2, .heat = 1,
        .bloom_season = PD_SEASON_SPRING, .winter_leaf = 0.15,
        .stems = 6, .leaf_len = 1.00, .leaf_width = 0.045,
        .arch = 1.00, .plump = 0.95, .leaf_form = HERB_LEAF_ROUND,
        .bloom_form = HERB_HEAD_CYME, .fruit_form = HERB_HEAD_NONE,
        .bloom_size = 0.07, .fruit_size = 0.035,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.height_scale = 1.15;
        profile.spread_scale = 0.76;
        profile.stems = 5;
        profile.leaf_width = 0.035;
        break;
    case HERB_SHAPE_B:
        profile.height_scale = 0.86;
        profile.spread_scale = 1.28;
        profile.stems = 7;
        profile.leaf_width = 0.053;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
