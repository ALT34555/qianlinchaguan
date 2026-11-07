/* 农历编历，对齐 ChineseCalendar.ts */
#ifndef QLCG_LUNAR_CALENDAR_H
#define QLCG_LUNAR_CALENDAR_H

#include <stdbool.h>

#define QLCG_CHINESE_TZ_MIN 480
#define QLCG_MAX_LUNAR_MONTHS 14

typedef struct {
    int    number;      /* 月序 1~12，闰月与所闰之月同号 */
    bool   is_leap;
    int    start_jdn;
    int    end_jdn;
    int    days;
    bool   has_major_term;
    char   name[24];
} QlcgLunarMonth;

typedef struct {
    int    solstice_year;
    QlcgLunarMonth months[QLCG_MAX_LUNAR_MONTHS];
    int    month_count;
    int    zhengyue_index;
} QlcgSuiTable;

typedef struct {
    int    index;        /* 小寒起 0..23 */
    double longitude;
    int    jdn;          /* 东八区当地日 */
    double frac;
    bool   is_major;
} QlcgSolarTerm;

typedef struct {
    int  jdn;
    int  year;
    int  month;
    int  day;
    bool is_leap_month;
    char month_name[24];
    char day_name[24];
    char year_gan_zhi[12];
    char day_gan_zhi[12];
    char month_gan_zhi[12];
    const char *zodiac;
    int  days_in_month;
    char solar_term[16];   /* 空串表示当日非节气 */
    int  next_term_days;
    char next_term_name[16];
} QlcgChineseDate;

void qlcg_lunar_reset(void);

/* 某公历年二十四节气（小寒起） */
const QlcgSolarTerm *qlcg_solar_terms_of_year(int year);

/* 由日序求农历日期 */
bool qlcg_lunar_from_jdn(int jdn, QlcgChineseDate *out);
bool qlcg_lunar_from_unix_ms(double ms, QlcgChineseDate *out);

/* 农历年正月初一的公历日序 */
int qlcg_spring_festival_jdn(int lunar_year);

#endif
