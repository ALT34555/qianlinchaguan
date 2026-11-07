#include "climate_season.h"
#include "../calendar/jd_astronomy.h"
#include "../../core/math_util.h"
#include <math.h>
#include <string.h>

#define CLIMATE_PI 3.14159265358979323846
#define CLIMATE_YEAR 365.2422f
#define WINTER_MAX 8.0f
#define SUMMER_MIN 15.0f

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float wrap4(float p) {
    p = fmodf(p, 4.0f);
    return p < 0.0f ? p + 4.0f : p;
}

float qlcg_climate_amp_scale(float tilt_deg) {
    float t = clampf(tilt_deg, 0.0f, 60.0f);
    if (t < 1e-4f) return 0.0f;
    return (float)(sin((double)t * CLIMATE_PI / 180.0) /
                   sin(23.5 * CLIMATE_PI / 180.0));
}

float qlcg_climate_orbit_phase(const CalendarSnapshot *date) {
    if (!date) return 0.0f;
    double jd = qlcg_julian_day_of(date->jdn, date->day_frac)
              - (double)date->utc_offset_min / 1440.0;
    static double cached_jd;
    static float cached_phase;
    static int cached;
    if (cached && fabs(jd - cached_jd) < 1e-6) return cached_phase;
    double lon = qlcg_sun_apparent_longitude_deg(jd);
    cached_phase = wrap4((float)(lon / 90.0));
    cached_jd = jd;
    cached = 1;
    return cached_phase;
}

/* 温度阈值交叉相位；warming=1 取升温交点 */
static float threshold_phase(double mean, double amp, double thr,
                             int warming, float phase, int forward) {
    if (amp < 1e-6) return NAN;
    double c = (thr - mean) / amp;
    if (c < -1.0 || c > 1.0) return NAN;
    double x = acos(qlcg_clampf((float)c, -1.0f, 1.0f));
    double raw = warming ? 1.0 + (2.0 * CLIMATE_PI - x) / (CLIMATE_PI * 0.5)
                         : 1.0 + x / (CLIMATE_PI * 0.5);
    raw = fmod(raw, 4.0);
    if (raw < 0.0) raw += 4.0;
    float best = 0.0f;
    int found = 0;
    for (int k = -1; k <= 1; k++) {
        float p = (float)(raw + 4.0 * k);
        if (forward) {
            if (p <= phase + 1e-4f) continue;
            if (!found || p < best) { best = p; found = 1; }
        } else {
            if (p >= phase - 1e-4f) continue;
            if (!found || p > best) { best = p; found = 1; }
        }
    }
    return found ? best : NAN;
}

static float cell_lat(const MacroWorld *w, int cell) {
    if(qlcg_world_flat(w)) return 30.0f;
    int row = cell / w->columns;
    return (0.5f - ((float)row + 0.5f) / (float)w->rows) * 180.0f;
}

void qlcg_climate_season_at(const MacroWorld *w, int cell,
                            const CalendarSnapshot *date, QlcgClimateSeason *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->season = 1;
    out->seasonless = 1;
    if (!w || !w->columns || !w->rows || cell < 0 || cell >= w->cell_count) return;

    float lat = cell_lat(w, cell);
    float mean = w->temperature ? w->temperature[cell] : 15.0f;
    float amp = w->season_amp ? w->season_amp[cell] : 0.0f;
    float monsoon = w->monsoon_amp ? w->monsoon_amp[cell] : 0.0f;
    float base_wet = w->moisture ? w->moisture[cell] : 0.5f;

    out->amplitude = amp;
    out->monsoon = clampf(monsoon, 0.0f, 1.0f);

    if (!date) {
        out->temperature = mean;
        out->moisture = clampf(base_wet, 0.0f, 1.0f);
        out->season = mean >= WINTER_MAX ? 1 : 3;
        return;
    }

    float orbit = qlcg_climate_orbit_phase(date);
    float lag = 18.0f + 12.0f * (1.0f - clampf(amp / 18.0f, 0.0f, 1.0f));
    lag -= 5.0f * out->monsoon;
    lag = clampf(lag, 10.0f, 30.0f);
    float phase = wrap4(orbit - lag * 4.0f / CLIMATE_YEAR);
    if (lat < 0.0f) phase = wrap4(phase + 2.0f);

    out->phase = phase;
    out->temperature = mean + amp * cosf((phase - 1.0f) * (float)CLIMATE_PI * 0.5f);
    out->moisture = clampf(base_wet + 0.30f * out->monsoon *
                           cosf((phase - 1.25f) * (float)CLIMATE_PI * 0.5f), 0.0f, 1.0f);

    if (amp < 0.5f) {
        out->seasonless = 1;
        out->season = out->temperature >= WINTER_MAX ? 1 : 3;
        return;
    }

    float tmin = mean - amp, tmax = mean + amp;
    if (tmax < WINTER_MAX) {
        out->seasonless = 1;
        out->season = 3;
        return;
    }
    if (tmin > SUMMER_MIN) {
        out->seasonless = 1;
        out->season = 1;
        return;
    }
    if (tmin >= WINTER_MAX && tmax <= SUMMER_MIN) {
        out->seasonless = 1;
        out->season = 1;
        return;
    }

    out->seasonless = 0;
    int warming = -sinf((phase - 1.0f) * (float)CLIMATE_PI * 0.5f) > 0.0f;
    if (out->temperature < WINTER_MAX) out->season = 3;
    else if (out->temperature < SUMMER_MIN) out->season = warming ? 0 : 2;
    else out->season = 1;

    float lo = NAN, hi = NAN;
    switch (out->season) {
        case 3:
            lo = threshold_phase(mean, amp, WINTER_MAX, 0, phase, 0);
            hi = threshold_phase(mean, amp, WINTER_MAX, 1, phase, 1);
            break;
        case 0:
            lo = threshold_phase(mean, amp, WINTER_MAX, 1, phase, 0);
            hi = threshold_phase(mean, amp, SUMMER_MIN, 1, phase, 1);
            break;
        case 2:
            lo = threshold_phase(mean, amp, SUMMER_MIN, 0, phase, 0);
            hi = threshold_phase(mean, amp, WINTER_MAX, 0, phase, 1);
            break;
        default:
            lo = threshold_phase(mean, amp, SUMMER_MIN, 1, phase, 0);
            hi = threshold_phase(mean, amp, SUMMER_MIN, 0, phase, 1);
            break;
    }
    if (!isfinite(lo) || !isfinite(hi)) {
        out->seasonless = 1;
        return;
    }
    out->since_days = (phase - lo) * CLIMATE_YEAR * 0.25f;
    out->until_days = (hi - phase) * CLIMATE_YEAR * 0.25f;
    if (out->since_days < 0.0f) out->since_days += CLIMATE_YEAR;
    if (out->until_days < 0.0f) out->until_days += CLIMATE_YEAR;
}

int qlcg_climate_season_index(const MacroWorld *w, int cell,
                              const CalendarSnapshot *date) {
    QlcgClimateSeason cs;
    qlcg_climate_season_at(w, cell, date, &cs);
    return cs.season;
}
