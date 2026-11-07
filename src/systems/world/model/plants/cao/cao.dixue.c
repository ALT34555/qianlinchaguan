#include "cao_model.h"

/* 雪线地衣：贴地裂片，春生子囊盘 */
void qlcg_plant_cao_dixue(LpBuilder *b, const PdDef *def, int season, int form) {
    HerbProfile profile = {
        .habit = HERB_CUSHION, .cold = 0, .heat = 4,
        .bloom_season = PD_SEASON_SPRING, .winter_leaf = 0.85,
        .bloom_form = HERB_HEAD_CUP,
        .cushion_lobes = 7, .plump = 1.0,
    };
    switch (qlcg_pa_herb_shape(def)) {
    case HERB_SHAPE_A:
        profile.spread_scale = 0.86;
        profile.cushion_lobes = 5;
        profile.plump = 1.52;
        break;
    case HERB_SHAPE_B:
        profile.spread_scale = 1.26;
        profile.cushion_lobes = 10;
        profile.plump = 0.63;
        break;
    }
    qlcg_pa_build_herb(b, def, season, form, &profile);
}
