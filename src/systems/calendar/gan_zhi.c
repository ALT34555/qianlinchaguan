/* 干支与日名实现 */
#include "gan_zhi.h"

#include <stdio.h>
#include <string.h>

static const char *const STEMS[] = { "甲","乙","丙","丁","戊","己","庚","辛","壬","癸" };
static const char *const BRANCHES[] = { "子","丑","寅","卯","辰","巳","午","未","申","酉","戌","亥" };
static const char *const ZODIAC[] = { "鼠","牛","虎","兔","龙","蛇","马","羊","猴","鸡","狗","猪" };
static const char *const DIGITS[] = { "〇","一","二","三","四","五","六","七","八","九" };

static const char *const SOLAR_TERMS[] = {
    "小寒","大寒","立春","雨水","惊蛰","春分","清明","谷雨",
    "立夏","小满","芒种","夏至","小暑","大暑","立秋","处暑",
    "白露","秋分","寒露","霜降","立冬","小雪","大雪","冬至",
};
static const char *const SEASONS[] = { "春","夏","秋","冬" };

const char *qlcg_stem_name(int index) {
    return STEMS[((index % 10) + 10) % 10];
}

const char *qlcg_branch_name(int index) {
    return BRANCHES[((index % 12) + 12) % 12];
}

void qlcg_gan_zhi_name(int index, char *buf, int cap) {
    int i = ((index % 60) + 60) % 60;
    snprintf(buf, (size_t)cap, "%s%s", STEMS[i % 10], BRANCHES[i % 12]);
}

void qlcg_chinese_number(int n, char *buf, int cap) {
    if (n < 0) n = 0;
    if (n < 10) { snprintf(buf, (size_t)cap, "%s", DIGITS[n]); return; }
    if (n < 20) {
        if (n == 10) snprintf(buf, (size_t)cap, "十");
        else snprintf(buf, (size_t)cap, "十%s", DIGITS[n % 10]);
        return;
    }
    if (n < 100) {
        int tens = n / 10;
        int ones = n % 10;
        if (ones == 0) snprintf(buf, (size_t)cap, "%s十", DIGITS[tens]);
        else snprintf(buf, (size_t)cap, "%s十%s", DIGITS[tens], DIGITS[ones]);
        return;
    }
    snprintf(buf, (size_t)cap, "%d", n);
}

void qlcg_chinese_day_name(int day, char *buf, int cap) {
    if (day < 1) { snprintf(buf, (size_t)cap, "—"); return; }
    if (day < 10) { snprintf(buf, (size_t)cap, "初%s", DIGITS[day]); return; }
    /* base 对 day=10 越界，此处补"初十" */
    if (day == 10) { snprintf(buf, (size_t)cap, "初十"); return; }
    if (day < 20) {
        if (day == 10) snprintf(buf, (size_t)cap, "十");
        else snprintf(buf, (size_t)cap, "十%s", DIGITS[day % 10]);
        return;
    }
    if (day == 20) { snprintf(buf, (size_t)cap, "二十"); return; }
    if (day < 30) { snprintf(buf, (size_t)cap, "廿%s", DIGITS[day % 10]); return; }
    if (day == 30) { snprintf(buf, (size_t)cap, "三十"); return; }
    snprintf(buf, (size_t)cap, "%d", day);
}

int qlcg_year_gan_zhi_index(int year) {
    return (((year - 4) % 60) + 60) % 60;
}

const char *qlcg_zodiac_of_year(int year) {
    return ZODIAC[qlcg_year_gan_zhi_index(year) % 12];
}

int qlcg_day_gan_zhi_index(int jdn) {
    return (((jdn + 49) % 60) + 60) % 60;
}

void qlcg_day_gan_zhi_name(int jdn, char *buf, int cap) {
    qlcg_gan_zhi_name(qlcg_day_gan_zhi_index(jdn), buf, cap);
}

const char *qlcg_solar_term_name(int index) {
    return SOLAR_TERMS[((index % 24) + 24) % 24];
}

const char *qlcg_solar_term_name_at(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < 24; i++) if (strcmp(name, SOLAR_TERMS[i]) == 0) return SOLAR_TERMS[i];
    return NULL;
}

const char *qlcg_season_name(int index) {
    return SEASONS[((index % 4) + 4) % 4];
}
