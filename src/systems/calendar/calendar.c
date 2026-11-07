#include "calendar.h"
#include "jd_astronomy.h"
#include "lunar_calendar.h"
#include "gan_zhi.h"
#include "../../core/i18n.h"
#include "../../core/math_util.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* 节气与时辰保留中文；其余名称走 i18n 键表 */
static const char *const STEM_KEYS[10] = {
    "calendar.stem.0", "calendar.stem.1", "calendar.stem.2", "calendar.stem.3", "calendar.stem.4",
    "calendar.stem.5", "calendar.stem.6", "calendar.stem.7", "calendar.stem.8", "calendar.stem.9"
};
static const char *const BRANCH_KEYS[12] = {
    "calendar.branch.0", "calendar.branch.1", "calendar.branch.2", "calendar.branch.3",
    "calendar.branch.4", "calendar.branch.5", "calendar.branch.6", "calendar.branch.7",
    "calendar.branch.8", "calendar.branch.9", "calendar.branch.10", "calendar.branch.11"
};
static const char *const LUNAR_MONTH_KEYS[12] = {
    "calendar.lunar.month.1", "calendar.lunar.month.2", "calendar.lunar.month.3",
    "calendar.lunar.month.4", "calendar.lunar.month.5", "calendar.lunar.month.6",
    "calendar.lunar.month.7", "calendar.lunar.month.8", "calendar.lunar.month.9",
    "calendar.lunar.month.10", "calendar.lunar.month.11", "calendar.lunar.month.12"
};
static const char *const LUNAR_DAY_KEYS[30] = {
    "calendar.lunar.day.1", "calendar.lunar.day.2", "calendar.lunar.day.3",
    "calendar.lunar.day.4", "calendar.lunar.day.5", "calendar.lunar.day.6",
    "calendar.lunar.day.7", "calendar.lunar.day.8", "calendar.lunar.day.9",
    "calendar.lunar.day.10", "calendar.lunar.day.11", "calendar.lunar.day.12",
    "calendar.lunar.day.13", "calendar.lunar.day.14", "calendar.lunar.day.15",
    "calendar.lunar.day.16", "calendar.lunar.day.17", "calendar.lunar.day.18",
    "calendar.lunar.day.19", "calendar.lunar.day.20", "calendar.lunar.day.21",
    "calendar.lunar.day.22", "calendar.lunar.day.23", "calendar.lunar.day.24",
    "calendar.lunar.day.25", "calendar.lunar.day.26", "calendar.lunar.day.27",
    "calendar.lunar.day.28", "calendar.lunar.day.29", "calendar.lunar.day.30"
};
static const char *const YUAN_MONTH_KEYS[12] = {
    "calendar.yuan.month.1", "calendar.yuan.month.2", "calendar.yuan.month.3",
    "calendar.yuan.month.4", "calendar.yuan.month.5", "calendar.yuan.month.6",
    "calendar.yuan.month.7", "calendar.yuan.month.8", "calendar.yuan.month.9",
    "calendar.yuan.month.10", "calendar.yuan.month.11", "calendar.yuan.month.12"
};
static const char *const YUAN_SEASON_KEYS[4] = {
    "calendar.yuan.season.0", "calendar.yuan.season.1",
    "calendar.yuan.season.2", "calendar.yuan.season.3"
};
static const char *const SEASON_KEYS[4] = {
    "calendar.season.0", "calendar.season.1", "calendar.season.2", "calendar.season.3"
};
static const char *const YUAN_XUN_KEYS[3] = {
    "calendar.yuan.xun.0", "calendar.yuan.xun.1", "calendar.yuan.xun.2"
};
static const char *BRANCHES[12] = {"子","丑","寅","卯","辰","巳","午","未","申","酉","戌","亥"};

#define UNIX_EPOCH_JDN 2440588
#define YUAN_EPOCH_JDN 2451624

/* 六十甲子名按当前语言拼合 */
static void localized_ganzhi(int index, char *buf, size_t cap) {
    int i = ((index % 60) + 60) % 60;
    snprintf(buf, cap, tr("calendar.ganzhi.fmt"), tr(STEM_KEYS[i % 10]), tr(BRANCH_KEYS[i % 12]));
}

/* 农历月名（含闰月前缀） */
static void localized_lunar_month(int month, bool leap, char *buf, size_t cap) {
    int i = qlcg_clampi(month, 1, 12) - 1;
    snprintf(buf, cap, "%s%s", leap ? tr("calendar.lunar.leap") : "", tr(LUNAR_MONTH_KEYS[i]));
}

/* 儒略日转换见 jd_astronomy.c */

/* 元历年月日转儒略日 JDN */
int qlcg_yuan_to_jdn(int year, int month, int day) {
    return YUAN_EPOCH_JDN + (year - 1) * 360 + (month - 1) * 30 + (day - 1);
}

/* JDN 与日内比例转 Unix 毫秒时间戳 */
double qlcg_jdn_to_unix_ms(int jdn, double day_frac, int utc_offset_min) {
    double days = (double)(jdn - UNIX_EPOCH_JDN) + day_frac - (double)utc_offset_min / 1440.0;
    return days * 86400000.0;
}

/* 根据经度计算整数小时时区偏移（分钟） */
int qlcg_timezone_offset_min(float world_x, float circumference) {
    if (circumference <= 1.0f) return 480;
    float lon = (world_x / circumference) * 360.0f;
    while (lon >  180.0f) lon -= 360.0f;
    while (lon < -180.0f) lon += 360.0f;
    int tz_hours = (int)roundf(lon / 15.0f);
    tz_hours = qlcg_clampi(tz_hours, -12, 12);
    return tz_hours * 60;
}

/* 计算 UTC 太阳时刻（0~24h） */
float qlcg_utc_sun_hours(double unix_ms) {
    double days = unix_ms / 86400000.0;
    double frac = days - floor(days);
    return (float)(frac * 24.0);
}

/* 当地太阳时刻：UTC 小时叠加时区 */
float qlcg_local_sun_hours(double unix_ms, int utc_offset_min) {
    float h = qlcg_utc_sun_hours(unix_ms) + (float)utc_offset_min / 60.0f;
    h = fmodf(h, 24.0f);
    return (h < 0.0f) ? h + 24.0f : h;
}

/* 初始化日历时钟 */
void qlcg_calendar_init(CalendarClock *clk, CalendarMode mode, int year, int month, int day, float speed) {
    clk->mode = mode;
    clk->time_speed = speed;
    clk->paused = (speed <= 0.0f);
    int jdn = (mode == CALENDAR_YUAN)
        ? qlcg_yuan_to_jdn(year, month, day)
        : qlcg_gregorian_to_jdn(year, month, day);
    clk->unix_ms = qlcg_jdn_to_unix_ms(jdn, 8.0 / 24.0, 0);
    clk->epoch_unix_ms = clk->unix_ms;
}

/* 按 UTC 日序与日内比例初始化时钟 */
void qlcg_calendar_init_jd(CalendarClock *clk, CalendarMode mode, int jdn, double day_frac, float speed) {
    if (!clk) return;
    clk->mode = mode;
    clk->time_speed = speed;
    clk->paused = (speed <= 0.0f);
    clk->unix_ms = qlcg_jdn_to_unix_ms(jdn, day_frac, 0);
    clk->epoch_unix_ms = clk->unix_ms;
}

/* 推进游戏内时间 */
void qlcg_calendar_advance(CalendarClock *clk, float dt_sec) {
    if (!clk || clk->paused || clk->time_speed <= 0.0f) return;
    clk->unix_ms += (double)dt_sec * (double)clk->time_speed * 1000.0;
}

/* 生成公历、农历（干支/节气/时辰）、元历 */
void qlcg_calendar_snapshot(const CalendarClock *clk, int utc_offset_min, CalendarSnapshot *out) {
    memset(out, 0, sizeof(*out));
    out->utc_offset_min = utc_offset_min;
    double total_days = (clk->unix_ms + (double)utc_offset_min * 60000.0) / 86400000.0;
    double fl = floor(total_days);
    out->jdn = (int)fl + UNIX_EPOCH_JDN;
    out->day_frac = total_days - fl;

    int sec_of_day = (int)floor(out->day_frac * 86400.0 + 0.5);
    if (sec_of_day >= 86400) sec_of_day = 86399;

    /* 1. 公历 */
    qlcg_jdn_to_gregorian(out->jdn, &out->gregorian.year, &out->gregorian.month, &out->gregorian.day);
    out->gregorian.weekday = (out->jdn + 1) % 7;
    out->gregorian.hour    = sec_of_day / 3600;
    out->gregorian.minute  = (sec_of_day % 3600) / 60;
    out->gregorian.second  = sec_of_day % 60;

    /* 2. 十二时辰与刻数，23:00 起子初 */
    double shifted = fmod(out->day_frac + 1.0 / 24.0, 1.0);
    int sc_idx = (int)floor(shifted * 12.0) % 12;
    double in_sc = fmod(shifted * 12.0, 1.0);
    const char *half = (in_sc < 0.5) ? "初" : "正";
    int ke = (int)floor(fmod(in_sc, 0.5) * 8.0);
    static const char *KE_NAMES[4] = {"初刻","一刻","二刻","三刻"};
    out->shichen.index = sc_idx;
    out->shichen.ke = ke;
    snprintf(out->shichen.name, sizeof(out->shichen.name), "%s时", BRANCHES[sc_idx]);
    snprintf(out->shichen.label, sizeof(out->shichen.label), "%s%s%s",
             BRANCHES[sc_idx], half, KE_NAMES[qlcg_clampi(ke, 0, 3)]);

    /* 3. 农历：真定朔编岁 + 时宪历置闰（日界固定东八区） */
    {
        /* 当地日时间还原 UTC 儒略日，再取东八区日界 */
        int chinese_jdn = qlcg_civil_jdn_of_jd(
            qlcg_julian_day_of(out->jdn, out->day_frac) - (double)utc_offset_min / 1440.0,
            QLCG_CHINESE_TZ_MIN);
        QlcgChineseDate cd;
        if (qlcg_lunar_from_jdn(chinese_jdn, &cd)) {
            out->chinese.year = cd.year;
            out->chinese.month = cd.month;
            out->chinese.day = cd.day;
            out->chinese.is_leap = cd.is_leap_month;
            localized_lunar_month(cd.month, cd.is_leap_month,
                                  out->chinese.month_name, sizeof(out->chinese.month_name));
            snprintf(out->chinese.day_name, sizeof(out->chinese.day_name), "%s",
                     tr(LUNAR_DAY_KEYS[qlcg_clampi(cd.day, 1, 30) - 1]));
            localized_ganzhi(qlcg_year_gan_zhi_index(cd.year),
                             out->chinese.year_ganzhi, sizeof(out->chinese.year_ganzhi));
            localized_ganzhi(qlcg_day_gan_zhi_index(out->jdn),
                             out->chinese.day_ganzhi, sizeof(out->chinese.day_ganzhi));
            out->chinese.solar_term_today = cd.solar_term[0] ? qlcg_solar_term_name_at(cd.solar_term) : NULL;
            out->chinese.next_solar_term = qlcg_solar_term_name_at(cd.next_term_name);
            out->chinese.days_to_next_term = cd.next_term_days;
        }
    }

    /* 4. 元历（12月×3旬×10日=360日） */
    int yuan_diff = out->jdn - YUAN_EPOCH_JDN;
    int y_year = (yuan_diff >= 0) ? (yuan_diff / 360 + 1) : ((yuan_diff - 359) / 360 + 1);
    int y_doy  = ((yuan_diff % 360) + 360) % 360;
    int y_mon  = y_doy / 30 + 1;
    int y_day  = (y_doy % 30) + 1;
    int y_xun  = (y_day - 1) / 10;
    int y_dix  = (y_day - 1) % 10 + 1;
    int y_sea  = (y_mon - 1) / 3;

    out->yuan.year         = y_year;
    out->yuan.month        = y_mon;
    out->yuan.day          = y_day;
    out->yuan.season_idx   = y_sea;
    out->yuan.xun_idx      = y_xun;
    out->yuan.day_in_xun   = y_dix;
    out->yuan.xun_of_year  = y_doy / 10 + 1;
    out->yuan.season_name  = tr(YUAN_SEASON_KEYS[y_sea]);
    out->yuan.month_name   = tr(YUAN_MONTH_KEYS[y_mon - 1]);
    out->yuan.month_branch = tr(BRANCH_KEYS[(y_mon + 1) % 12]);
    out->yuan.xun_name     = tr(YUAN_XUN_KEYS[y_xun]);
    out->yuan.day_name     = tr(LUNAR_DAY_KEYS[y_day - 1]);
    out->yuan.stem_name    = tr(STEM_KEYS[y_dix - 1]);
    out->yuan.hour         = out->gregorian.hour;
    out->yuan.minute       = out->gregorian.minute;
    out->yuan.second       = out->gregorian.second;

    /* 5. 季节：真实模式按四立，元历按季表 */
    if (clk->mode == CALENDAR_YUAN) {
        out->season_index = y_sea;
        out->season_start_jdn = out->jdn - y_doy % 90;
        out->next_season_start_jdn = out->season_start_jdn + 90;
        out->season_phase = (float)y_sea + (float)((y_doy % 90) + out->day_frac) / 90.0f;
    } else {
        int gy = out->gregorian.year;
        int start_jdn = 0, next_jdn = 0, season_idx = 0;
        double start_lon = 315.0;
        int found = 0;
        int prev_jdn = -1 << 30, prev_lon = 0;
        for (int pass = 0; pass < 3 && !found; pass++) {
            int yy = gy - 1 + pass;
            const QlcgSolarTerm *terms = qlcg_solar_terms_of_year(yy);
            for (int i = 0; i < 24; i++) {
                double lon = terms[i].longitude;
                int is_start = (lon == 315.0 || lon == 45.0 || lon == 135.0 || lon == 225.0);
                if (!is_start) continue;
                if (terms[i].jdn <= out->jdn && terms[i].jdn > prev_jdn) {
                    prev_jdn = terms[i].jdn;
                    prev_lon = (int)lon;
                    start_jdn = terms[i].jdn;
                    start_lon = lon;
                }
            }
        }
        if (prev_jdn == (-1 << 30)) { start_jdn = out->jdn; start_lon = 315.0; }
        /* 找下一个四立 */
        next_jdn = 0;
        {
            int best = 1 << 30;
            for (int pass = 0; pass < 3; pass++) {
                int yy = gy - 1 + pass;
                const QlcgSolarTerm *terms = qlcg_solar_terms_of_year(yy);
                for (int i = 0; i < 24; i++) {
                    double lon = terms[i].longitude;
                    int is_start = (lon == 315.0 || lon == 45.0 || lon == 135.0 || lon == 225.0);
                    if (!is_start) continue;
                    if (terms[i].jdn > start_jdn && terms[i].jdn < best) best = terms[i].jdn;
                }
            }
            if (best == (1 << 30)) best = start_jdn + 90;
            next_jdn = best;
        }
        if (start_lon == 315.0) season_idx = 0;
        else if (start_lon == 45.0) season_idx = 1;
        else if (start_lon == 135.0) season_idx = 2;
        else if (start_lon == 225.0) season_idx = 3;
        int days_in_season = next_jdn - start_jdn;
        if (days_in_season <= 0) days_in_season = 90;
        double progress = ((double)(out->jdn - start_jdn) + out->day_frac) / (double)days_in_season;
        out->season_index = season_idx;
        out->season_start_jdn = start_jdn;
        out->next_season_start_jdn = next_jdn;
        out->season_phase = (float)((double)season_idx + progress);
    }
    out->season_name = tr(SEASON_KEYS[out->season_index]);
}

const char *qlcg_calendar_mode_name(CalendarMode mode) {
    switch (mode) {
        case CALENDAR_GREGORIAN: return tr("calendar.real");
        case CALENDAR_LUNAR: return tr("calendar.chinese");
        case CALENDAR_YUAN: return tr("calendar.yuan");
        default: return tr("calendar.default");
    }
}

/* 格式化游戏顶栏日期时间 */
void qlcg_calendar_format_topbar(const CalendarSnapshot *snap, CalendarMode mode, char *buf, size_t cap) {
    int tz_h = snap->utc_offset_min / 60;
    char tz_str[16];
    if (tz_h == 0) snprintf(tz_str, sizeof(tz_str), "UTC");
    else snprintf(tz_str, sizeof(tz_str), "UTC%+d", tz_h);

    if (mode == CALENDAR_YUAN) {
        snprintf(buf, cap, tr("calendar.topbar.yuan"),
                 snap->yuan.year, snap->yuan.month, snap->yuan.day,
                 snap->yuan.hour, snap->yuan.minute, tz_str, snap->season_name);
    } else if (mode == CALENDAR_LUNAR) {
        snprintf(buf, cap, tr("calendar.topbar.chinese"),
                 snap->chinese.year_ganzhi, snap->chinese.month_name, snap->chinese.day_name,
                 snap->gregorian.hour, snap->gregorian.minute, tz_str, snap->season_name);
    } else if (mode == CALENDAR_GREGORIAN) {
        snprintf(buf, cap, tr("calendar.topbar.gregorian"),
                 snap->gregorian.year, snap->gregorian.month, snap->gregorian.day,
                 snap->gregorian.hour, snap->gregorian.minute, tz_str, snap->season_name);
    } else {
        snprintf(buf, cap, tr("calendar.topbar.default"),
                 snap->gregorian.year, snap->gregorian.month, snap->gregorian.day,
                 snap->chinese.month_name, snap->chinese.day_name,
                 snap->gregorian.hour, snap->gregorian.minute, tz_str, snap->season_name);
    }
}

/* 农历行：真实给干支月日节气，元历给旬名月建 */
void qlcg_calendar_format_lunar(const CalendarSnapshot *snap, CalendarMode mode, char *buf, size_t cap) {
    if (mode == CALENDAR_YUAN) {
        snprintf(buf, cap, tr("calendar.lunar.yuanLine"),
                 snap->yuan.month_name ? snap->yuan.month_name : "",
                 snap->yuan.xun_name ? snap->yuan.xun_name : "",
                 snap->yuan.day_name ? snap->yuan.day_name : "",
                 snap->yuan.month_branch ? snap->yuan.month_branch : "");
        return;
    }

    if (mode == CALENDAR_GREGORIAN) {
        if (cap) buf[0] = '\0';
        return;
    }

    char term[64];
    if (snap->chinese.solar_term_today && snap->chinese.solar_term_today[0]) {
        snprintf(term, sizeof(term), "%s", snap->chinese.solar_term_today);
    } else if (snap->chinese.next_solar_term && snap->chinese.next_solar_term[0]) {
        snprintf(term, sizeof(term), tr("calendar.lunar.nextTerm"), snap->chinese.next_solar_term,
                 snap->chinese.days_to_next_term);
    } else {
        term[0] = '\0';
    }
    snprintf(buf, cap, tr("calendar.lunar.realLine"),
             snap->chinese.year_ganzhi, snap->chinese.month_name,
             snap->chinese.day_name, term);
}

/* 格式化 F12 调试面板三行历法明细 */
void qlcg_calendar_format_debug(const CalendarSnapshot *snap, CalendarMode mode,
                                char *line1, size_t c1, char *line2, size_t c2, char *line3, size_t c3) {
    if (mode == CALENDAR_YUAN) {
        snprintf(line1, c1, tr("calendar.debug.yuanLine1"),
                 snap->yuan.year, snap->yuan.month_name, snap->yuan.month_branch,
                 snap->yuan.xun_name, snap->yuan.day_name, snap->yuan.stem_name);
        snprintf(line2, c2, tr("calendar.debug.timeYuan"),
                 snap->yuan.hour, snap->yuan.minute, snap->yuan.second, snap->yuan.xun_of_year);
        snprintf(line3, c3, tr("calendar.debug.line3"), snap->shichen.label, snap->season_name);
    } else {
        snprintf(line1, c1, tr("calendar.debug.gregLine1"),
                 snap->gregorian.year, snap->gregorian.month, snap->gregorian.day,
                 snap->gregorian.hour, snap->gregorian.minute, snap->gregorian.second);
        if (mode == CALENDAR_GREGORIAN) {
            if (c2) line2[0] = '\0';
            snprintf(line3, c3, tr("calendar.debug.line3"), snap->shichen.label, snap->season_name);
            return;
        }
        char *lunar_line = mode == CALENDAR_LUNAR ? line1 : line2;
        size_t lunar_cap = mode == CALENDAR_LUNAR ? c1 : c2;
        const char *term_s = snap->chinese.solar_term_today
            ? snap->chinese.solar_term_today
            : snap->chinese.next_solar_term;
        snprintf(lunar_line, lunar_cap, tr("calendar.debug.lunarLine2"),
                 snap->chinese.year_ganzhi, snap->chinese.month_name,
                 snap->chinese.day_name, snap->chinese.day_ganzhi,
                 snap->chinese.solar_term_today ? tr("calendar.term.today") : tr("calendar.term.near"),
                 term_s ? term_s : "");
        if (mode == CALENDAR_LUNAR)
            snprintf(line2, c2, tr("calendar.debug.time"),
                     snap->gregorian.hour, snap->gregorian.minute, snap->gregorian.second);
        snprintf(line3, c3, tr("calendar.debug.line3"), snap->shichen.label, snap->season_name);
    }
}
