/* 岩石原型解释器实现 */
#include "rock_arch.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TAU 6.283185307179586476925286766559
#define PI  3.14159265358979323846

/* 原型内部状态 */
typedef struct {
    double diameter;
    double height;
    int    sides;
    double jitter;
    LpRng  rng;      /* 布局序列，与 builder 内的抖色序列独立同源 */
} RockRes;

typedef struct { LpRgb body, band, cap, base; } RockTones;

typedef struct {
    LpRgb color;
    int   mat;
    double amount;
    double slope;
    double blend;
    int   has_color;
} RockPlan;

static double pnum(const RockBuildDef *d, const char *k, double dflt) {
    double v = pd_num(&d->params, k, NAN);
    return isnan(v) ? dflt : v;
}

static int js_round(double v) { return (int)floor(v + 0.5); }

static LpRgb slot_color(const RockBuildDef *d, const char *slot) {
    const char *hex = pd_str(&d->palette, slot, NULL);
    if (!hex) return qlcg_lp_rgb(127, 127, 127);
    return qlcg_lp_hex(hex, 127, 127, 127);
}

static void tones_of(const RockBuildDef *d, RockTones *t) {
    t->body = slot_color(d, "body");
    t->band = slot_color(d, "band");
    t->cap = slot_color(d, "cap");
    t->base = slot_color(d, "base");
}

static void crust_plan(const RockBuildDef *d, RockPlan *p) {
    memset(p, 0, sizeof(*p));
    if (d->crust_material == ROCK_MAT_SNOW) return;
    double fallback = (d->crust_material >= 0) ? 0.75 : 0.16;
    p->amount = qlcg_lp_clamp01(pnum(d, "crust", fallback));
    p->slope = pnum(d, "crustSlope", 0.42);
    if (d->crust_material >= 0 && d->has_crust_color) {
        p->color = d->crust_rgb;
        p->mat = d->crust_material;
        p->has_color = 1;
        p->blend = (d->crust_material == ROCK_MAT_MOSS) ? 0.2 : 0.0;
    } else {
        p->mat = ROCK_MAT_SOLID;
        p->amount = 0.0;
        p->blend = 0.0;
    }
}

static void crust_args(const RockPlan *p, RockCrust *out, int *has) {
    memset(out, 0, sizeof(*out));
    *has = 0;
    if (!p->has_color || p->amount <= 0.0) return;
    out->active = 1;
    out->mat = p->mat;
    out->amount = p->amount;
    out->slope = p->slope;
    out->color = p->color;
    out->seed = 0x51ed2701u;
    *has = 1;
}

static int wants_stack(const RockBuildDef *d) {
    return d->form == ROCK_FORM_STACKED;
}

static void resolve(const RockBuildDef *d, RockRes *r) {
    double diameter = pnum(d, "diameter", 2.0);
    double aspect = pnum(d, "aspect", 0.62);
    double height = pnum(d, "height", diameter * aspect);
    double size_scale = pnum(d, "sizeScale", 1.0);
    double spread_scale = pnum(d, "spreadScale", 1.0);
    r->diameter = diameter * size_scale * spread_scale;
    r->height = height * size_scale;
    double sides = pnum(d, "sides", 6.0);
    r->sides = js_round(sides) < 3 ? 3 : js_round(sides);
    r->jitter = qlcg_lp_clamp01(pnum(d, "jitter", 0.18));
    qlcg_lp_rng_seed(&r->rng, (uint32_t)(d->build_seed ^ d->shape_seed));
}

static double rr(RockRes *r) { return qlcg_lp_rng_next(&r->rng); }

static void shell_defaults(RockShellOpts *o) {
    memset(o, 0, sizeof(*o));
    o->cap_color[0] = o->cap_color[1] = o->cap_color[2] = NAN;
    o->bottom_color[0] = o->bottom_color[1] = o->bottom_color[2] = NAN;
    o->side_color[0] = o->side_color[1] = o->side_color[2] = NAN;
    o->cap = NAN;
    o->bottom_cap = NAN;
    o->mat = -1;
    o->wobble_amount = 0.0;
}

static void column_defaults(RockColumnOpts *o) {
    memset(o, 0, sizeof(*o));
    o->skew = NAN;
    o->twist = NAN;
    o->tip_color[0] = o->tip_color[1] = o->tip_color[2] = NAN;
    o->mat = -1;
}

static void blob_defaults(RockBlobOpts *o) {
    memset(o, 0, sizeof(*o));
    o->cap_color[0] = o->cap_color[1] = o->cap_color[2] = NAN;
    o->rings = NAN;
    o->mat = -1;
}

/* 环半径抖动表 */
static void make_ripple(RockRes *r, int sides, int layers, double *out) {
    for (int l = 0; l < layers; l++)
        for (int i = 0; i < sides; i++)
            out[l * sides + i] = 0.78 + rr(r) * 0.38;
}

/* 环沿法线位移表 */
static void make_wobble(RockRes *r, int sides, int layers, double *out) {
    for (int l = 0; l < layers; l++)
        for (int i = 0; i < sides; i++)
            out[l * sides + i] = (rr(r) * 2.0 - 1.0) * 0.9;
}


/* 试算主石顶面高：按 outcrop 独立重建 */
static double main_span(const RockBuildDef *def);

/* 叠置档的小石 */
static void stack_core(RockBuilder *b, const RockBuildDef *def, RockRes *r,
                       const RockTones *t, const RockPlan *plan) {
    double ratio = pnum(def, "stackRatio", 0.42);
    double base_diameter = pnum(def, "diameter", 2.0);
    double size_scale = pnum(def, "sizeScale", 1.0);
    double spread_scale = pnum(def, "spreadScale", 1.0);
    double rad = (base_diameter * size_scale * spread_scale * ratio) / 2.0;
    double off = pnum(def, "stackOffset", 0.3) * base_diameter;
    double y = main_span(def);

    RockShellOpts o;
    shell_defaults(&o);
    o.center[0] = off;
    o.center[1] = y - rad * 0.15;
    o.center[2] = off * 0.5;
    o.rx = rad;
    o.rz = rad * (0.8 + rr(r) * 0.35);
    o.thickness = rad * 1.5;
    int sides2 = r->sides - 1;
    if (sides2 < 4) sides2 = 4;
    o.sides = (double)sides2;
    o.normal[0] = sin(rr(r) * 0.5 - 0.25);
    o.normal[1] = 1.0;
    o.normal[2] = sin(rr(r) * 0.5 - 0.25);
    o.color = qlcg_lp_mix(t->body, t->band, 0.25);
    LpRgb capm = plan->has_color ? qlcg_lp_mix(t->cap, plan->color, plan->blend) : t->cap;
    o.cap_color[0] = capm.r; o.cap_color[1] = capm.g; o.cap_color[2] = capm.b;
    LpRgb bc = qlcg_lp_deepen(t->base, 0.5);
    o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
    o.jitter = pnum(def, "jitter", 0.18) * 1.2;
    o.rot_y = rr(r) * TAU;
    o.bottom_cap = 0;
    RockCrust crust; int has_crust;
    crust_args(plan, &crust, &has_crust);
    o.crust = crust;
    o.has_crust = has_crust ? 1 : 0;
    qlcg_rock_shell(b, &o);
}

/* ---------- 巨砾 ---------- */

static void arch_boulder(RockBuilder *b, const RockBuildDef *def) {
    RockRes r;
    resolve(def, &r);
    RockTones t;
    tones_of(def, &t);
    RockPlan plan;
    crust_plan(def, &plan);

    int shells = js_round(pnum(def, "shells", 5.0));
    if (shells < 1) shells = 1;
    double taper = pnum(def, "shellTaper", 0.85);
    double skew = pnum(def, "shellSkew", 0.11);
    double twist = pnum(def, "shellTwist", 0.5);
    double flatten = pnum(def, "baseFlatten", 0.95);
    double tilt_x = pnum(def, "tiltX", 0.0);
    double tilt_z = pnum(def, "tiltZ", 0.0);
    double normal[3] = { sin(tilt_z), cos(tilt_z) * cos(tilt_x), sin(tilt_x) };
    double sink = pnum(def, "sink", 0.16) * r.height;
    double span = r.height * flatten - sink;
    if (span < r.height * 0.12) span = r.height * 0.12;

    double *steps = (double *)malloc((size_t)shells * sizeof(double));
    int rl = shells + 2;
    double *ripple = (double *)malloc((size_t)(rl * r.sides) * sizeof(double));
    double *wobble = (double *)malloc((size_t)(rl * r.sides) * sizeof(double));
    if (!steps || !ripple || !wobble) { free(steps); free(ripple); free(wobble); return; }

    double step_sum = 0.0;
    for (int s = 0; s < shells; s++) { steps[s] = 0.6 + rr(&r) * 0.8; step_sum += steps[s]; }
    make_ripple(&r, r.sides, rl, ripple);
    make_wobble(&r, r.sides, rl, wobble);
    double base_phase = rr(&r) * TAU;

    double rx = r.diameter / 2.0;
    double rz = rx * (0.82 + rr(&r) * 0.3);
    double y = -sink;
    RockCrust crust; int has_crust;
    crust_args(&plan, &crust, &has_crust);

    for (int s = 0; s < shells; s++) {
        double k = pow(taper, s);
        double h = (span * steps[s]) / step_sum;
        double cx = (rr(&r) * 2.0 - 1.0) * skew * rx;
        double cz = (rr(&r) * 2.0 - 1.0) * skew * rz;

        RockShellOpts o;
        shell_defaults(&o);
        o.center[0] = cx; o.center[1] = y + h / 2.0; o.center[2] = cz;
        o.rx = rx * k;
        o.rz = rz * k;
        o.thickness = h * 1.08;
        o.sides = (double)r.sides;
        o.normal[0] = normal[0]; o.normal[1] = normal[1]; o.normal[2] = normal[2];
        double blend = s * 0.08;
        if (blend > 0.5) blend = 0.5;
        o.color = qlcg_lp_mix(t.body, t.band, blend);
        o.cap_color[0] = t.cap.r; o.cap_color[1] = t.cap.g; o.cap_color[2] = t.cap.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.42);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter;
        o.radius_scale = ripple + (size_t)s * (size_t)r.sides;
        o.wobble = wobble + (size_t)s * (size_t)r.sides;
        o.wobble_amount = 0.5;
        o.rot_y = base_phase + (double)s * twist;
        o.crust = crust;
        o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_shell(b, &o);

        y += h * 0.93;
        rx *= taper;
        rz *= taper;
    }
    if (wants_stack(def) && shells <= 3) stack_core(b, def, &r, &t, &plan);
    free(steps);
    free(ripple);
    free(wobble);
}

/* ---------- 岩块 ---------- */

static void arch_block(RockBuilder *b, const RockBuildDef *def) {
    RockRes r;
    resolve(def, &r);
    RockTones t;
    tones_of(def, &t);
    RockPlan plan;
    crust_plan(def, &plan);

    int shells = js_round(pnum(def, "shells", 4.0));
    if (shells < 1) shells = 1;
    double taper = pnum(def, "shellTaper", 0.9);
    double skew = pnum(def, "shellSkew", 0.12);
    double twist = pnum(def, "shellTwist", 0.7);
    double tilt_x = pnum(def, "tiltX", 0.06);
    double tilt_z = pnum(def, "tiltZ", -0.05);
    double normal[3] = { sin(tilt_z), cos(tilt_z) * cos(tilt_x), sin(tilt_x) };
    double sink = pnum(def, "sink", 0.1) * r.height;
    double layer_h = r.height / ((double)shells * 0.86);

    int rl = shells + 2;
    double *ripple = (double *)malloc((size_t)(rl * r.sides) * sizeof(double));
    double *wobble = (double *)malloc((size_t)(rl * r.sides) * sizeof(double));
    if (!ripple || !wobble) { free(ripple); free(wobble); return; }
    make_ripple(&r, r.sides, rl, ripple);
    make_wobble(&r, r.sides, rl, wobble);

    double y = -sink;
    double rx = r.diameter / 2.0;
    double rz = rx * (0.72 + rr(&r) * 0.3);
    RockCrust crust; int has_crust;
    crust_args(&plan, &crust, &has_crust);
    double rot_base = pnum(def, "rotY", 0.0);

    for (int s = 0; s < shells; s++) {
        double k = pow(taper, s);
        double cx = (rr(&r) * 2.0 - 1.0) * skew * rx;
        double cz = (rr(&r) * 2.0 - 1.0) * skew * rz;

        RockShellOpts o;
        shell_defaults(&o);
        o.center[0] = cx; o.center[1] = y + layer_h / 2.0; o.center[2] = cz;
        o.rx = rx * k;
        o.rz = rz * k;
        o.thickness = layer_h * 1.02;
        o.sides = (double)r.sides;
        o.normal[0] = normal[0]; o.normal[1] = normal[1]; o.normal[2] = normal[2];
        o.color = (s % 2 == 1) ? qlcg_lp_mix(t.body, t.band, 0.35) : t.body;
        o.cap_color[0] = t.cap.r; o.cap_color[1] = t.cap.g; o.cap_color[2] = t.cap.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.45);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter * 0.55;
        o.radius_scale = ripple + (size_t)s * (size_t)r.sides;
        o.wobble = wobble + (size_t)s * (size_t)r.sides;
        o.wobble_amount = 0.45;
        o.rot_y = (double)s * twist + rot_base;
        o.crust = crust;
        o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_shell(b, &o);

        y += layer_h * 0.82;
        rx *= taper;
        rz *= taper;
    }

    /* 一道外凸的棱 */
    {
        RockShellOpts o;
        shell_defaults(&o);
        o.center[0] = 0; o.center[1] = r.height * 0.5; o.center[2] = 0;
        o.rx = r.diameter * 0.44;
        o.rz = r.diameter * 0.3;
        o.thickness = r.height * 0.1;
        int sides2 = r.sides - 1;
        if (sides2 < 4) sides2 = 4;
        o.sides = (double)sides2;
        o.normal[0] = 0.24; o.normal[1] = 0.96; o.normal[2] = -0.14;
        o.color = qlcg_lp_deepen(t.band, 0.1);
        o.cap_color[0] = t.cap.r; o.cap_color[1] = t.cap.g; o.cap_color[2] = t.cap.b;
        LpRgb bc = qlcg_lp_deepen(t.band, 0.4);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter * 0.4;
        o.rot_y = rr(&r) * TAU;
        o.crust = crust;
        o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_shell(b, &o);
    }
    if (wants_stack(def)) stack_core(b, def, &r, &t, &plan);
    free(ripple);
    free(wobble);
}

/* ---------- 碎石堆 ---------- */

static void arch_rubble(RockBuilder *b, const RockBuildDef *def) {
    RockRes r;
    resolve(def, &r);
    RockTones t;
    tones_of(def, &t);
    RockPlan plan;
    crust_plan(def, &plan);
    int chunks = js_round(pnum(def, "chunks", 6.0));
    if (chunks < 2) chunks = 2;
    double spread = pnum(def, "spread", 0.8) * r.diameter;
    RockCrust crust; int has_crust;
    crust_args(&plan, &crust, &has_crust);

    for (int i = 0; i < chunks; i++) {
        int main_chunk = (i == 0);
        double k = main_chunk ? 0.52 : 0.2 + rr(&r) * 0.26;
        double rad = (r.diameter * k) / 2.0;
        double a = rr(&r) * TAU;
        double d = main_chunk ? 0.0 : spread * (0.35 + rr(&r) * 0.75);
        double h = rad * (1.1 + rr(&r) * 0.9);

        RockShellOpts o;
        shell_defaults(&o);
        o.center[0] = cos(a) * d;
        o.center[1] = -h * 0.34 + h * 0.5;
        o.center[2] = sin(a) * d;
        o.rx = rad;
        o.rz = rad * (0.7 + rr(&r) * 0.6);
        o.thickness = h * 0.82;
        int sides2 = r.sides - (main_chunk ? 1 : 2);
        if (sides2 < 4) sides2 = 4;
        o.sides = (double)sides2;
        o.normal[0] = (rr(&r) * 2.0 - 1.0) * 0.36;
        o.normal[1] = 1.0;
        o.normal[2] = (rr(&r) * 2.0 - 1.0) * 0.36;
        o.color = qlcg_lp_mix(t.body, t.band, rr(&r) * 0.45);
        LpRgb capm = qlcg_lp_mix(t.cap, t.body, rr(&r) * 0.3);
        o.cap_color[0] = capm.r; o.cap_color[1] = capm.g; o.cap_color[2] = capm.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.5);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter * 1.3;
        o.rot_y = rr(&r) * TAU;
        o.crust = crust;
        o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_shell(b, &o);
    }
    int litter = js_round((double)chunks / 3.0);
    if (litter < 1) litter = 1;
    for (int i = 0; i < litter; i++) {
        double a = rr(&r) * TAU;
        double d = spread * (0.6 + rr(&r) * 0.6);
        RockShellOpts o;
        shell_defaults(&o);
        o.center[0] = cos(a) * d;
        o.center[1] = r.height * 0.06;
        o.center[2] = sin(a) * d;
        o.rx = r.diameter * (0.08 + rr(&r) * 0.1);
        o.rz = r.diameter * (0.06 + rr(&r) * 0.1);
        o.thickness = r.height * 0.16;
        o.sides = 4;
        o.normal[0] = (rr(&r) * 2.0 - 1.0) * 0.5;
        o.normal[1] = 1.0;
        o.normal[2] = (rr(&r) * 2.0 - 1.0) * 0.5;
        o.color = qlcg_lp_deepen(t.base, 0.12);
        LpRgb cm = qlcg_lp_mix(t.cap, t.base, 0.4);
        o.cap_color[0] = cm.r; o.cap_color[1] = cm.g; o.cap_color[2] = cm.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.55);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter;
        o.rot_y = rr(&r) * TAU;
        qlcg_rock_shell(b, &o);
    }
    if (wants_stack(def)) stack_core(b, def, &r, &t, &plan);
}

/* ---------- 板岩席 ---------- */

static void arch_slab(RockBuilder *b, const RockBuildDef *def) {
    RockRes r; resolve(def, &r);
    RockTones t; tones_of(def, &t);
    RockPlan plan; crust_plan(def, &plan);
    int plates = js_round(pnum(def, "plates", 3.0));
    if (plates < 1) plates = 1;
    double thickness = pnum(def, "plateThickness", 0.12) * r.diameter;
    double rise = pnum(def, "plateRise", 1.0);
    double skew = pnum(def, "plateSkew", 0.14) * r.diameter;
    double tilt = pnum(def, "plateTilt", 0.14);
    int rl = plates + 1;
    double *wob = (double *)malloc((size_t)(rl * r.sides) * sizeof(double));
    if (!wob) return;
    make_wobble(&r, r.sides, rl, wob);
    RockCrust crust; int has_crust; crust_args(&plan, &crust, &has_crust);
    double y = -r.height * 0.08;
    int denom = plates > 1 ? plates : 1;
    for (int i = 0; i < plates; i++) {
        double k = 1.0 - ((double)i / (double)denom) * 0.32;
        RockSlabOpts o;
        memset(&o, 0, sizeof(o));
        o.center[0] = (rr(&r) * 2.0 - 1.0) * skew;
        o.center[1] = y + thickness / 2.0;
        o.center[2] = (rr(&r) * 2.0 - 1.0) * skew * 0.7;
        o.rx = (r.diameter / 2.0) * k;
        o.rz = (r.diameter / 2.0) * k * (0.66 + rr(&r) * 0.4);
        o.thickness = thickness * (0.8 + rr(&r) * 0.5);
        o.sides = (double)r.sides;
        o.tilt = tilt * (0.6 + rr(&r) * 0.9);
        o.bearing = rr(&r) * TAU;
        o.color = (i % 2 == 1) ? qlcg_lp_mix(t.body, t.band, 0.4) : t.body;
        o.mat = -1;
        o.jitter = r.jitter * 0.8;
        o.wobble = wob + (size_t)i * (size_t)r.sides;
        o.wobble_amount = 0.55;
        o.rot_y = rr(&r) * TAU;
        o.crust = crust;
        o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_slab(b, &o);
        y += thickness * (0.86 + rr(&r) * 0.3) * rise;
    }
    free(wob);
    if (wants_stack(def)) stack_core(b, def, &r, &t, &plan);
}

/* ---------- 石台 ---------- */

static void arch_ledge(RockBuilder *b, const RockBuildDef *def) {
    RockRes r; resolve(def, &r);
    RockTones t; tones_of(def, &t);
    RockPlan plan; crust_plan(def, &plan);
    double tilt_x_def = 0.05 + (rr(&r) * 2.0 - 1.0) * 0.05;
    double tilt_z_def = (rr(&r) * 2.0 - 1.0) * 0.05;
    double tilt_x = pnum(def, "tiltX", tilt_x_def);
    double tilt_z = pnum(def, "tiltZ", tilt_z_def);
    double normal[3] = { sin(tilt_z), cos(tilt_z) * cos(tilt_x), sin(tilt_x) };
    double sink = pnum(def, "sink", 0.34) * r.height;
    double main_top = pnum(def, "baseFlatten", 0.86) * r.height;
    double main_thick = main_top + sink;
    RockCrust crust; int has_crust; crust_args(&plan, &crust, &has_crust);

    double *rip = (double *)malloc((size_t)(2 * r.sides) * sizeof(double));
    if (!rip) return;

    {
        RockShellOpts o; shell_defaults(&o);
        o.center[0] = 0; o.center[1] = main_top - main_thick / 2.0; o.center[2] = 0;
        o.rx = r.diameter / 2.0;
        o.rz = (r.diameter / 2.0) * (0.7 + rr(&r) * 0.35);
        make_ripple(&r, r.sides, 2, rip);
        o.thickness = main_thick;
        int sides2 = r.sides > 5 ? r.sides : 5;
        o.sides = (double)sides2;
        o.normal[0] = normal[0]; o.normal[1] = normal[1]; o.normal[2] = normal[2];
        o.color = t.body;
        LpRgb cm = qlcg_lp_mix(t.cap, t.body, 0.25);
        o.cap_color[0] = cm.r; o.cap_color[1] = cm.g; o.cap_color[2] = cm.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.45);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter * 0.6;
        o.radius_scale = rip;
        o.rot_y = rr(&r) * TAU;
        o.crust = crust; o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_shell(b, &o);
    }
    {
        double step_h = r.height * 0.4;
        double step_top = main_top + step_h * 0.92;
        RockShellOpts o; shell_defaults(&o);
        o.center[0] = (rr(&r) * 2.0 - 1.0) * r.diameter * 0.16;
        o.center[1] = step_top - step_h / 2.0;
        o.center[2] = (rr(&r) * 2.0 - 1.0) * r.diameter * 0.12;
        o.rx = r.diameter * 0.3;
        o.rz = r.diameter * 0.24 * (0.8 + rr(&r) * 0.4);
        make_ripple(&r, r.sides, 2, rip);
        o.thickness = step_h;
        int sides2 = r.sides - 1;
        if (sides2 < 4) sides2 = 4;
        o.sides = (double)sides2;
        o.normal[0] = sin(tilt_z * 0.6);
        o.normal[1] = cos(tilt_z * 0.6) * cos(tilt_x * 0.6);
        o.normal[2] = sin(tilt_x * 0.6);
        o.color = qlcg_lp_mix(t.body, t.band, 0.2);
        o.cap_color[0] = t.cap.r; o.cap_color[1] = t.cap.g; o.cap_color[2] = t.cap.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.5);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter * 0.7;
        o.radius_scale = rip + r.sides;
        o.rot_y = rr(&r) * TAU;
        o.crust = crust; o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_shell(b, &o);
    }
    free(rip);

    double cap_y = main_top + r.height * 0.015;
    for (int i = 0; i < 2; i++) {
        RockShellOpts o; shell_defaults(&o);
        o.center[0] = (rr(&r) * 2.0 - 1.0) * r.diameter * 0.2;
        o.center[1] = cap_y;
        o.center[2] = (rr(&r) * 2.0 - 1.0) * r.diameter * 0.2;
        o.rx = r.diameter * (0.3 + rr(&r) * 0.2);
        o.rz = r.diameter * (0.05 + rr(&r) * 0.04);
        o.thickness = r.height * 0.1;
        o.sides = 4;
        o.normal[0] = 0; o.normal[1] = 1; o.normal[2] = 0;
        o.color = qlcg_lp_deepen(t.band, 0.28);
        LpRgb cm = qlcg_lp_deepen(t.band, 0.2);
        o.cap_color[0] = cm.r; o.cap_color[1] = cm.g; o.cap_color[2] = cm.b;
        LpRgb bc = qlcg_lp_deepen(t.band, 0.5);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter * 0.5;
        o.rot_y = rr(&r) * TAU;
        qlcg_rock_shell(b, &o);
    }
    if (wants_stack(def)) stack_core(b, def, &r, &t, &plan);
}

/* ---------- 碎岩 ---------- */

static void arch_shard(RockBuilder *b, const RockBuildDef *def) {
    RockRes r; resolve(def, &r);
    RockTones t; tones_of(def, &t);
    RockPlan plan; crust_plan(def, &plan);
    RockCrust crust; int has_crust; crust_args(&plan, &crust, &has_crust);

    {
        RockShellOpts o; shell_defaults(&o);
        o.center[0] = 0; o.center[1] = -r.height * 0.1; o.center[2] = 0;
        o.rx = r.diameter / 2.0;
        o.rz = (r.diameter / 2.0) * (0.72 + rr(&r) * 0.3);
        o.thickness = r.height * 0.3;
        int sides2 = r.sides > 5 ? r.sides : 5;
        o.sides = (double)sides2;
        o.normal[0] = 0; o.normal[1] = 1; o.normal[2] = 0;
        o.color = t.body;
        LpRgb cm = qlcg_lp_mix(t.cap, t.body, 0.35);
        o.cap_color[0] = cm.r; o.cap_color[1] = cm.g; o.cap_color[2] = cm.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.45);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter * 0.7;
        o.rot_y = rr(&r) * TAU;
        o.crust = crust; o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_shell(b, &o);
    }
    int plates = js_round(pnum(def, "plates", 3.0));
    if (plates < 2) plates = 2;
    for (int i = 0; i < plates; i++) {
        double bearing = ((double)i / (double)plates) * TAU + rr(&r) * 0.8;
        double tilt = 0.55 + rr(&r) * 0.55;
        double len = (r.diameter / 2.0) * (0.5 + rr(&r) * 0.42);
        double h = r.height * (0.9 + rr(&r) * 0.7);
        RockShellOpts o; shell_defaults(&o);
        o.center[0] = cos(bearing) * len * 0.45;
        o.center[1] = r.height * (0.16 + rr(&r) * 0.2);
        o.center[2] = sin(bearing) * len * 0.45;
        o.rx = len;
        o.rz = len * (0.34 + rr(&r) * 0.24);
        o.thickness = h * 0.42;
        o.sides = (double)(4 + js_round(rr(&r)));
        o.normal[0] = sin(tilt) * cos(bearing);
        o.normal[1] = cos(tilt);
        o.normal[2] = sin(tilt) * sin(bearing);
        o.color = qlcg_lp_mix(t.body, t.band, 0.18 + rr(&r) * 0.3);
        o.cap_color[0] = t.cap.r; o.cap_color[1] = t.cap.g; o.cap_color[2] = t.cap.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.5);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter * 0.8;
        o.rot_y = bearing + PI / 2.0;
        o.crust = crust; o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_shell(b, &o);
    }
    if (wants_stack(def)) stack_core(b, def, &r, &t, &plan);
}

/* ---------- 尖石 ---------- */

static void arch_spire(RockBuilder *b, const RockBuildDef *def) {
    RockRes r; resolve(def, &r);
    RockTones t; tones_of(def, &t);
    RockPlan plan; crust_plan(def, &plan);
    RockCrust crust; int has_crust; crust_args(&plan, &crust, &has_crust);
    double sink = pnum(def, "sink", 0.06) * r.height;

    {
        RockShellOpts o; shell_defaults(&o);
        o.center[0] = 0; o.center[1] = -sink + r.height * 0.1; o.center[2] = 0;
        o.rx = (r.diameter / 2.0) * (0.8 + rr(&r) * 0.2);
        o.rz = (r.diameter / 2.0) * (0.66 + rr(&r) * 0.24);
        o.thickness = r.height * 0.22;
        int sides2 = r.sides > 5 ? r.sides : 5;
        o.sides = (double)sides2;
        o.normal[0] = 0; o.normal[1] = 1; o.normal[2] = 0;
        o.color = t.body;
        LpRgb cm = qlcg_lp_mix(t.cap, t.body, 0.3);
        o.cap_color[0] = cm.r; o.cap_color[1] = cm.g; o.cap_color[2] = cm.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.45);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter * 0.7;
        o.rot_y = rr(&r) * TAU;
        o.crust = crust; o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_shell(b, &o);
    }
    {
        RockColumnOpts o; column_defaults(&o);
        o.base[0] = 0; o.base[1] = -sink + r.height * 0.18; o.base[2] = 0;
        o.radius = (r.diameter / 2.0) * pnum(def, "baseRatio", 0.55);
        o.height = r.height * 0.86;
        int segs = js_round(pnum(def, "segments", 3.0));
        o.segments = (double)(segs < 1 ? 1 : segs);
        o.tip_ratio = pnum(def, "tipRatio", 0.0);
        int sides2 = r.sides - 1;
        if (sides2 < 4) sides2 = 4;
        o.sides = (double)sides2;
        o.color = t.body;
        o.skew = pnum(def, "nodeSkew", 0.16);
        o.twist = pnum(def, "nodeTwist", 0.4);
        LpRgb tc = qlcg_lp_mix(t.cap, qlcg_lp_lighten(t.body, 0.12), 0.5);
        o.tip_color[0] = tc.r; o.tip_color[1] = tc.g; o.tip_color[2] = tc.b;
        o.jitter = r.jitter * 1.2;
        o.crust = crust; o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_column(b, &o);
    }
    int chips = js_round(pnum(def, "chunks", 2.0));
    if (chips < 1) chips = 1;
    for (int i = 0; i < chips; i++) {
        double a = rr(&r) * TAU;
        double d = (r.diameter / 2.0) * (0.5 + rr(&r) * 0.4);
        RockShellOpts o; shell_defaults(&o);
        o.center[0] = cos(a) * d;
        o.center[1] = r.height * (0.06 + rr(&r) * 0.16);
        o.center[2] = sin(a) * d;
        o.rx = r.diameter * (0.12 + rr(&r) * 0.1);
        o.rz = r.diameter * (0.1 + rr(&r) * 0.08);
        o.thickness = r.height * 0.2;
        o.sides = (double)(4 + js_round(rr(&r)));
        o.normal[0] = cos(a) * 0.4; o.normal[1] = 1.0; o.normal[2] = sin(a) * 0.4;
        o.color = qlcg_lp_mix(t.body, t.band, 0.3);
        LpRgb cm = qlcg_lp_mix(t.cap, t.body, 0.3);
        o.cap_color[0] = cm.r; o.cap_color[1] = cm.g; o.cap_color[2] = cm.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.5);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter;
        o.rot_y = rr(&r) * TAU;
        o.crust = crust; o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_shell(b, &o);
    }
    if (wants_stack(def)) stack_core(b, def, &r, &t, &plan);
}

/* ---------- 石墩 ---------- */

static void arch_stub(RockBuilder *b, const RockBuildDef *def) {
    RockRes r; resolve(def, &r);
    RockTones t; tones_of(def, &t);
    RockPlan plan; crust_plan(def, &plan);
    RockCrust crust; int has_crust; crust_args(&plan, &crust, &has_crust);
    double sink = pnum(def, "sink", 0.14) * r.height;

    {
        RockShellOpts o; shell_defaults(&o);
        o.center[0] = 0; o.center[1] = -sink + r.height * 0.14; o.center[2] = 0;
        o.rx = (r.diameter / 2.0) * (0.9 + rr(&r) * 0.16);
        o.rz = (r.diameter / 2.0) * (0.78 + rr(&r) * 0.2);
        o.thickness = r.height * 0.3;
        int sides2 = r.sides > 5 ? r.sides : 5;
        o.sides = (double)sides2;
        o.normal[0] = 0; o.normal[1] = 1; o.normal[2] = 0;
        o.color = qlcg_lp_mix(t.body, t.base, 0.22);
        LpRgb cm = qlcg_lp_mix(t.cap, t.body, 0.3);
        o.cap_color[0] = cm.r; o.cap_color[1] = cm.g; o.cap_color[2] = cm.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.5);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter * 0.8;
        o.rot_y = rr(&r) * TAU;
        o.crust = crust; o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_shell(b, &o);
    }
    {
        RockColumnOpts o; column_defaults(&o);
        o.base[0] = 0; o.base[1] = -sink + r.height * 0.24; o.base[2] = 0;
        o.radius = (r.diameter / 2.0) * pnum(def, "baseRatio", 0.72);
        o.height = r.height * 0.72;
        int segs = js_round(pnum(def, "segments", 2.0));
        o.segments = (double)(segs < 1 ? 1 : segs);
        o.tip_ratio = pnum(def, "tipRatio", 0.72);
        int sides2 = r.sides - 1;
        if (sides2 < 4) sides2 = 4;
        o.sides = (double)sides2;
        o.color = t.body;
        o.skew = pnum(def, "nodeSkew", 0.1);
        o.twist = pnum(def, "nodeTwist", 0.26);
        LpRgb tc = qlcg_lp_mix(t.cap, qlcg_lp_lighten(t.body, 0.06), 0.6);
        o.tip_color[0] = tc.r; o.tip_color[1] = tc.g; o.tip_color[2] = tc.b;
        o.jitter = r.jitter * 0.9;
        o.crust = crust; o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_column(b, &o);
    }
    if (wants_stack(def)) stack_core(b, def, &r, &t, &plan);
}

/* ---------- 冰岩 ---------- */

static void arch_floe(RockBuilder *b, const RockBuildDef *def) {
    RockRes r; resolve(def, &r);
    RockTones t; tones_of(def, &t);
    RockPlan plan; crust_plan(def, &plan);
    RockCrust crust; int has_crust; crust_args(&plan, &crust, &has_crust);
    double sink = pnum(def, "sink", 0.22) * r.height;
    int shells = js_round(pnum(def, "shells", 3.0));
    if (shells < 2) shells = 2;
    double taper = pnum(def, "shellTaper", 0.84);
    double span = r.height - sink;
    if (span < r.height * 0.15) span = r.height * 0.15;
    int sides_min = r.sides > 5 ? r.sides : 5;

    double *steps = (double *)malloc((size_t)shells * sizeof(double));
    int rl = shells + 2;
    double *rip = (double *)malloc((size_t)(rl * sides_min) * sizeof(double));
    double *wob = (double *)malloc((size_t)(rl * sides_min) * sizeof(double));
    if (!steps || !rip || !wob) { free(steps); free(rip); free(wob); return; }
    double step_sum = 0.0;
    for (int s = 0; s < shells; s++) { steps[s] = 0.7 + rr(&r) * 0.6; step_sum += steps[s]; }
    make_ripple(&r, sides_min, rl, rip);
    make_wobble(&r, sides_min, rl, wob);

    double rx = r.diameter / 2.0;
    double rz = rx * (0.8 + rr(&r) * 0.3);
    double y = -sink;
    int shell_denom = shells > 1 ? shells - 1 : 1;
    for (int s = 0; s < shells; s++) {
        double k = pow(taper, s);
        double h = (span * steps[s]) / step_sum;
        double blend = (double)s / (double)shell_denom;
        RockShellOpts o; shell_defaults(&o);
        o.center[0] = (rr(&r) * 2.0 - 1.0) * 0.09 * rx;
        o.center[1] = y + h / 2.0;
        o.center[2] = (rr(&r) * 2.0 - 1.0) * 0.09 * rz;
        o.rx = rx * k;
        o.rz = rz * k;
        o.thickness = h * 1.04;
        o.sides = (double)sides_min;
        o.normal[0] = 0; o.normal[1] = 1; o.normal[2] = 0;
        o.color = qlcg_lp_mix(qlcg_lp_deepen(t.body, 0.18), t.body, blend);
        o.cap_color[0] = t.cap.r; o.cap_color[1] = t.cap.g; o.cap_color[2] = t.cap.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.4);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter * 0.6;
        o.radius_scale = rip + (size_t)s * (size_t)sides_min;
        o.wobble = wob + (size_t)s * (size_t)sides_min;
        o.wobble_amount = 0.45;
        o.rot_y = rr(&r) * TAU;
        o.crust = crust; o.has_crust = has_crust ? 1 : 0;
        qlcg_rock_shell(b, &o);
        y += h * 0.95;
        rx *= taper;
        rz *= taper;
    }
    free(steps); free(rip); free(wob);

    int plates = js_round(pnum(def, "plates", 2.0));
    if (plates < 1) plates = 1;
    double rad = r.diameter / 2.0;
    for (int i = 0; i < plates; i++) {
        double bearing = rr(&r) * TAU;
        double tilt = 0.12 + rr(&r) * 0.3;
        RockShellOpts o; shell_defaults(&o);
        o.center[0] = (rr(&r) * 2.0 - 1.0) * rad * 0.32;
        o.center[1] = r.height * (0.46 + rr(&r) * 0.16);
        o.center[2] = (rr(&r) * 2.0 - 1.0) * rad * 0.32;
        o.rx = rad * (0.42 + rr(&r) * 0.3);
        o.rz = rad * (0.3 + rr(&r) * 0.26);
        o.thickness = r.height * (0.12 + rr(&r) * 0.1);
        o.sides = (double)(4 + js_round(rr(&r) * 2.0));
        o.normal[0] = sin(tilt) * cos(bearing);
        o.normal[1] = cos(tilt);
        o.normal[2] = sin(tilt) * sin(bearing);
        o.color = t.body;
        o.cap_color[0] = t.cap.r; o.cap_color[1] = t.cap.g; o.cap_color[2] = t.cap.b;
        LpRgb bc = qlcg_lp_deepen(t.base, 0.35);
        o.bottom_color[0] = bc.r; o.bottom_color[1] = bc.g; o.bottom_color[2] = bc.b;
        o.jitter = r.jitter * 0.4;
        o.rot_y = rr(&r) * TAU;
        o.mat = ROCK_MAT_ICE;
        qlcg_rock_shell(b, &o);
    }
    if (wants_stack(def)) stack_core(b, def, &r, &t, &plan);
}

typedef struct {
    double x, y, z, rx, rz, height, sink, mount_z;
    double taper, shoulder, crown, lean, twist, fracture, bedding;
    double lean_z;
    const double *joint_profile;
    int sides, tiers, beds, wall;
    uint32_t salt;
} RockMass;

static double mass_angle(const RockMass *m, double side, double t) {
    if (m->joint_profile) return TAU * side / m->sides + m->twist * sin(PI * t);
    if (!m->wall) return TAU * side / m->sides + m->twist * t;
    double u = side / (m->sides - 1);
    return (u - .5) * TAU * (2.0 / 3.0) + m->twist * t * sin(PI * u);
}

static void stack_ring(const RockMass *m, int row, double ring[16][3]) {
    const double joint_radius = 2.4;
    double t = (double)row / m->tiers;
    int segment = row == m->tiers ? 3 : (int)(4 * t);
    double fraction = 4 * t - segment;
    double profile = m->joint_profile[segment] * (1.0 - fraction) + m->joint_profile[segment + 1] * fraction;
    double blend = fmin(1.0, 8.0 * fmin(t, 1.0 - t));
    double drift = sin(PI * t);
    for (int i = 0; i < m->sides; i++) {
        double a = TAU * i / m->sides;
        double radius = i % 2 ? joint_radius * cos(PI / 8) : joint_radius;
        double x = radius * sin(a), z = radius * cos(a);
        if (row == 0 || row == m->tiers) {
            ring[i][0] = x;
            ring[i][1] = m->height * t;
            ring[i][2] = z;
            continue;
        }
        double angle = mass_angle(m, i, t);
        double fault = 2.0 * qlcg_lp_hash2(i, 0, (int32_t)m->salt) - 1.0;
        double chips = 2.0 * qlcg_lp_hash2(i, row, (int32_t)m->salt) - 1.0;
        double bed = .5 + .5 * cos(TAU * (m->beds * t + m->bedding * sin(angle)));
        double r = profile * (1.0 + m->fracture * (.72 * fault + .24 * chips - .38 * bed * bed));
        double body_x = m->rx * r * sin(angle) + m->lean * drift;
        double body_z = m->rz * r * cos(angle) + m->lean_z * drift;
        ring[i][0] = x + (body_x - x) * blend;
        ring[i][1] = m->height * (t + .45 * m->bedding * drift * sin(a));
        ring[i][2] = z + (body_z - z) * blend;
    }
}

static void mass_ring(const RockMass *m, int row, double ring[16][3]) {
    if (m->joint_profile) {
        stack_ring(m, row, ring);
        return;
    }
    double t = (double)row / m->tiers;
    double mid = sin(PI * t);
    double profile = fmax(.13, (1.0 - m->taper * t + m->shoulder * mid)
                                    * (1.0 - m->crown * pow(t, 7.0)));
    for (int i = 0; i < m->sides; i++) {
        double angle = mass_angle(m, i, t);
        double x = sin(angle), z = cos(angle);
        double fault = 2.0 * qlcg_lp_hash2(i, 0, (int32_t)m->salt) - 1.0;
        double chips = 2.0 * qlcg_lp_hash2(i, row + 1, (int32_t)m->salt) - 1.0;
        double bed = .5 + .5 * cos(TAU * (m->beds * t + m->bedding * x));
        double cut = bed * bed * bed;
        double r = profile * (1.0 + m->fracture * (.78 * fault + .22 * chips - .42 * cut));
        ring[i][0] = m->x + m->rx * (r * x + m->lean * t);
        ring[i][1] = m->y + m->height * ((1.0 + m->sink) * t - m->sink + m->bedding * mid * x);
        ring[i][2] = m->z + m->rz * r * z;
        if (m->wall)
            ring[i][2] = i == 0 || i == m->sides - 1 ? m->mount_z
                : fmax(ring[i][2], m->mount_z + .06 * m->rz);
    }
}

static void mass_cap(RockBuilder *b, const RockMass *m, double ring[16][3], LpRgb color, int top) {
    double center[3] = {0};
    if (m->wall) {
        for (int axis = 0; axis < 3; axis++)
            center[axis] = .5 * (ring[0][axis] + ring[m->sides - 1][axis]);
    } else {
        for (int i = 0; i < m->sides; i++)
            for (int axis = 0; axis < 3; axis++)
                center[axis] += ring[i][axis] / m->sides;
    }
    for (int i = 0; i < m->sides - m->wall; i++) {
        int j = (i + 1) % m->sides;
        qlcg_rock_tri(b, center, ring[top ? i : j], ring[top ? j : i], color, ROCK_MAT_SOLID);
    }
}

static void build_mass(RockBuilder *b, const RockMass *m, const RockTones *tones) {
    double rings[2][16][3];
    mass_ring(m, 0, rings[0]);
    mass_cap(b, m, rings[0], tones->base, 0);
    for (int row = 0; row < m->tiers; row++) {
        double (*low)[3] = rings[row % 2], (*high)[3] = rings[(row + 1) % 2];
        mass_ring(m, row + 1, high);
        double t = (row + .5) / m->tiers;
        for (int i = 0; i < m->sides - m->wall; i++) {
            int j = (i + 1) % m->sides;
            double angle = mass_angle(m, i + .5, t);
            double band = .5 + .5 * cos(TAU * (m->beds * t + m->bedding * sin(angle)));
            LpRgb color = qlcg_lp_mix(tones->body, tones->band, .58 * band * band);
            color = qlcg_lp_mix(color, tones->cap, .24 * t * t);
            qlcg_rock_quad(b, low[i], low[j], high[j], high[i], color, ROCK_MAT_SOLID);
        }
    }
    mass_cap(b, m, rings[m->tiers % 2], tones->cap, 1);
}

static void arch_mega(RockBuilder *b, const RockBuildDef *def) {
    double h = def->height_m * pnum(def, "sizeScale", 1.0);
    if (!(h > 0.0)) return;
    RockTones tones;
    tones_of(def, &tones);
    RockRes r;
    resolve(def, &r);
    double spread = pnum(def, "spreadScale", 1.0);
    int wall = strcmp(def->archetype, "escarpment") == 0;
    RockMass main = {
        .rx = h * spread * pnum(def, "widthRatio", .75) * .5,
        .rz = h * spread * pnum(def, "depthRatio", .58) * .5,
        .height = h, .sink = pnum(def, "sink", wall ? .45 : .20),
        .taper = pnum(def, "taper", .52), .shoulder = pnum(def, "shoulder", .08),
        .crown = pnum(def, "crown", .44), .lean = pnum(def, "lean", .12),
        .twist = pnum(def, "twist", .15), .fracture = pnum(def, "fracture", .22),
        .bedding = pnum(def, "bedding", .05),
        .sides = (int)qlcg_lp_clamp(r.sides, 8, 16),
        .tiers = (int)qlcg_lp_clamp(pnum(def, "tiers", 16), 12, 24),
        .beds = (int)qlcg_lp_clamp(pnum(def, "beds", 6), 3, 9),
        .wall = wall, .salt = def->build_seed ^ def->shape_seed,
    };
    if (wall) {
        main.sides = (main.sides * 2 + 1) / 3 + 1;
        main.mount_z = -.52 * main.rz;
    }
    build_mass(b, &main, &tones);

    int ribs = (int)qlcg_lp_clamp(pnum(def, "ribs", 2), 1, 4);
    double relief = qlcg_lp_clamp(pnum(def, "relief", .58), .20, .99);
    for (int i = 0; i < ribs; i++) {
        RockMass rib = main;
        double across = (i + .5) / ribs * 2.0 - 1.0;
        rib.x = main.rx * (across * (wall ? .85 : 1.18) + (rr(&r) - .5) * .18);
        rib.z = main.rz * (wall ? .57 + rr(&r) * .24 : .12 + rr(&r) * .43);
        if (!wall && (i % 2)) rib.z = -rib.z;
        rib.rx *= (wall ? .34 : .50) + rr(&r) * .19;
        rib.rz *= .49 + rr(&r) * .24;
        rib.height *= relief * (.60 + rr(&r) * .40);
        rib.sink = main.sink * h / rib.height;
        rib.lean = main.lean * .5 + (rr(&r) - .5) * .37;
        rib.twist = -main.twist * .7;
        rib.sides = wall ? 6 : 9;
        rib.tiers = main.tiers / 2 + 2;
        rib.salt ^= (uint32_t)(i + 1) * 0x9e3779b9u;
        RockTones shade = tones;
        shade.body = qlcg_lp_mix(tones.body, tones.band, .12 + rr(&r) * .22);
        build_mass(b, &rib, &shade);
    }
}

static void arch_stone_forest(RockBuilder *b, const RockBuildDef *def) {
    if (!(def->height_m > 0.0)) return;
    RockTones tones;
    tones_of(def, &tones);
    const double profile[] = {
        pnum(def, "foot", .8), pnum(def, "lower", .9), pnum(def, "waist", .85),
        pnum(def, "upper", .8), pnum(def, "head", .7)
    };
    RockMass mass = {
        .rx = pnum(def, "widthMeters", 8.0) * .5,
        .rz = pnum(def, "depthMeters", 7.0) * .5,
        .height = def->height_m, .joint_profile = profile,
        .lean = pnum(def, "leanX", 0.0), .lean_z = pnum(def, "leanZ", 0.0),
        .twist = pnum(def, "twist", .14), .fracture = pnum(def, "fracture", .22),
        .bedding = qlcg_lp_clamp(pnum(def, "bedding", .08), -.24, .24),
        .sides = 16, .tiers = (int)qlcg_lp_clamp(pnum(def, "tiers", 16), 12, 22),
        .beds = (int)qlcg_lp_clamp(pnum(def, "beds", 5), 3, 9),
        .salt = def->build_seed,
    };
    build_mass(b, &mass, &tones);
}

static const struct { const char *name; RockArchFn fn; } ARCH_TABLE[] = {
    { "boulder", arch_boulder },
    { "block", arch_block },
    { "rubble", arch_rubble },
    { "slab", arch_slab }, { "ledge", arch_ledge }, { "shard", arch_shard },
    { "spire", arch_spire }, { "stub", arch_stub }, { "floe", arch_floe },
    { "escarpment", arch_mega }, { "landmark", arch_mega },
    { "stoneForest", arch_stone_forest },
};

RockArchFn qlcg_rock_arch_fn(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < (int)(sizeof(ARCH_TABLE) / sizeof(ARCH_TABLE[0])); i++)
        if (strcmp(ARCH_TABLE[i].name, name) == 0) return ARCH_TABLE[i].fn;
    return NULL;
}


static double main_span(const RockBuildDef *def) {
    RockArchFn fn = qlcg_rock_arch_fn(def->archetype);
    if (!fn) return 0.5;
    LpRng rng;
    qlcg_lp_rng_seed(&rng, (uint32_t)(def->build_seed ^ def->shape_seed ^ 0x9e3779b9u));
    RockBuilder probe;
    qlcg_rock_builder_init(&probe, &rng);
    RockBuildDef copy = *def;
    copy.form = ROCK_FORM_OUTCROP;
    fn(&probe, &copy);
    RockGeometry g;
    qlcg_rock_build(&probe, &g);
    double h = (g.height > 0.05) ? g.height : 0.05;
    qlcg_rock_geometry_free(&g);
    qlcg_rock_builder_free(&probe);
    return h;
}

int qlcg_rock_arch_build(const RockBuildDef *def, RockGeometry *out) {
    RockArchFn fn = qlcg_rock_arch_fn(def->archetype);
    if (!fn) return 0;
    LpRng rng;
    qlcg_lp_rng_seed(&rng, (uint32_t)(def->build_seed ^ def->shape_seed));
    RockBuilder b;
    qlcg_rock_builder_init(&b, &rng);
    fn(&b, def);
    qlcg_rock_build(&b, out);
    qlcg_rock_builder_free(&b);
    if (out->vertex_count > 0 && def->height_m > 0.0 && def->form == ROCK_FORM_OUTCROP)
        qlcg_rock_geometry_scale(out, def->height_m / out->height);
    return out->vertex_count > 0;
}
