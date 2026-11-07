/* 农历编历实现 */
#pragma float_control(precise, on)
#pragma fp_contract(off)

#include "lunar_calendar.h"
#include "jd_astronomy.h"
#include "gan_zhi.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TERM_CACHE 512
#define SUI_CACHE  512

typedef struct { int year; bool ready; QlcgSolarTerm terms[24]; } TermSlot;
typedef struct { int year; bool ready; QlcgSuiTable sui; } SuiSlot;

static TermSlot g_terms[TERM_CACHE];
static SuiSlot  g_sui[SUI_CACHE];

void qlcg_lunar_reset(void) {
    memset(g_terms, 0, sizeof(g_terms));
    memset(g_sui, 0, sizeof(g_sui));
}

/* 24 节气黄经：小寒 285° 起，每 15° */
static double term_longitude(int index) {
    return qlcg_norm360(285.0 + 15.0 * (double)index);
}

const QlcgSolarTerm *qlcg_solar_terms_of_year(int year) {
    int slot = ((year % TERM_CACHE) + TERM_CACHE) % TERM_CACHE;
    TermSlot *s = &g_terms[slot];
    if (s->ready && s->year == year) return s->terms;

    double base_guess = (double)qlcg_gregorian_to_jdn(year, 1, 6) + 0.5;
    for (int i = 0; i < 24; i++) {
        double lon = term_longitude(i);
        double jde = qlcg_solar_term_jde(lon, base_guess + (double)i * 15.2184);
        double local = qlcg_jde_to_jd_ut(jde) + 0.5 + (double)QLCG_CHINESE_TZ_MIN / 1440.0;
        int jdn = (int)floor(local);
        s->terms[i].index = i;
        s->terms[i].longitude = lon;
        s->terms[i].jdn = jdn;
        s->terms[i].frac = local - (double)jdn;
        s->terms[i].is_major = (i % 2) == 1;
    }
    s->year = year;
    s->ready = true;
    return s->terms;
}

/* 朔所在的东八区初一日序 */
static int new_moon_jdn(int k) {
    return qlcg_civil_jdn_of_jde(qlcg_new_moon_jde(k), QLCG_CHINESE_TZ_MIN);
}

/* 不晚于某日序的最后一次朔的序号 */
static int lunation_index_before(int jdn) {
    int k = qlcg_lunation_index_of((double)jdn + 0.5);
    while (new_moon_jdn(k) > jdn) k -= 1;
    while (new_moon_jdn(k + 1) <= jdn) k += 1;
    return k;
}

/* 某月是否含中气 */
static bool contains_major_term(const QlcgLunarMonth *m) {
    double start_jde = qlcg_jd_ut_to_jde((double)m->start_jdn - 0.5
                       - (double)QLCG_CHINESE_TZ_MIN / 1440.0);
    QlcgMajorTerm t = qlcg_next_major_term_jde(start_jde);
    int zq = qlcg_civil_jdn_of_jde(t.jde, QLCG_CHINESE_TZ_MIN);
    return (zq >= m->start_jdn && zq < m->end_jdn);
}

static const char *const LUNAR_MONTH_NAMES[] = {
    "正月", "二月", "三月", "四月", "五月", "六月",
    "七月", "八月", "九月", "十月", "冬月", "腊月",
};

const QlcgSuiTable *qlcg_sui_table(int solstice_year);

const QlcgSuiTable *qlcg_sui_table(int solstice_year) {
    int slot = ((solstice_year % SUI_CACHE) + SUI_CACHE) % SUI_CACHE;
    SuiSlot *s = &g_sui[slot];
    if (s->ready && s->year == solstice_year) return &s->sui;

    QlcgSuiTable *sui = &s->sui;
    memset(sui, 0, sizeof(*sui));
    sui->solstice_year = solstice_year;

    int ws_jdn = qlcg_civil_jdn_of_jde(qlcg_winter_solstice_jde(solstice_year), QLCG_CHINESE_TZ_MIN);
    int start_k = lunation_index_before(ws_jdn);
    int next_ws_jdn = qlcg_civil_jdn_of_jde(qlcg_winter_solstice_jde(solstice_year + 1), QLCG_CHINESE_TZ_MIN);
    int end_k = lunation_index_before(next_ws_jdn);

    int count = 0;
    for (int k = start_k; k < end_k && count < QLCG_MAX_LUNAR_MONTHS; k++) {
        QlcgLunarMonth *m = &sui->months[count];
        m->start_jdn = new_moon_jdn(k);
        m->end_jdn = new_moon_jdn(k + 1);
        m->days = m->end_jdn - m->start_jdn;
        m->number = 0;
        m->is_leap = false;
        m->has_major_term = false;
        m->name[0] = '\0';
        count++;
    }
    sui->month_count = count;
    for (int i = 0; i < count; i++) sui->months[i].has_major_term = contains_major_term(&sui->months[i]);

    int leap_index = -1;
    if (count == 13) {
        for (int i = 1; i < count; i++) {
            if (!sui->months[i].has_major_term) { leap_index = i; break; }
        }
        if (leap_index < 0) leap_index = -1;
    }

    int next_number = 11;
    int previous_number = 11;
    for (int i = 0; i < count; i++) {
        QlcgLunarMonth *m = &sui->months[i];
        if (i == leap_index) {
            m->is_leap = true;
            m->number = previous_number;
        } else {
            m->number = ((next_number - 1) % 12) + 1;
            previous_number = m->number;
            next_number += 1;
        }
        snprintf(m->name, sizeof(m->name), "%s%s", m->is_leap ? "闰" : "",
                 LUNAR_MONTH_NAMES[m->number - 1]);
    }

    sui->zhengyue_index = -1;
    for (int i = 0; i < count; i++) {
        if (sui->months[i].number == 1 && !sui->months[i].is_leap) { sui->zhengyue_index = i; break; }
    }
    s->year = solstice_year;
    s->ready = true;
    return sui;
}

/* 由日序定位农历月 */
static bool find_month(int jdn, QlcgLunarMonth *out_month, int *out_lunar_year) {
    int g = 0, m = 0, d = 0;
    qlcg_jdn_to_gregorian(jdn, &g, &m, &d);
    for (int pass = 0; pass < 2; pass++) {
        int sy = (pass == 0) ? g - 1 : g;
        const QlcgSuiTable *sui = qlcg_sui_table(sy);
        if (!sui || sui->month_count <= 0) continue;
        for (int i = 0; i < sui->month_count; i++) {
            const QlcgLunarMonth *mm = &sui->months[i];
            if (jdn >= mm->start_jdn && jdn < mm->end_jdn) {
                *out_month = *mm;
                *out_lunar_year = (i < sui->zhengyue_index) ? sy : sy + 1;
                return true;
            }
        }
    }
    return false;
}

int qlcg_spring_festival_jdn(int lunar_year) {
    const QlcgSuiTable *sui = qlcg_sui_table(lunar_year - 1);
    if (!sui || sui->zhengyue_index < 0) return 0;
    return sui->months[sui->zhengyue_index].start_jdn;
}

/* 月柱干支：以节为界，年柱以立春为界 */
static void month_gan_zhi_of(int jdn, char *buf, int cap) {
    int g = 0, m = 0, d = 0;
    qlcg_jdn_to_gregorian(jdn, &g, &m, &d);

    int best_offset = -1;
    int best_jdn = -1;
    for (int pass = 0; pass < 3; pass++) {
        int year = g - 1 + pass;
        const QlcgSolarTerm *terms = qlcg_solar_terms_of_year(year);
        for (int i = 0; i < 24; i++) {
            if (terms[i].is_major) continue;
            int offset = ((((int)floor(((terms[i].longitude - 315.0) / 30.0) + 0.5) % 12) + 12) % 12);
            if (terms[i].jdn <= jdn && terms[i].jdn > best_jdn) {
                best_jdn = terms[i].jdn;
                best_offset = offset;
            }
        }
    }
    if (best_offset < 0) best_offset = 0;

    const QlcgSolarTerm *this_year = qlcg_solar_terms_of_year(g);
    int li_chun_jdn = this_year[2].jdn;
    int year_for_stem = (jdn >= li_chun_jdn) ? g : g - 1;
    int year_stem = (((year_for_stem - 4) % 10) + 10) % 10;
    int first_month_stem = ((year_stem % 5) * 2 + 2) % 10;
    snprintf(buf, (size_t)cap, "%s%s", qlcg_stem_name((first_month_stem + best_offset) % 10),
             qlcg_branch_name(2 + best_offset));
}

bool qlcg_lunar_from_jdn(int jdn, QlcgChineseDate *out) {
    memset(out, 0, sizeof(*out));
    QlcgLunarMonth mon;
    int lunar_year = 0;
    if (!find_month(jdn, &mon, &lunar_year)) return false;

    out->jdn = jdn;
    out->year = lunar_year;
    out->month = mon.number;
    out->day = jdn - mon.start_jdn + 1;
    out->is_leap_month = mon.is_leap;
    snprintf(out->month_name, sizeof(out->month_name), "%s", mon.name);
    qlcg_chinese_day_name(out->day, out->day_name, (int)sizeof(out->day_name));
    qlcg_gan_zhi_name(qlcg_year_gan_zhi_index(lunar_year), out->year_gan_zhi, (int)sizeof(out->year_gan_zhi));
    qlcg_day_gan_zhi_name(jdn, out->day_gan_zhi, (int)sizeof(out->day_gan_zhi));
    month_gan_zhi_of(jdn, out->month_gan_zhi, (int)sizeof(out->month_gan_zhi));
    out->zodiac = qlcg_zodiac_of_year(lunar_year);
    out->days_in_month = mon.days;

    /* 当日节气 */
    int g = 0, m = 0, d = 0;
    qlcg_jdn_to_gregorian(jdn, &g, &m, &d);
    const QlcgSolarTerm *terms = qlcg_solar_terms_of_year(g);
    for (int i = 0; i < 24; i++) {
        if (terms[i].jdn == jdn) {
            snprintf(out->solar_term, sizeof(out->solar_term), "%s", qlcg_solar_term_name(i));
            break;
        }
    }

    /* 下一节气 */
    int next_i = -1, next_jdn = 1 << 30;
    for (int i = 0; i < 24; i++) {
        if (terms[i].jdn > jdn && terms[i].jdn < next_jdn) { next_jdn = terms[i].jdn; next_i = i; }
    }
    if (next_i < 0) {
        const QlcgSolarTerm *nt = qlcg_solar_terms_of_year(g + 1);
        next_jdn = nt[0].jdn;
        next_i = 0;
    }
    snprintf(out->next_term_name, sizeof(out->next_term_name), "%s", qlcg_solar_term_name(next_i));
    out->next_term_days = next_jdn - jdn;
    return true;
}

bool qlcg_lunar_from_unix_ms(double ms, QlcgChineseDate *out) {
    int jdn = 0;
    double frac = 0.0;
    qlcg_day_time_from_unix_ms(ms, QLCG_CHINESE_TZ_MIN, &jdn, &frac);
    return qlcg_lunar_from_jdn(jdn, out);
}
