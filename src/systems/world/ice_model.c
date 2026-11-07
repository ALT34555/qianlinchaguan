/* ice_model：相位温度+热滞后+度日积分 */
#include "ice_model.h"
#include <math.h>
#include <string.h>

#define ICE_PI   3.14159265358979323846
#define ICE_TAU  6.28318530717958647693
#define ICE_OMEGA (6.28318530717958647693 / 360.0)

static double clampd(double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
static double dmod(double x, double m) {
    double r = fmod(x, m);
    return r < 0.0 ? r + m : r;
}
static double dstep(double a, double b, double x) {
    double t = clampd((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/* 温度原函数：∫(m+A·sinφ)dt */
static double primitive(double m, double a, double lo, double hi) {
    return (m * (hi - lo) - a * (cos(hi) - cos(lo))) / ICE_OMEGA;
}

typedef struct {
    bool   valid;
    double start, end;
    double cold, warm;
    double h0, peak, old_q, peak_q;
} IceCycle;

static IceCycle ice_cycle(const IceParams *p) {
    IceCycle c;
    memset(&c, 0, sizeof(c));
    if (p->amp < 1e-10 || fabs(p->mean) >= p->amp) return c;
    double angle = asin(-p->mean / p->amp);
    c.start = ICE_PI - angle;
    c.end   = ICE_TAU + angle;
    c.cold  = fmax(0.0, -primitive(p->mean, p->amp, c.start, c.end));
    c.warm  = fmax(0.0, primitive(p->mean, p->amp, c.end, c.start + ICE_TAU));
    double cap_c = p->k2 * fmax(0.0, c.cold - p->debt);
    double decay_h = p->km * c.warm;
    c.h0 = decay_h > 1e-12
         ? clampd((cap_c - decay_h * decay_h) / (2.0 * decay_h), 0.0, fmax(0.0, p->cap - decay_h))
         : p->cap;
    c.peak = fmin(p->cap, sqrt(c.h0 * c.h0 + cap_c));
    double ratio = c.peak > 1e-12 ? c.h0 / c.peak : 0.0;
    double decay = exp(-c.warm / 18.0);
    double denom = 1.0 - ratio * decay;
    double peak_q = denom > 1e-12 ? (1.0 - ratio) / denom : 1.0;
    double old_q = fmax(.08, peak_q * decay);
    peak_q = ratio * old_q + 1.0 - ratio;
    c.old_q = old_q;
    c.peak_q = peak_q;
    c.valid = true;
    return c;
}

void qlcg_ice_params(const IceSite *site, IceParams *out) {
    memset(out, 0, sizeof(*out));
    if (!site) return;
    float depth = site->depth > 0.0f ? site->depth : 0.0f;
    double u = clampd((double)site->raw_speed / 2.5, 0.0, 1.0);
    double by_grade = dstep(.008, .04, site->grade);
    if (by_grade > u) u = by_grade;
    u *= 1.0 - .65 * (double)site->lateral * (double)site->lateral;
    double shore = clampd(site->shore_weight, 0.0, 1.0);
    double lag = clampd(.6 + .65 * fmin(depth, 16.0) - .4 * shore, .25, 12.0);
    double signed_amp = 16.0 * sin((double)site->latitude);
    double amp = fabs(signed_amp) / sqrt(1.0 + (ICE_OMEGA * lag) * (ICE_OMEGA * lag));
    double shift = -atan(ICE_OMEGA * lag) + (signed_amp < 0.0 ? ICE_PI : 0.0);
    double point = (site->kind == ICE_KIND_SEA) ? (double)QLCG_ICE_SEA_POINT : 0.0;
    double noise = clampd(site->noise, 0.0, 1.0);
    double k2 = .00075 * (1.0 - .55 * u) * (.9 + .2 * noise);
    if (site->kind == ICE_KIND_SEA) k2 *= .7;
    out->mean  = (double)site->annual_temp - point + 2.0 * u;
    out->amp   = amp;
    out->shift = shift;
    out->k2    = k2;
    out->km    = .012 * (1.0 + .8 * shore + .6 * u);
    out->debt  = 1.5 + 1.8 * fmin(depth, 12.0) + 5.0 * u;
    out->cap   = fmax(0.0, fmin(depth, site->kind == ICE_KIND_RIVER ? .70 : 1.20));
    out->noise = (float)noise;
    out->u     = (float)u;
    out->annual = site->annual_temp;
    out->air_amp = (float)signed_amp;
    if (site->kind == ICE_KIND_DRY || site->kind == ICE_KIND_FALL) out->cap = 0.0;
}

float qlcg_ice_load_capacity(float h, float q, float noise) {
    if (h <= 0.0f) return 0.0f;
    float k = h / .10f;
    return k * k * q * (.9f + .2f * noise);
}

uint8_t qlcg_ice_stage(float h, float q) {
    if (h <= .002f) return ICE_STAGE_NONE;
    if (q < .45f) return ICE_STAGE_ROTTEN;
    if (h < .008f) return ICE_STAGE_FILM;
    if (h < .07f) return ICE_STAGE_THIN;
    if (h < .15f) return ICE_STAGE_WALKABLE;
    return ICE_STAGE_THICK;
}

const char *qlcg_ice_stage_name(uint8_t stage) {
    switch (stage) {
        case ICE_STAGE_FILM:     return "ice.film";
        case ICE_STAGE_THIN:     return "ice.thin";
        case ICE_STAGE_WALKABLE: return "ice.walkable";
        case ICE_STAGE_THICK:    return "ice.thick";
        case ICE_STAGE_ROTTEN:   return "ice.rotten";
        default:                 return "ice.none";
    }
}

void qlcg_ice_sample_params(const IceParams *p, const IceSite *site,
                            double climate_day, IceSample *out) {
    memset(out, 0, sizeof(*out));
    if (!p || !out) return;
    float depth = site ? site->depth : 0.0f;
    uint8_t kind = site ? site->kind : (uint8_t)ICE_KIND_DRY;
    double phi = ICE_OMEGA * climate_day + p->shift;
    double h = 0.0, q = 1.0, cold = 0.0, warm = 0.0;

    if (depth <= .02f || kind == ICE_KIND_DRY || kind == ICE_KIND_FALL) {
        h = 0.0;
    } else if (p->mean - p->amp >= 0.0) {
        h = 0.0;
    } else if (p->mean + p->amp <= 0.0) {
        h = p->cap;
    } else if (p->amp < 1e-10) {
        h = p->mean < 0.0 ? p->cap : 0.0;
    } else {
        IceCycle c = ice_cycle(p);
        double v = c.start + dmod(phi - c.start, ICE_TAU);
        if (v <= c.end) {
            cold = fmax(0.0, -primitive(p->mean, p->amp, c.start, v));
            h = fmin(p->cap, sqrt(c.h0 * c.h0 + p->k2 * fmax(0.0, cold - p->debt)));
            q = h > 1e-12 ? (c.h0 * c.old_q + fmax(0.0, h - c.h0)) / h : 1.0;
        } else {
            warm = fmax(0.0, primitive(p->mean, p->amp, c.end, v));
            h = fmax(0.0, c.peak - p->km * warm);
            q = fmax(.08, c.peak_q * exp(-warm / 18.0));
        }
    }

    h = clampd(h, 0.0, fmax(0.0, (double)depth));
    q = clampd(q, .08, 1.0);
    out->thickness  = (float)h;
    out->quality    = (float)q;
    out->coverage   = qlcg_ice_coverage((float)h, (float)depth);
    out->strength   = qlcg_ice_load_capacity((float)h, (float)q, p->noise);
    out->effective_temp = (float)(p->mean + p->amp * sin(phi));
    out->air_temp       = (float)((double)p->annual + (double)p->air_amp * sin(ICE_OMEGA * climate_day));
    out->cold_degree_days = cold;
    out->warm_degree_days = warm;
    out->stage = qlcg_ice_stage(out->thickness, out->quality);
    out->top    = site ? site->top : -1e9f;
    out->bottom = (site && site->top > -1e8f)
                ? fmaxf(site->bed, site->top - out->thickness) : -1e9f;
}

void qlcg_ice_evaluate(const IceSite *site, const IceTime *time, IceSample *out) {
    if (!site || !time || !out) return;
    IceParams p;
    qlcg_ice_params(site, &p);
    qlcg_ice_sample_params(&p, site, time->climate_day, out);
}

float qlcg_ice_coverage(float thickness, float depth) {
    if (thickness <= 0.0f) return 0.0f;
    /* 岸线由水深等高线决定，不是网格边 */
    return (float)(dstep(.004, .020, (double)thickness) *
                   dstep(QLCG_ICE_EDGE_LO, QLCG_ICE_EDGE_HI, (double)depth));
}

double qlcg_ice_integral(const IceSite *site, double t0, double t1, bool cold) {
    if (!(t1 > t0)) return 0.0;
    IceParams p;
    qlcg_ice_params(site, &p);
    double m = p.mean, sh = p.shift, a = p.amp;
    if (cold) { m = -m; sh += ICE_PI; }
    if (m - a >= 0.0) return fmax(0.0, primitive(m, a, ICE_OMEGA * t0 + sh, ICE_OMEGA * t1 + sh));
    if (m + a <= 0.0) return 0.0;
    double angle = asin(-m / a);
    double lo = angle, hi = ICE_PI - angle;
    double full = primitive(m, a, lo, hi);
    double acc0, acc1;
    /* 固定次数的周期分解，不逐日循环 */
    double x0 = ICE_OMEGA * t0 + sh;
    double n0 = floor((x0 - lo) / ICE_TAU);
    double r0 = x0 - n0 * ICE_TAU;
    if (r0 < lo) r0 = lo;
    acc0 = n0 * full + primitive(m, a, lo, fmin(r0, hi));
    double x1 = ICE_OMEGA * t1 + sh;
    double n1 = floor((x1 - lo) / ICE_TAU);
    double r1 = x1 - n1 * ICE_TAU;
    if (r1 < lo) r1 = lo;
    acc1 = n1 * full + primitive(m, a, lo, fmin(r1, hi));
    return fmax(0.0, acc1 - acc0);
}

double qlcg_ice_regrow(const IceSite *site, double broken_day, double now_day,
                       bool occupied, double *out_quality) {
    double quality = 1.0;
    if (out_quality) *out_quality = quality;
    if (!site) return 0.0;
    IceParams p;
    qlcg_ice_params(site, &p);
    IceSample base;
    qlcg_ice_sample_params(&p, site, now_day, &base);
    if (base.thickness <= 1e-6f) {
        if (out_quality) *out_quality = base.quality;
        return 0.0;
    }
    if (now_day <= broken_day) return 0.0;
    if (now_day - broken_day >= QLCG_ICE_PERIOD) {
        if (out_quality) *out_quality = base.quality;
        return base.thickness;
    }
    if (occupied) return 0.0;
    if (p.mean + p.amp <= 0.0) {
        /* 常年寒冷：只按冷度日生长，不扣整湖冷量 */
        double h = sqrt(p.k2 * qlcg_ice_integral(site, broken_day, now_day, true));
        h = fmin((double)base.thickness, h);
        if (out_quality) *out_quality = base.quality;
        return h;
    }
    if (p.mean - p.amp >= 0.0) return 0.0;

    IceCycle c = ice_cycle(&p);
    double cuts[6];
    int n = 0;
    cuts[n++] = broken_day;
    for (int k = 0; k < 2; k++) {
        double root = k ? c.end : c.start;
        double first = (root - p.shift) / ICE_OMEGA;
        double idx = floor((broken_day - first) / QLCG_ICE_PERIOD) + 1.0;
        double boundary = first + idx * QLCG_ICE_PERIOD;
        if (broken_day < boundary && boundary < now_day && n < 5) cuts[n++] = boundary;
    }
    cuts[n++] = now_day;
    for (int i = 1; i < n; i++) {           /* 至多 4 段，插入排序 */
        double v = cuts[i];
        int j = i - 1;
        while (j >= 0 && cuts[j] > v) { cuts[j + 1] = cuts[j]; j--; }
        cuts[j + 1] = v;
    }
    double h = 0.0, q = 1.0;
    for (int i = 0; i + 1 < n; i++) {
        double lo = cuts[i], hi = cuts[i + 1];
        if (!(hi > lo)) continue;
        double mid = (lo + hi) * 0.5;
        double temp = p.mean + p.amp * sin(ICE_OMEGA * mid + p.shift);
        if (temp < 0.0) {
            h = fmin(p.cap, sqrt(h * h + p.k2 * qlcg_ice_integral(site, lo, hi, true)));
        } else {
            double warm = qlcg_ice_integral(site, lo, hi, false);
            h = fmax(0.0, h - p.km * warm);
            q = h > 1e-6 ? fmax(.08, q * exp(-warm / 18.0)) : 1.0;
        }
    }
    h = fmin((double)base.thickness, h);
    if (out_quality) {
        *out_quality = h > 1e-6 ? clampd(q, .08, 1.0) : .08;
        if (h >= base.thickness) *out_quality = base.quality;
    }
    return h;
}

IceTime qlcg_ice_time(const CalendarClock *clk, const CalendarSnapshot *snap,
                      int forced_season) {
    IceTime t;
    memset(&t, 0, sizeof(t));
    if (!snap) return t;
    if (forced_season >= 0 && forced_season < 4) {
        t.forced = true;
        t.phase = (float)forced_season + .5f;
    } else {
        t.phase = snap->season_phase;
        if (t.phase < 0.0f) t.phase = 0.0f;
        if (t.phase >= 4.0f) t.phase = 3.999999f;
    }
    int cycle_year = 0;
    if (clk && clk->mode == CALENDAR_YUAN) {
        cycle_year = snap->yuan.year;
    } else {
        int y = 0, m = 0, d = 0;
        qlcg_jdn_to_gregorian(snap->season_start_jdn, &y, &m, &d);
        cycle_year = y;
    }
    t.climate_day = QLCG_ICE_PERIOD * (double)cycle_year + QLCG_ICE_PER_SEASON * (double)t.phase;
    return t;
}
