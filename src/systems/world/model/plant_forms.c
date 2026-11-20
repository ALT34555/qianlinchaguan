/* 植物几何入口实现 */
#include "plant_forms.h"
#include "plant_arch_internal.h"
#include "shore_models.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void simple_vine_leaf(LpBuilder *b, const double at[3], double a,
                             double length, double rise, LpRgb leaf) {
    double x = cos(a) * length, z = sin(a) * length;
    double mid_y = at[1] + rise * .42 - length * .13;
    double tip[3] = {at[0] + x, at[1] + rise, at[2] + z};
    double left[3] = {at[0] + .42*x + .34*z, mid_y, at[2] + .42*z - .34*x};
    double right[3] = {at[0] + .42*x - .34*z, mid_y, at[2] + .42*z + .34*x};
    qlcg_lp_quad(b, at, right, tip, left, leaf, LP_MAT_SOLID);
    qlcg_lp_quad(b, right, at, left, tip, qlcg_lp_deepen(leaf, .14), LP_MAT_SOLID);
}

/* 三段藤茎与四片叶 */
static void build_simple_vine(LpBuilder *b, double h, double r, int hanging,
                              LpRgb leaf, LpRgb bark) {
    static const double path[][3] = {{0, 0, 0}, {.31, .57, .15}, {.63, .75, -.16}, {1, .33, .12}};
    double nodes[4][3];
    for (int i = 0; i < 4; i++) {
        double reach = path[i][0], lift = path[i][1];
        nodes[i][0] = r * (hanging ? .31*reach + .35*lift : reach);
        nodes[i][1] = hanging ? -r * .92 * reach : h * lift;
        nodes[i][2] = r * path[i][2];
    }
    for (int i = 1; i < 4; i++)
        qlcg_pa_stem_between(b, nodes[i-1], nodes[i], r*(.024 - .005*i), .69, bark);
    double first[3] = {nodes[1][0]*.56, nodes[1][1]*.56, nodes[1][2]*.56};
    simple_vine_leaf(b, first, -1.26, r*.36, r*.035, leaf);
    for (int i = 1; i < 4; i++) {
        double length = r * (.48 - .055*i);
        simple_vine_leaf(b, nodes[i], i % 2 ? 1.11 : -1.17, length,
                         length*(hanging ? -.26 : .17), leaf);
    }
}

/* 六边菌盖与双段菌柄 */
static void build_simple_mushroom(LpBuilder *b, double h, double r, int hanging, int dry) {
    const double origin[3] = {0, 0, 0};
    const double cap[3] = {r * (hanging ? 1.19 : .11), h * (hanging ? .31 : .73), 0};
    const double bend[3] = {cap[0]*.66, hanging ? -r*.17 : h*.43, 0};
    const double underside[3] = {cap[0], cap[1] - r*.09, 0};
    LpRgb stem = qlcg_lp_hex("#e6d5b2", 230, 213, 178);
    LpRgb skin = qlcg_lp_hex(dry ? "#8b7760" : "#b97944", 185, 121, 68);
    LpRgb gills = qlcg_lp_deepen(stem, .08);
    LpOpts options = qlcg_lp_opts();
    options.rings = 1;
    options.floor_on = 0;
    const int sides = 6;
    double rx = r * .87, rz = r * .91;
    qlcg_pa_stem_between(b, origin, bend, r*.23, .79, stem);
    qlcg_pa_stem_between(b, bend, underside, r*.18, .83, stem);
    qlcg_lp_dome(b, cap, rx, r*.47, rz, sides, skin, &options);
    for (int i = 0; i < sides; i++) {
        double a = TAU*i/sides, next = TAU*(i+1)/sides;
        double p[3] = {cap[0] + rx*cos(a), cap[1], rz*sin(a)};
        double q[3] = {cap[0] + rx*cos(next), cap[1], rz*sin(next)};
        qlcg_lp_triangle(b, p, q, underside, gills, LP_MAT_SOLID);
    }
}

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
    case PLANT_CAT_TENG: {
        double spread = fmax(.03, pd_num(p, "spread", .9));
        if (def->state == PD_STATE_WITHERED)
            leaf = qlcg_pa_tone(def, "withered", 1);
        build_simple_vine(b, h, spread, def->state == PD_STATE_HANGING, leaf, bark);
        break;
    }
    case PLANT_CAT_JUN:
        build_simple_mushroom(b, h, fmax(.012, pd_num(p, "spread", .14)),
                              def->state == PD_STATE_HANGING, def->state == PD_STATE_WITHERED);
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

int qlcg_plant_build_source(const char *id, int season, int form, LpGeometry *out) {
    memset(out, 0, sizeof(*out));
    PdDef def;
    int force_normal = form == VEG_FORM_SIMPLE &&
                       !qlcg_pd_category_can_hang(qlcg_pd_category_from_id(id));
    if (!qlcg_pd_build_def_ex(id, season, force_normal, &def)) return 0;
    int shoreline = qlcg_shore_plant_index(def.id);
    if (shoreline >= 0) {
        int built = qlcg_shore_plant_geometry(shoreline, qlcg_shore_model_form(form), out);
        qlcg_lp_geometry_scale(out, def.scale);
        qlcg_pd_free_def(&def);
        return built;
    }

    int variant = qlcg_plant_model_variant(form);
    if (variant && qlcg_plant_category_has_variants(def.category))
        pd_set_num(&def.params, "shapeVariant", variant);

    LpBuilder b;
    qlcg_lp_init(&b);
    int species_mesh = def.category == PLANT_CAT_HUA || def.category == PLANT_CAT_NONG ||
                     def.category == PLANT_CAT_YAO;
    int built = form == VEG_FORM_SIMPLE && !species_mesh ? build_simple(&def, season, &b)
                    : qlcg_plant_species_build(&def, season,
                        form == VEG_FORM_SIMPLE ? VEG_FORM_SIMPLE : VEG_FORM_NORMAL, &b);
    if (!built) {
        qlcg_lp_free(&b);
        qlcg_pd_free_def(&def);
        return 0;
    }
    if (def.category == PLANT_CAT_SHU || def.category == PLANT_CAT_GUAN)
        b.cling_height = pd_num(&def.params, "height", 1.0);
    qlcg_lp_build(&b, out);
    if (def.state == PD_STATE_HANGING) out->height -= out->min_y;
    qlcg_lp_free(&b);
    qlcg_lp_geometry_scale(out, def.scale);
    qlcg_pd_free_def(&def);
    return out->vertex_count > 0;
}

static int published_or_source(const char *id, int season, int form, LpGeometry *out) {
    int result = qlcg_plant_read_release(id, season, form, out);
    return result ? result > 0 : qlcg_plant_build_source(id, season, form, out);
}

int qlcg_plant_build(const char *id, int season, int form, LpGeometry *out) {
    const char *suffix = id ? strchr(id, '@') : NULL;
    if (suffix) {
        int stage = qlcg_pd_state_of(suffix + 1);
        if (stage > PD_STATE_NORMAL && stage < PD_STATE_HANGING)
            season = PD_SEASON_NONE;
    }
    return published_or_source(id, season, form, out);
}

int qlcg_plant_build_growth(const char *id, int stage, int form, LpGeometry *out) {
    if (id && stage == PD_STATE_NORMAL) return published_or_source(id, PD_SEASON_NONE, form, out);
    char key[200];
    if (!id || stage < PD_STATE_SEEDLING || stage >= PD_STATE_HANGING ||
        snprintf(key, sizeof(key), "%s@%s", id, qlcg_pd_state_name(stage)) >= (int)sizeof(key)) {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    return published_or_source(key, PD_SEASON_NONE, form, out);
}
