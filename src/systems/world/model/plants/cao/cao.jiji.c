#include "cao_model.h"

/* 芨芨草：硬秆高台，冬秆不倒 */
void qlcg_plant_cao_jiji(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_TUSSOCK, .cold = 1, .heat = 2,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.82,
        .blades = 20, .leaf_len = 1.00, .leaf_width = 0.050,
        .arch = 1.35, .plump = 0.95, .leaf_form = HERB_LEAF_LINEAR,
        .bloom_form = HERB_HEAD_PANICLE, .fruit_form = HERB_HEAD_PANICLE,
        .bloom_size = 0.26, .fruit_size = 0.045,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.height_scale = 1.17;
        profile.spread_scale = 0.77;
        profile.blades = 17;
        profile.arch = 1.47;
        break;
    case HERB_SHAPE_B:
        profile.height_scale = 0.89;
        profile.spread_scale = 1.30;
        profile.blades = 24;
        profile.arch = 1.04;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
