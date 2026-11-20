#include "jun_models.h"
#include "../../plant_forms.h"

#include <math.h>

enum { CAP_SECTORS = 14, CAP_RINGS = 3 };

typedef struct {
    double origin[3], radius, yaw, tilt, phase;
    double crown;
    int sides, dry;
} MushroomCap;

static void cap_point(const MushroomShape *shape, const MushroomCap *cap,
                      double a, double t, double y, double out[3]) {
    double c = cos(a);
    double wave = 1 + .044 * sin(3*a + cap->phase) + shape->fan*.065*cos(5*a);
    double x = cap->radius * t * (shape->fan*.56 + (1-shape->fan*.36)*c*wave);
    double z = cap->radius * t * shape->width * sin(a)*wave;
    double ca = cos(cap->yaw), sa = sin(cap->yaw);
    out[0] = cap->origin[0] + ca*x - sa*z;
    out[1] = cap->origin[1] + cap->radius*y + x*cap->tilt;
    out[2] = cap->origin[2] + sa*x + ca*z;
}

static LpRgb cap_color(const char *hex, int season, int dry) {
    static const double season_shift[] = {.035, 0, .045, .10};
    LpRgb color = qlcg_lp_hex(hex, 240, 229, 210);
    LpRgb straw = qlcg_lp_hex("#ad8e6d", 173, 142, 109);
    return qlcg_lp_mix(color, straw, dry ? .69 : season == PD_SEASON_NONE ? 0 : season_shift[season]);
}

/* 菌盖、盖缘和放射菌褶 */
static void draw_cap(LpBuilder *b, const MushroomShape *shape, const MushroomCap *cap, int season) {
    static const double radii[CAP_RINGS] = {.36, .73, 1};
    static const double elevations[CAP_RINGS] = {.94, .65, 0};
    LpRgb skin = cap_color(shape->cap, season, cap->dry);
    LpRgb rim = cap_color(shape->rim, season, cap->dry);
    LpRgb gill = cap_color(shape->gills, season, cap->dry);
    int sides = cap->sides;
    double crown[CAP_RINGS][CAP_SECTORS][3], lip[CAP_SECTORS][3];
    double peak[3], throat[3];
    cap_point(shape, cap, 0, 0, cap->crown, peak);
    cap_point(shape, cap, 0, 0, -.10 - .17*shape->fan, throat);
    for (int i = 0; i < sides; i++) {
        double a = TAU*i/sides;
        double curl = shape->fan * .07 * cos(2*a + .6);
        for (int k = 0; k < CAP_RINGS; k++)
            cap_point(shape, cap, a, radii[k],
                      cap->crown*elevations[k] + radii[k]*curl, crown[k][i]);
        cap_point(shape, cap, a, .98, curl - .105, lip[i]);
    }
    for (int i = 0; i < sides; i++) {
        int next = (i+1)%sides;
        LpRgb panel = i%3 == 0 ? qlcg_lp_lighten(skin, .07) : skin;
        qlcg_lp_triangle(b, peak, crown[0][next], crown[0][i], panel, LP_MAT_SOLID);
        for (int k = 1; k < CAP_RINGS; k++) {
            LpRgb shade = qlcg_lp_mix(panel, rim, k == 1 ? .23 : .68);
            qlcg_lp_quad(b, crown[k-1][i], crown[k-1][next], crown[k][next], crown[k][i], shade, LP_MAT_SOLID);
        }
        qlcg_lp_quad(b, crown[2][i], crown[2][next], lip[next], lip[i], rim, LP_MAT_SOLID);

        if (i % 2) {
            qlcg_lp_triangle(b, throat, lip[i], lip[next], qlcg_lp_deepen(gill, .07), LP_MAT_SOLID);
            continue;
        }
        double fold[3];
        for (int axis = 0; axis < 3; axis++) {
            double edge = (lip[i][axis] + lip[next][axis])*.5;
            fold[axis] = throat[axis] + (edge - throat[axis])*.86;
        }
        fold[1] -= cap->radius*.052;
        qlcg_lp_triangle(b, throat, lip[i], fold, gill, LP_MAT_SOLID);
        qlcg_lp_triangle(b, throat, fold, lip[next], qlcg_lp_deepen(gill, .14), LP_MAT_SOLID);
        qlcg_lp_triangle(b, fold, lip[i], lip[next], gill, LP_MAT_SOLID);
    }
    if (!shape->spots) return;
    LpRgb spots = cap_color(shape->spots, season, cap->dry);
    LpOpts raised_spot = qlcg_lp_opts();
    raised_spot.floor_on = 0;
    for (int i = 0; i < sides; i += 2) {
        int row = (i%4 == 0);
        double *surface = crown[row][i];
        double size = cap->radius*shape->spot_size*(.75 + .07*(i%5));
        double center[3] = {surface[0], surface[1]-size*.15, surface[2]};
        qlcg_lp_dome(b, center, size, size*.46, size*.95, 7, spots, &raised_spot);
    }
    double size = cap->radius*shape->spot_size*.76;
    qlcg_lp_dome(b, peak, size, size*.31, size, 7, spots, &raised_spot);
}

/* 折边菌裙贴合上段菌柄 */
static void draw_skirt(LpBuilder *b, const double bend[3], const double top[3],
                       double r, double flare, LpRgb color, int sides) {
    double upper[CAP_SECTORS][3], hem[CAP_SECTORS][3];
    for (int i = 0; i < sides; i++) {
        double a = TAU*i/sides;
        double ca = cos(a), sa = sin(a), fold = (i % 2 ? .89 : 1.10);
        for (int axis = 0; axis < 3; axis++) {
            upper[i][axis] = bend[axis] + (top[axis]-bend[axis])*.62;
            hem[i][axis] = bend[axis] + (top[axis]-bend[axis])*.41;
        }
        upper[i][0] += r*.89*ca;
        upper[i][2] += r*.89*sa;
        hem[i][0] += (r+flare*fold)*ca;
        hem[i][1] -= flare*(.13 + .47*fold);
        hem[i][2] += (r+flare*fold)*sa;
    }
    for (int i = 0; i < sides; i++) {
        int next = (i+1)%sides;
        LpRgb shade = i%2 ? qlcg_lp_deepen(color, .10) : color;
        qlcg_lp_quad(b, upper[i], upper[next], hem[next], hem[i], shade, LP_MAT_SOLID);
    }
}

static int cap_sectors(double size) {
    if (size >= .76) return CAP_SECTORS;
    if (size >= .62) return 12;
    if (size >= .49) return 10;
    return 8;
}

void qlcg_pa_build_mushrooms(LpBuilder *b, const PdDef *def, int season, int form,
                             const MushroomShape *shape, const MushroomBody *bodies, int count) {
    const unsigned variant = qlcg_pa_shape_variant(def);
    const int hanging = def->state == PD_STATE_HANGING;
    const int young = def->state == PD_STATE_SEEDLING;
    const int dry = def->state == PD_STATE_WITHERED;
    const int simple = form == VEG_FORM_SIMPLE;
    const double height = fmax(.015, qlcg_pa_pnum(def, "height", .22)) * (variant == 1 ? 1.17 : variant == 2 ? .89 : 1);
    const double spread = fmax(.012, qlcg_pa_pnum(def, "spread", .14)) * (variant == 1 ? .87 : variant == 2 ? 1.16 : 1);
    const double yaw = variant == 1 ? .36 : variant == 2 ? -.30 : 0;
    const LpRgb stem = cap_color(shape->stem, season, dry);
    const LpRgb veil = cap_color(shape->veil, season, dry);
    if ((young || simple) && count > 2) count = 2;
    for (int i = 0; i < count; i++) {
        const MushroomBody *body = &bodies[i];
        double r = spread * body->size * (young ? .86 : 1);
        double h = height * body->height;
        double base[3] = {spread*(body->x*cos(yaw)-body->z*sin(yaw)),
                          variant ? r*shape->stem_width*.14 : 0,
                          spread*(body->x*sin(yaw)+body->z*cos(yaw))};
        double face = body->angle + yaw;
        double dx = r * (variant ? body->lean*cos(face) : .15);
        double dz = r * (variant ? body->lean*sin(face) : .04);
        double bend[3] = {base[0]+dx*.34, h*.56, base[2]+dz*.34-r*.10};
        MushroomCap cap = {{base[0]+dx, h, base[2]+dz}, r, face,
                           .10, i*.61+variant*.73, shape->crown*body->crown*(young ? 1.20 : dry ? .67 : 1),
                           simple ? 6 : cap_sectors(body->size), dry};
        if (hanging) {
            base[0] = base[1] = base[2] = 0;
            cap.origin[0] = spread*(.58 + body->x*.46 + body->size*(shape->fan > 0 ? .27 : 1.04)
                                    + body->lean*.20);
            cap.origin[1] = height*(body->height-.51)*.91;
            cap.origin[2] = spread*(body->z*.89 + variant*.033*(i%2 ? 1 : -1));
            bend[0] = cap.origin[0]*.68;
            bend[1] = cap.origin[1] - h*.33;
            bend[2] = cap.origin[2]*.78;
            cap.yaw = .36*body->z + (variant ? .29 : .10)*body->angle;
            cap.tilt = shape->fan > 0 ? .13 : -.27 - .15*(body->lean-.15);
        }
        double neck[3] = {cap.origin[0],cap.origin[1]+r*.06,cap.origin[2]};
        double stem_r = r * shape->stem_width;
        qlcg_pa_stem_between(b, base, bend, stem_r*(shape->bulb ? 1.75 : 1.18), .72, stem);
        qlcg_pa_stem_between(b, bend, neck, stem_r, .80, stem);
        if (shape->bulb) {
            double bulb[3] = {base[0] + bend[0]*.14 - base[0]*.14,
                              base[1] + stem_r*.52,
                              base[2] + (bend[2]-base[2])*.14};
            qlcg_lp_ico(b, bulb, (double[3]){stem_r*1.55,stem_r*.75,stem_r*1.46}, 7, veil, NULL);
        }
        if (shape->skirt > 0 && !young)
            draw_skirt(b, bend, neck, stem_r, r*shape->skirt, veil, shape->bulb ? 8 : 6);
        draw_cap(b, shape, &cap, season);
    }
}

void qlcg_plant_jun_pinggu(LpBuilder *b, const PdDef *def, int season, int form) {
    static const MushroomShape shape = {"#5d697e","#a5adbd","#e3dfd6","#c8bfb1","#e3dfd6",NULL,
                                        .27, 1, 1.00, .22, 0, 0, 0};
    static const MushroomBody bodies[3][4] = {
        {{.06,-.07,1,1.02,.17,1,.15}, {.24,-.77,.62,.82,-.56,1,.15},
         {.40,.85,.71,.91,.60,1,.15}, {-.58,.31,.42,.68,2.19,1,.15}},
        {{-.45,.10,1.22,1.02,.06,1.03,.23}, {.06,-.18,.89,.91,-.29,1.08,.22},
         {.43,.23,.54,.87,.17,.91,.30}, {.79,-.29,.32,.61,-.31,.86,.25}},
        {{.03,.07,.94,.91,.02,.81,.22}, {-.22,-1.16,.48,.88,-.95,.80,.18},
         {.14,1.07,.54,.92,.84,.83,.27}, {.65,-.05,.28,.71,.12,.69,.36}}
    };
    qlcg_pa_build_mushrooms(b, def, season, form, &shape, bodies[qlcg_pa_shape_variant(def)], 4);
}

void qlcg_plant_jun_koumo(LpBuilder *b, const PdDef *def, int season, int form) {
    static const MushroomShape shape = {"#eadfc7","#fff0d5","#c3b39d","#e5d7bd","#e5d7bd",NULL,
                                        .57, 0, .95, .35, 0, 0, 0};
    static const MushroomBody bodies[3][3] = {
        {{0,0,1,.77,.27,1,.15}, {.70,1.12,.57,.52,.83,1,.15}, {-.83,-.94,.75,.61,1.46,1,.15}},
        {{-.30,-.23,1.27,.79,.30,1.19,.12}, {.65,.48,.51,.57,.91,1.13,.11},
         {.25,-.97,.32,.43,-.45,1.27,.12}},
        {{-.23,-.65,.68,.77,-.20,.72,.15}, {.75,.01,.51,.67,.75,.70,.21},
         {-.36,1.03,.59,.70,2.15,.76,.18}}
    };
    qlcg_pa_build_mushrooms(b, def, season, form, &shape, bodies[qlcg_pa_shape_variant(def)], 3);
}

void qlcg_plant_jun_chashugu(LpBuilder *b, const PdDef *def, int season, int form) {
    static const MushroomShape shape = {"#7b351d","#c08951","#ab8565","#dbbd8d","#ecd8ad",NULL,
                                        .44, 0, .96, .19, .22, 0, 0};
    static const MushroomBody bodies[3][6] = {
        {{-.44,.10,1,.55,.2,1,.15}, {.17,-.64,.83,.52,-.2,1,.15}, {.24,.77,.71,.43,.3,1,.15},
         {-.60,-1.10,.59,.40,.5,1,.15}, {-.31,1.29,.84,.41,.7,1,.15}, {.76,.01,.51,.33,1.3,1,.15}},
        {{-.38,.02,1.33,.55,.13,1.10,.09}, {-.10,-.55,1.16,.51,-.60,1.05,.12},
         {.22,.51,.99,.43,.18,1.12,.12}, {-.44,-.80,.81,.40,-.40,1.14,.11},
         {.02,.90,.77,.40,.72,1.09,.13}, {.62,.06,.59,.35,.50,1.18,.10}},
        {{-.19,.05,1.04,.55,-.26,.84,.31}, {.12,-1.04,.76,.52,-1.20,.87,.60},
         {.42,.91,.78,.47,1.08,.80,.64}, {-.36,-1.55,.54,.42,-1.73,.82,.46},
         {-.28,1.49,.56,.41,1.84,.83,.60}, {.92,.06,.52,.40,.52,.89,.57}}
    };
    qlcg_pa_build_mushrooms(b, def, season, form, &shape, bodies[qlcg_pa_shape_variant(def)], 6);
}

void qlcg_plant_jun_jinhuangegao(LpBuilder *b, const PdDef *def, int season, int form) {
    static const MushroomShape shape = {"#d69419","#f7cb51","#f3e2a8","#dbb76c","#f1d28d","#f9de81",
                                        .44, 0, .99, .17, .29, .057, 1};
    static const MushroomBody bodies[3][3] = {
        {{-.30,0,1,.83,.3,1,.15}, {.50,1.15,.63,.59,.53,1,.15}, {.21,-.98,.45,.38,1.31,1,.15}},
        {{-.37,-.13,1.26,.87,.10,1.18,.12}, {.59,.73,.55,.48,.86,1.23,.24},
         {.23,-.88,.34,.36,-.78,1.31,.19}},
        {{-.36,-.62,.86,.79,-.61,.73,.24}, {.49,.63,.72,.75,.70,.76,.44},
         {1.00,-.22,.37,.40,.35,1.03,.23}}
    };
    qlcg_pa_build_mushrooms(b, def, season, form, &shape, bodies[qlcg_pa_shape_variant(def)], 3);
}

void qlcg_plant_jun_dumogu(LpBuilder *b, const PdDef *def, int season, int form) {
    static const MushroomShape shape = {"#682cb0","#a061d1","#d9bfd9","#e3cce8","#fff3eb","#fffdf7",
                                        .69, 0, 1.02, .22, .48, .137, 1};
    static const MushroomBody bodies[3][3] = {
        {{-.34,0,1,.88,.13,1,.15}, {.37,1.26,.55,.57,.76,1,.15}, {.67,-1.24,.68,.64,.29,1,.15}},
        {{-.49,.04,1.23,.96,.21,1.16,.11}, {.28,1.03,.43,.53,1.01,1.14,.25},
         {.64,-.88,.31,.43,-.65,1.18,.25}},
        {{-.37,-.82,.78,.83,-.51,.82,.28}, {.40,.83,.84,.82,.46,.76,.51},
         {.86,-.05,.32,.44,.11,.95,.37}}
    };
    qlcg_pa_build_mushrooms(b, def, season, form, &shape, bodies[qlcg_pa_shape_variant(def)], 3);
}
