#include "../jun/jun_models.h"

void qlcg_plant_yao_lingzhi(LpBuilder *b, const PdDef *def, int season, int form) {
    static const MushroomShape shape = {
        .cap = "#823820", .rim = "#E6BC7E", .gills = "#DDB787",
        .stem = "#703B2C", .veil = "#793F2C",
        .crown = .25, .fan = 1, .width = 1.23, .stem_width = .21,
    };
    static const MushroomBody bodies[] = {
        {-.24, -.18, 1, 1.04, .04, 1, .30},
        {.13, -.90, .61, .79, -.63, 1.12, .24},
        {.27, .77, .49, .85, .52, .94, .25},
        {-.50, .33, .77, .66, 1.35, 1.08, .22},
    };
    qlcg_pa_build_mushrooms(b, def, season, form, &shape, bodies,
                             sizeof(bodies) / sizeof(bodies[0]));
}
