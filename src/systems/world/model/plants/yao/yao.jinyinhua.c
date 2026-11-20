#include "yao_model.h"
#include "../teng/teng_model.h"
#include "../../plant_forms.h"

#include <math.h>

static void honeysuckle_bell(LpBuilder *b, const double base[3], double a, double size, LpRgb color) {
    enum { SIDES = 8 };
    double inner[SIDES][3], rim[SIDES][3];
    double x = cos(a), z = sin(a);
    LpRgb throat = qlcg_lp_mix(color, qlcg_lp_rgb(226, 158, 62), .56);
    for (int i = 0; i < SIDES; i++) {
        double phi = TAU * i / SIDES;
        double u = cos(phi), v = sin(phi);
        double lip = i % 2 == 0 ? .83 : .72;
        inner[i][0] = base[0] + size * (.57 * x - .21 * z * u - .12 * x * v);
        inner[i][1] = base[1] + size * (.41 + .17 * v);
        inner[i][2] = base[2] + size * (.57 * z + .21 * x * u - .12 * z * v);
        rim[i][0] = base[0] + size * (1.17 * x - lip * z * u - .56 * lip * x * v);
        rim[i][1] = base[1] + size * (.79 + .83 * lip * v);
        rim[i][2] = base[2] + size * (1.17 * z + lip * x * u - .56 * lip * z * v);
    }
    for (int i = 0; i < SIDES; i++) {
        int j = (i + 1) % SIDES;
        qlcg_lp_triangle(b, base, inner[i], inner[j], throat, LP_MAT_BLOOM);
        qlcg_lp_quad(b, inner[i], rim[i], rim[j], inner[j],
                    i % 2 == 0 ? color : qlcg_lp_lighten(color, .09), LP_MAT_BLOOM);
    }
}

static void honeysuckle_node(LpBuilder *b, const TengShoot *shoot, const double at[3], double a, int n) {
    if (shoot->bare) return;
    double len = shoot->span * (.22 + .015 * (n % 3));
    qlcg_pa_folded_petal(b, at, a, len, len * .54, .29, shoot->leaf, LP_MAT_SOLID);
    qlcg_pa_folded_petal(b, at, a + PI, len * .91, len * .52, .43, shoot->shade, LP_MAT_SOLID);
    if (n != 3 && n != 6) return;
    if (shoot->flowering) {
        LpRgb pale = qlcg_lp_rgb(255, 245, 216);
        double size = len * (n == 3 ? .71 : .82);
        for (int j = 0; j < 2; j++) {
            double dir = a + .83 + j * .87;
            double bud[3] = {at[0] + cos(dir) * size * .36, at[1] + size * (.31 + .18 * j),
                             at[2] + sin(dir) * size * .36};
            qlcg_pa_yao_twig(b, at, bud, size * .047, shoot->stem);
            honeysuckle_bell(b, bud, dir, size * (1 - .13 * j), j == 0 ? pale : shoot->bloom);
        }
    } else if (shoot->fruiting) {
        double r = len * .155;
        for (int j = 0; j < 2; j++) {
            double dir = a + .69 + j * 1.51;
            double fruit[3] = {at[0] + cos(dir) * r * 2.1, at[1] - r * 1.35,
                               at[2] + sin(dir) * r * 2.1};
            qlcg_pa_yao_twig(b, at, fruit, r * .15, shoot->stem);
            qlcg_pa_yao_berry(b, fruit, r, .92, shoot->fruit);
        }
    }
}

void qlcg_plant_yao_jinyinhua(LpBuilder *b, const PdDef *def, int season, int form) {
    TengHabit habit = {
        .canes = 4, .nodes = 7, .bloom_season = PD_SEASON_SUMMER, .twined_canes = 1,
        .thickness = .015, .draw_node = honeysuckle_node,
        .forms = {
            {.reach = 1, .arch = .64, .tip_height = .48, .bearing_step = GOLDEN, .bend = .46},
            {.reach = .80, .arch = .94, .tip_height = .55, .bearing_step = 1.47, .bend = .64},
            {.reach = 1.25, .arch = .43, .tip_height = .33, .bearing_step = GOLDEN, .bend = .31},
        },
    };
    if (form == VEG_FORM_SIMPLE) {
        habit.canes = 2;
        habit.nodes = 4;
    }
    qlcg_pa_vine(b, def, season, &habit);
}
