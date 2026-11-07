#ifndef QLCG_CALENDAR_H
#define QLCG_CALENDAR_H

#include "../../core/config.h"

/* 公历日期结构 */
typedef struct {
    int  year, month, day;
    int  weekday;
    int  hour, minute, second;
} GregorianDate;

/* 农历与干支节气结构 */
typedef struct {
    int         year, month, day;
    bool        is_leap;
    char        year_ganzhi[32];
    char        day_ganzhi[32];
    char        month_name[32];
    char        day_name[32];
    const char *solar_term_today;
    const char *next_solar_term;
    int         days_to_next_term;
} ChineseDate;

/* 元历日期结构：12月×3旬×10日=360 */
typedef struct {
    int         year, month, day;
    int         season_idx;
    int         xun_idx;
    int         day_in_xun;
    int         xun_of_year;
    const char *season_name;
    const char *month_name;
    const char *month_branch;
    const char *xun_name;
    const char *day_name;
    const char *stem_name;
    int         hour, minute, second;
} YuanDate;

/* 十二时辰与刻数信息 */
typedef struct {
    int  index;
    int  ke;
    char name[16];
    char label[24];
} ShichenInfo;

/* 三历完整快照 */
typedef struct {
    int           jdn;
    double        day_frac;
    GregorianDate gregorian;
    ChineseDate   chinese;
    YuanDate      yuan;
    ShichenInfo   shichen;
    int           season_index;
    int           season_start_jdn;
    int           next_season_start_jdn;
    const char   *season_name;
    float         season_phase;
    int           utc_offset_min;
} CalendarSnapshot;

/* 历法时钟结构 */
typedef struct {
    double       unix_ms;
    double       epoch_unix_ms;
    float        time_speed;
    bool         paused;
    CalendarMode mode;
} CalendarClock;

/* 历法计算与格式化接口 */
int    qlcg_gregorian_to_jdn(int year, int month, int day);
void   qlcg_jdn_to_gregorian(int jdn, int *year, int *month, int *day);
int    qlcg_yuan_to_jdn(int year, int month, int day);
double qlcg_jdn_to_unix_ms(int jdn, double day_frac, int utc_offset_min);
int    qlcg_timezone_offset_min(float world_x, float circumference);
/* 仅舆图日照层用，3D 天空请用 local */
float  qlcg_utc_sun_hours(double unix_ms);
float  qlcg_local_sun_hours(double unix_ms, int utc_offset_min);

void   qlcg_calendar_init(CalendarClock *clk, CalendarMode mode, int year, int month, int day, float speed);
/* 按 UTC 日序与日内比例初始化时钟 */
void   qlcg_calendar_init_jd(CalendarClock *clk, CalendarMode mode, int jdn, double day_frac, float speed);
void   qlcg_calendar_advance(CalendarClock *clk, float dt_sec);
void   qlcg_calendar_snapshot(const CalendarClock *clk, int utc_offset_min, CalendarSnapshot *out);
const char *qlcg_calendar_mode_name(CalendarMode mode);
void   qlcg_calendar_format_topbar(const CalendarSnapshot *snap, CalendarMode mode, char *buf, size_t cap);
void   qlcg_calendar_format_lunar(const CalendarSnapshot *snap, CalendarMode mode, char *buf, size_t cap);
void   qlcg_calendar_format_debug(const CalendarSnapshot *snap, CalendarMode mode,
                                  char *line1, size_t c1, char *line2, size_t c2, char *line3, size_t c3);

#endif /* QLCG_CALENDAR_H */
