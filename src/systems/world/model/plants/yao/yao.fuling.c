#include "../jun/jun_models.h"

void qlcg_plant_yao_fuling(LpBuilder *b, const PdDef *def, int season, int form) {
    static const MushroomShape shape = {
        .cap = "#CCAE82", .rim = "#F1DFC1", .gills = "#EEE0C9",
        .stem = "#E7D7B7", .veil = "#D7C2A4",
        .crown = .58, .width = .98, .stem_width = .32, .bulb = 1,
    };
    static const MushroomBody bodies[] = {
        {-.16, -.15, 1, .94, .43, 1, .15},
        {.87, -.66, .66, .65, .64, 1.08, .15},
        {.21, 1.24, .53, .69, 1.21, .91, .17},
    };
    qlcg_pa_build_mushrooms(b, def, season, form, &shape, bodies,
                             sizeof(bodies) / sizeof(bodies[0]));
}
