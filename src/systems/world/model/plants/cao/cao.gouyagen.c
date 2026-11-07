#include "cao_model.h"

/* 狗牙根：贴地走茎，夏生指状穗 */
void qlcg_plant_cao_gouyagen(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_STOLON, .cold = 3, .heat = 1,
        .bloom_season = PD_SEASON_SUMMER, .winter_leaf = 0.30,
        .stems = 5, .leaf_len = 1.00, .leaf_width = 0.035,
        .arch = 0.90, .plump = 1.00, .leaf_form = HERB_LEAF_LINEAR,
        .bloom_form = HERB_HEAD_DIGITATE, .fruit_form = HERB_HEAD_DIGITATE,
        .bloom_size = 0.10, .fruit_size = 0.04,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.height_scale = 0.79;
        profile.spread_scale = 1.34;
        profile.stems = 4;
        profile.leaf_width = 0.029;
        break;
    case HERB_SHAPE_B:
        profile.height_scale = 1.19;
        profile.spread_scale = 0.82;
        profile.stems = 6;
        profile.leaf_width = 0.044;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
