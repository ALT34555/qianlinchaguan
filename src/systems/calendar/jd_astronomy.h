/* 儒略日与日月位置，对齐 JulianDay.ts */
#ifndef QLCG_JD_ASTRONOMY_H
#define QLCG_JD_ASTRONOMY_H

#define QLCG_UNIX_EPOCH_JDN 2440588
#define QLCG_J2000          2451545.0
#define QLCG_MS_PER_DAY     86400000.0
#define QLCG_DAYS_PER_CENTURY 36525.0

int  qlcg_is_leap_year(int year);
int  qlcg_days_in_month(int year, int month);
int  qlcg_gregorian_to_jdn(int year, int month, int day);
void qlcg_jdn_to_gregorian(int jdn, int *year, int *month, int *day);
int  qlcg_weekday_of(int jdn);
double qlcg_norm360(double deg);
double qlcg_norm180(double deg);

double qlcg_julian_day_of(int jdn, double frac);
void   qlcg_day_time_of_julian_day(double jd, int *jdn, double *frac);
void   qlcg_day_time_from_unix_ms(double ms, int utc_offset_min, int *jdn, double *frac);
double qlcg_unix_ms_from_day_time(int jdn, double frac, int utc_offset_min);

double qlcg_delta_t_seconds(double decimal_year);
double qlcg_decimal_year_of_jde(double jde);
double qlcg_jde_to_jd_ut(double jde);
double qlcg_jd_ut_to_jde(double jd);
int    qlcg_civil_jdn_of_jd(double jd, int utc_offset_min);
int    qlcg_civil_jdn_of_jde(double jde, int utc_offset_min);

double qlcg_sin_deg(double deg);
double qlcg_cos_deg(double deg);

/* 月球地心视黄经（度） */
double qlcg_moon_longitude_deg(double jde);
/* 太阳视黄经（度） */
double qlcg_sun_apparent_longitude_deg(double jde);
/* 日月视黄经差（度，-180..180） */
double qlcg_lunar_elongation_deg(double jde);

/* 朔望月序号 */
int    qlcg_lunation_index_of(double jd);
/* 第 k 个朔的力学时儒略日 */
double qlcg_new_moon_jde(int k);
double qlcg_new_moon_jde_before(double jd);
double qlcg_new_moon_jde_on_or_after(double jd);

/* 太阳视黄经达到目标值的时刻 */
double qlcg_solar_term_jde(double target_longitude, double guess_jde);
double qlcg_winter_solstice_jde(int year);

typedef struct {
    double longitude;
    double jde;
    int    index;
} QlcgMajorTerm;

QlcgMajorTerm qlcg_next_major_term_jde(double jd);

#endif
