/* 植物几何入口实现 */
#include "plant_forms.h"
#include "plant_arch_internal.h"

#include <math.h>
#include <string.h>

static int build_simple(const PdDef *def, int season, LpBuilder *b) {
    const PdDict *p = &def->params, *palette = &def->palette;
    double h = fmax(.1, pd_num(p, "height", pd_num(p, "trunk", 1)));
    double r = fmax(.05, pd_num(p, "canopy", pd_num(p, "spread", h * .3)));
    LpRgb leaf = qlcg_lp_hex(pd_str(palette, "foliage", pd_str(palette, "waterweed", "#50843b")), 80, 132, 59);
    LpRgb bark = qlcg_lp_hex(pd_str(palette, "bark", pd_str(palette, "barkLight", "#725338")), 114, 83, 56);
    leaf = qlcg_lp_wind_color(leaf, LP_WIND_LEAF);
    bark = qlcg_lp_wind_color(bark, LP_WIND_BARK);
    const double origin[3] = {0, 0, 0};

    switch (def->category) {
    case PLANT_CAT_SHU: {
        LpOpts canopy_opts = qlcg_lp_opts();
        canopy_opts.cap = 1;
        qlcg_lp_prism(b, origin, h * .04, h * .04, 0, h * .5, 6, bark, NULL);
        qlcg_lp_cone(b, (double[3]){0, h * .28, 0}, r, r, h * .72, 6, leaf, &canopy_opts);
        if (season == PD_SEASON_WINTER && pd_num(p, "snow", 0) > 0) {
            LpOpts snow_opts = qlcg_lp_opts();
            snow_opts.mat = LP_MAT_SNOW;
            LpRgb snow = qlcg_lp_hex(pd_str(palette, "snow", "#e4edf1"), 228, 237, 241);
            qlcg_lp_cone(b, (double[3]){0, h * .62, 0}, r * .54, r * .54, h * .39, 6, snow, &snow_opts);
        }
        break;
    }
    case PLANT_CAT_GUAN:
        qlcg_lp_prism(b, origin, h * .045, h * .045, 0, h * .3, 5, bark, NULL);
        for (int side = -1; side <= 1; side += 2) {
            qlcg_lp_limb(b, (double[3]){0, h * .15, 0}, (double[3]){side * .55, .8, 0},
                         h * .4, h * .025, bark, NULL);
        }
        qlcg_lp_dome(b, (double[3]){0, h * .55, 0}, r, h * .45, r, 5, leaf, NULL);
        break;
    case PLANT_CAT_CAO:
        qlcg_lp_cross_quad(b, origin, r * 2, h, leaf, NULL);
        break;
    case PLANT_CAT_HUA: {
        LpRgb flower = qlcg_lp_hex(pd_str(palette, "bloom", "#cf7595"), 207, 117, 149);
        double y = h * .9, radius = fmax(.06, fmin(r, h * .25));
        qlcg_lp_prism(b, origin, h * .02, h * .02, 0, y, 3, leaf, NULL);
        for (int i = 0; i < 10; i++) {
            double a = i * TAU / 10, c = (i + 1) * TAU / 10;
            double ra = radius * (i % 2 ? .45 : 1), rc = radius * (i % 2 ? 1 : .45);
            qlcg_lp_triangle(b, (double[3]){0, y, 0},
                             (double[3]){cos(c) * rc, y, sin(c) * rc},
                             (double[3]){cos(a) * ra, y, sin(a) * ra}, flower, LP_MAT_BLOOM);
        }
        break;
    }
    case PLANT_CAT_SHUI: {
        double w = fmin(r * .15, h * .08), bend = r * .4;
        for (int i = 0; i < 3; i++) {
            double a = i * TAU / 3, x = cos(a) * r * .3, z = sin(a) * r * .3;
            double dx = cos(a) * w, dz = sin(a) * w;
            double mid_left[3] = {x - dx + bend, h * .5, z - dz};
            double mid_right[3] = {x + dx + bend, h * .5, z + dz};
            qlcg_lp_quad(b, (double[3]){x - dx, 0, z - dz}, (double[3]){x + dx, 0, z + dz},
                         mid_right, mid_left, leaf, LP_MAT_SOLID);
            qlcg_lp_quad(b, mid_left, mid_right, (double[3]){x + dx, h, z + dz},
                         (double[3]){x - dx, h, z - dz}, leaf, LP_MAT_SOLID);
        }
        break;
    }
    case PLANT_CAT_NONG: {
        double w = fmin(r * .15, h * .08);
        for (int i = 0; i < 7; i++) {
            double a = i * TAU / 7, x = cos(a) * r * .3, z = sin(a) * r * .3;
            double dx = cos(a) * w, dz = sin(a) * w;
            qlcg_lp_triangle(b, (double[3]){x - dx, 0, z - dz}, (double[3]){x + dx, 0, z + dz},
                             (double[3]){x * 1.6, h * (.7 + i % 3 * .15), z * 1.6}, leaf, LP_MAT_SOLID);
        }
        break;
    }
    default:
        return 0;
    }
    return 1;
}

int qlcg_plant_build(const char *id, int season, int form, LpGeometry *out) {
    memset(out, 0, sizeof(*out));
    PdDef def;
    int force_normal = (form == VEG_FORM_SIMPLE) ? 1 : 0;
    if (!qlcg_pd_build_def_ex(id, season, force_normal, &def)) return 0;

    int variant = qlcg_plant_model_variant(form);
    if (variant && qlcg_plant_category_has_variants(def.category))
        pd_set_num(&def.params, "shapeVariant", variant);

    LpBuilder b;
    qlcg_lp_init(&b);
    int cultivated = def.category == PLANT_CAT_HUA || def.category == PLANT_CAT_NONG;
    int built = form == VEG_FORM_SIMPLE && !cultivated ? build_simple(&def, season, &b)
                    : qlcg_plant_species_build(&def, season,
                        form == VEG_FORM_SIMPLE ? VEG_FORM_SIMPLE : VEG_FORM_NORMAL, &b);
    if (!built) {
        qlcg_lp_free(&b);
        qlcg_pd_free_def(&def);
        return 0;
    }
    qlcg_lp_build(&b, out);
    qlcg_lp_free(&b);
    qlcg_lp_geometry_scale(out, def.scale);
    qlcg_pd_free_def(&def);
    return out->vertex_count > 0;
}
