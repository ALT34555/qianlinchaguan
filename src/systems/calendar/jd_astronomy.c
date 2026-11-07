/* 儒略日与日月位置实现 */
#pragma float_control(precise, on)
#pragma fp_contract(off)

#include "jd_astronomy.h"

#include <math.h>

#define D2R 0.017453292519943295

int qlcg_is_leap_year(int year) {
    return ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
}

int qlcg_days_in_month(int year, int month) {
    if (month == 2) return qlcg_is_leap_year(year) ? 29 : 28;
    if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
    return 31;
}

int qlcg_gregorian_to_jdn(int year, int month, int day) {
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
}

void qlcg_jdn_to_gregorian(int jdn, int *year, int *month, int *day) {
    int a = jdn + 32044;
    int b = (4 * a + 3) / 146097;
    int c = a - (146097 * b) / 4;
    int d = (4 * c + 3) / 1461;
    int e = c - (1461 * d) / 4;
    int m = (5 * e + 2) / 153;
    *day = e - (153 * m + 2) / 5 + 1;
    *month = m + 3 - 12 * (m / 10);
    *year = 100 * b + d - 4800 + m / 10;
}

int qlcg_weekday_of(int jdn) {
    return (((jdn + 1) % 7) + 7) % 7;
}

double qlcg_norm360(double deg) {
    double r = fmod(deg, 360.0);
    return r < 0.0 ? r + 360.0 : r;
}

double qlcg_norm180(double deg) {
    double r = qlcg_norm360(deg);
    return r > 180.0 ? r - 360.0 : r;
}

double qlcg_julian_day_of(int jdn, double frac) {
    return (double)jdn - 0.5 + frac;
}

void qlcg_day_time_of_julian_day(double jd, int *jdn, double *frac) {
    double shifted = jd + 0.5;
    double f = floor(shifted);
    *jdn = (int)f;
    *frac = shifted - f;
}

void qlcg_day_time_from_unix_ms(double ms, int utc_offset_min, int *jdn, double *frac) {
    double jd = ms / QLCG_MS_PER_DAY + (double)QLCG_UNIX_EPOCH_JDN - 0.5;
    qlcg_day_time_of_julian_day(jd + (double)utc_offset_min / 1440.0, jdn, frac);
}

double qlcg_unix_ms_from_day_time(int jdn, double frac, int utc_offset_min) {
    double jd = qlcg_julian_day_of(jdn, frac) - (double)utc_offset_min / 1440.0;
    return floor((jd - ((double)QLCG_UNIX_EPOCH_JDN - 0.5)) * QLCG_MS_PER_DAY + 0.5);
}

/* ΔT：分段拟合，对齐 base */
double qlcg_delta_t_seconds(double y) {
    if (y < -500.0) {
        double u = (y - 1820.0) / 100.0;
        return -20.0 + 32.0 * u * u;
    }
    if (y < 500.0) {
        double u = y / 100.0;
        return 10583.6 - 1014.41 * u + 33.78311 * pow(u, 2) - 5.952053 * pow(u, 3)
             - 0.1798452 * pow(u, 4) + 0.022174192 * pow(u, 5) + 0.0090316521 * pow(u, 6);
    }
    if (y < 1600.0) {
        double u = (y - 1000.0) / 100.0;
        return 1574.2 - 556.01 * u + 71.23472 * pow(u, 2) + 0.319781 * pow(u, 3)
             - 0.8503463 * pow(u, 4) - 0.005050998 * pow(u, 5) + 0.0083572073 * pow(u, 6);
    }
    if (y < 1700.0) {
        double t = y - 1600.0;
        return 120.0 - 0.9808 * t - 0.01532 * t * t + (t * t * t) / 7129.0;
    }
    if (y < 1800.0) {
        double t = y - 1700.0;
        return 8.83 + 0.1603 * t - 0.0059285 * t * t + 0.00013336 * pow(t, 3) - pow(t, 4) / 1174000.0;
    }
    if (y < 1860.0) {
        double t = y - 1800.0;
        return 13.72 - 0.332447 * t + 0.0068612 * t * t + 0.0041116 * pow(t, 3)
             - 0.00037436 * pow(t, 4) + 0.0000121272 * pow(t, 5)
             - 0.0000001699 * pow(t, 6) + 0.000000000875 * pow(t, 7);
    }
    if (y < 1900.0) {
        double t = y - 1860.0;
        return 7.62 + 0.5737 * t - 0.251754 * t * t + 0.01680668 * pow(t, 3)
             - 0.0004473624 * pow(t, 4) + pow(t, 5) / 233174.0;
    }
    if (y < 1920.0) {
        double t = y - 1900.0;
        return -2.79 + 1.494119 * t - 0.0598939 * t * t + 0.0061966 * pow(t, 3) - 0.000197 * pow(t, 4);
    }
    if (y < 1941.0) {
        double t = y - 1920.0;
        return 21.2 + 0.84493 * t - 0.0761 * t * t + 0.0020936 * pow(t, 3);
    }
    if (y < 1961.0) {
        double t = y - 1950.0;
        return 29.07 + 0.407 * t - (t * t) / 233.0 + (t * t * t) / 2547.0;
    }
    if (y < 1986.0) {
        double t = y - 1975.0;
        return 45.45 + 1.067 * t - (t * t) / 260.0 - (t * t * t) / 718.0;
    }
    if (y < 2005.0) {
        double t = y - 2000.0;
        return 63.86 + 0.3345 * t - 0.060374 * t * t + 0.0017275 * pow(t, 3)
             + 0.000651814 * pow(t, 4) + 0.00002373599 * pow(t, 5);
    }
    if (y < 2050.0) {
        double t = y - 2000.0;
        return 62.92 + 0.32217 * t + 0.005589 * t * t;
    }
    if (y < 2150.0) {
        double u = (y - 1820.0) / 100.0;
        return -20.0 + 32.0 * u * u - 0.5628 * (2150.0 - y);
    }
    {
        double u = (y - 1820.0) / 100.0;
        return -20.0 + 32.0 * u * u;
    }
}

double qlcg_decimal_year_of_jde(double jde) {
    return 2000.0 + (jde - QLCG_J2000) / 365.25;
}

double qlcg_jde_to_jd_ut(double jde) {
    return jde - qlcg_delta_t_seconds(qlcg_decimal_year_of_jde(jde)) / 86400.0;
}

double qlcg_jd_ut_to_jde(double jd) {
    double guess = jd + qlcg_delta_t_seconds(2000.0 + (jd - QLCG_J2000) / 365.25) / 86400.0;
    return jd + qlcg_delta_t_seconds(qlcg_decimal_year_of_jde(guess)) / 86400.0;
}

int qlcg_civil_jdn_of_jd(double jd, int utc_offset_min) {
    double local = jd + 0.5 + (double)utc_offset_min / 1440.0;
    return (int)floor(local);
}

int qlcg_civil_jdn_of_jde(double jde, int utc_offset_min) {
    return qlcg_civil_jdn_of_jd(qlcg_jde_to_jd_ut(jde), utc_offset_min);
}

double qlcg_sin_deg(double deg) { return sin(deg * D2R); }
double qlcg_cos_deg(double deg) { return cos(deg * D2R); }

/* 月球黄经周期项：[D, M, Mp, F, 系数] */
static const int MOON_TERMS[][5] = {
    { 0,0,1,0,6288774 }, { 2,0,-1,0,1274027 }, { 2,0,0,0,658314 }, { 0,0,2,0,213618 },
    { 0,1,0,0,-185116 }, { 0,0,0,2,-114332 }, { 2,0,-2,0,58793 }, { 2,-1,-1,0,57066 },
    { 2,0,1,0,53322 }, { 2,-1,0,0,45758 }, { 0,1,-1,0,-40923 }, { 1,0,0,0,-34720 },
    { 0,1,1,0,-30383 }, { 2,0,0,-2,15327 }, { 0,0,1,2,-12528 }, { 0,0,1,-2,10980 },
    { 4,0,-1,0,10675 }, { 0,0,3,0,10034 }, { 4,0,-2,0,8548 }, { 2,1,-1,0,-7888 },
    { 2,1,0,0,-6766 }, { 1,0,-1,0,-5163 }, { 1,1,0,0,4987 }, { 2,-1,1,0,4036 },
    { 2,0,2,0,3994 }, { 4,0,0,0,3861 }, { 2,0,-3,0,3665 }, { 0,1,-2,0,-2689 },
    { 2,0,-1,2,-2602 }, { 2,-1,-2,0,2390 }, { 1,0,1,0,-2348 }, { 2,-2,0,0,2236 },
    { 0,1,2,0,-2120 }, { 0,2,0,0,-2069 }, { 2,-2,-1,0,2048 }, { 2,0,1,-2,-1773 },
    { 2,0,0,2,-1595 }, { 4,-1,-1,0,1215 }, { 0,0,2,2,-1110 }, { 3,0,-1,0,-892 },
    { 2,1,1,0,-810 }, { 4,-1,-2,0,759 }, { 0,2,-1,0,-713 }, { 2,2,-1,0,-700 },
    { 2,1,-2,0,691 }, { 2,-1,0,-2,596 }, { 4,0,1,0,549 }, { 0,0,4,0,537 },
    { 4,-1,0,0,520 }, { 1,0,-2,0,-487 }, { 2,1,0,-2,-399 }, { 0,0,2,-2,-381 },
    { 1,1,1,0,351 }, { 3,0,-2,0,-340 }, { 4,0,-3,0,330 }, { 2,-1,2,0,327 },
    { 0,2,1,0,-323 }, { 1,1,-1,0,299 }, { 2,0,3,0,294 },
};

double qlcg_moon_longitude_deg(double jde) {
    double T = (jde - QLCG_J2000) / QLCG_DAYS_PER_CENTURY;
    double lp = 218.3164477 + 481267.88123421 * T - 0.0015786 * T * T
              + pow(T, 3) / 538841.0 - pow(T, 4) / 65194000.0;
    double d = 297.8501921 + 445267.1114034 * T - 0.0018819 * T * T
             + pow(T, 3) / 545868.0 - pow(T, 4) / 113065000.0;
    double m = 357.5291092 + 35999.0502909 * T - 0.0001536 * T * T + pow(T, 3) / 24490000.0;
    double mp = 134.9633964 + 477198.8675055 * T + 0.0087414 * T * T
              + pow(T, 3) / 69699.0 - pow(T, 4) / 14712000.0;
    double f = 93.272095 + 483202.0175233 * T - 0.0036539 * T * T
             - pow(T, 3) / 3526000.0 + pow(T, 4) / 863310000.0;

    double e = 1.0 - 0.002516 * T - 0.0000074 * T * T;
    double e2 = e * e;
    double a1 = 119.75 + 131.849 * T;
    double a2 = 53.09 + 479264.29 * T;

    double sum = 3958.0 * qlcg_sin_deg(a1)
               + 1962.0 * qlcg_sin_deg(lp - f)
               + 318.0 * qlcg_sin_deg(a2);

    int n = (int)(sizeof(MOON_TERMS) / sizeof(MOON_TERMS[0]));
    for (int i = 0; i < n; i++) {
        const int *t = MOON_TERMS[i];
        double arg = (double)t[0] * d + (double)t[1] * m + (double)t[2] * mp + (double)t[3] * f;
        double coeff = (double)t[4];
        if (t[1] == 1 || t[1] == -1) coeff *= e;
        else if (t[1] == 2 || t[1] == -2) coeff *= e2;
        sum += coeff * qlcg_sin_deg(arg);
    }
    return qlcg_norm360(lp + sum * 1e-6);
}

double qlcg_sun_apparent_longitude_deg(double jde) {
    double T = (jde - QLCG_J2000) / QLCG_DAYS_PER_CENTURY;
    double L0 = 280.46646 + 36000.76983 * T + 0.0003032 * T * T;
    double M = 357.52911 + 35999.05029 * T - 0.0001537 * T * T;
    double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * qlcg_sin_deg(M)
             + (0.019993 - 0.000101 * T) * qlcg_sin_deg(2.0 * M)
             + 0.000289 * qlcg_sin_deg(3.0 * M);
    double true_lon = L0 + C;
    double omega = 125.04 - 1934.136 * T;
    return qlcg_norm360(true_lon - 0.00569 - 0.00478 * qlcg_sin_deg(omega));
}

double qlcg_lunar_elongation_deg(double jde) {
    return qlcg_norm180(qlcg_moon_longitude_deg(jde) - qlcg_sun_apparent_longitude_deg(jde));
}

#define MEAN_LUNATION_DAYS 29.530588861
#define NEW_MOON_EPOCH_JDE 2451550.25981
#define MEAN_ELONGATION_RATE 12.190749
#define MEAN_SUN_RATE 0.98564736

int qlcg_lunation_index_of(double jd) {
    return (int)floor((jd - NEW_MOON_EPOCH_JDE) / MEAN_LUNATION_DAYS + 0.5);
}

double qlcg_new_moon_jde(int k) {
    double jde = NEW_MOON_EPOCH_JDE + MEAN_LUNATION_DAYS * (double)k;
    for (int i = 0; i < 12; i++) {
        double diff = qlcg_lunar_elongation_deg(jde);
        jde -= diff / MEAN_ELONGATION_RATE;
        if (fabs(diff) < 1e-7) break;
    }
    return jde;
}

double qlcg_new_moon_jde_before(double jd) {
    int k = qlcg_lunation_index_of(jd);
    double jde = qlcg_new_moon_jde(k);
    while (jde > jd) { k -= 1; jde = qlcg_new_moon_jde(k); }
    while (qlcg_new_moon_jde(k + 1) <= jd) { k += 1; jde = qlcg_new_moon_jde(k); }
    return jde;
}

double qlcg_new_moon_jde_on_or_after(double jd) {
    int k = qlcg_lunation_index_of(jd);
    double jde = qlcg_new_moon_jde(k);
    while (jde < jd) { k += 1; jde = qlcg_new_moon_jde(k); }
    while (qlcg_new_moon_jde(k - 1) >= jd) { k -= 1; jde = qlcg_new_moon_jde(k); }
    return jde;
}

double qlcg_solar_term_jde(double target_longitude, double guess_jde) {
    double target = qlcg_norm360(target_longitude);
    double jde = guess_jde;
    for (int i = 0; i < 20; i++) {
        double diff = qlcg_norm180(qlcg_sun_apparent_longitude_deg(jde) - target);
        jde -= diff / MEAN_SUN_RATE;
        if (fabs(diff) < 1e-8) break;
    }
    return jde;
}

double qlcg_winter_solstice_jde(int year) {
    return qlcg_solar_term_jde(270.0, (double)qlcg_gregorian_to_jdn(year, 12, 21) + 0.5);
}

QlcgMajorTerm qlcg_next_major_term_jde(double jd) {
    QlcgMajorTerm out;
    double lon = qlcg_sun_apparent_longitude_deg(jd);
    int k = (int)ceil((lon - 270.0) / 30.0 - 1e-9);
    double target = qlcg_norm360(270.0 + 30.0 * (double)k);
    double guess = jd + qlcg_norm360(target - lon) / MEAN_SUN_RATE;
    out.longitude = target;
    out.jde = qlcg_solar_term_jde(target, guess);
    out.index = ((k % 12) + 12) % 12;
    return out;
}
