/** 元历 —— 本项目原创的极简历法（纯算术 */
import { clockTimeOf, gregorianToJdn, jdnToGregorian, dayTimeFromUnixMs } from './JulianDay';
import {
  YUAN_MONTH_NAMES,
  YUAN_SEASON_NAMES,
  YUAN_XUN_NAMES,
  branchName,
  chineseDayName,
  stemName,
} from './GanZhi';
import type { DayTime, YuanDate } from './types';

/** 元历纪元 */
export const YUAN_EPOCH_JDN = gregorianToJdn(2000, 3, 20);

/** 一旬日数。 */
export const YUAN_DAYS_PER_XUN = 10;

/** 一月旬数。 */
export const YUAN_XUNS_PER_MONTH = 3;

/** 一月日数。 */
export const YUAN_DAYS_PER_MONTH = YUAN_XUNS_PER_MONTH * YUAN_DAYS_PER_XUN;

/** 一年月数。 */
export const YUAN_MONTHS_PER_YEAR = 12;

/** 一季月数。 */
export const YUAN_MONTHS_PER_SEASON = 3;

/** 一季日数。 */
export const YUAN_DAYS_PER_SEASON = YUAN_MONTHS_PER_SEASON * YUAN_DAYS_PER_MONTH;

/** 一年日数：恒为 12 × 30 = 360。 */
export const YUAN_DAYS_PER_YEAR = YUAN_MONTHS_PER_YEAR * YUAN_DAYS_PER_MONTH;

/** 一年旬数：恒为 36。 */
export const YUAN_XUNS_PER_YEAR = YUAN_DAYS_PER_YEAR / YUAN_DAYS_PER_XUN;

/** 元历月建地支（一月为寅，十二月为丑）。 */
function monthBranchName(month: number): string {
  return branchName(2 + (month - 1));
}

/** 元历某年一月一日的日序。 */
export function yuanYearStartJdn(year: number): number {
  return YUAN_EPOCH_JDN + YUAN_DAYS_PER_YEAR * (year - 1);
}

/** 元历某年某月一日的日序（月为 1 ~ 12）。 */
export function yuanMonthStartJdn(year: number, month: number): number {
  return yuanYearStartJdn(year) + (month - 1) * YUAN_DAYS_PER_MONTH;
}

/** 元历某年的日数（恒为 360）。 */
export function yuanYearDays(_year?: number): number {
  return YUAN_DAYS_PER_YEAR;
}

/** 由日序求元历年号（纪元之前为 0 或负数 */
export function yuanYearOfJdn(jdn: number): number {
  return Math.floor((jdn - YUAN_EPOCH_JDN) / YUAN_DAYS_PER_YEAR) + 1;
}

export class YuanCalendar {
  /** 由日序与当日比例得到元历日期。 */
  fromDayTime(dayTime: DayTime): YuanDate {
    return this.fromJdn(dayTime.jdn, dayTime.frac);
  }

  /** 由 Unix 毫秒时间戳得到元历日期（按给定时区 */
  fromUnixMs(ms: number, utcOffsetMinutes = 480): YuanDate {
    const { jdn, frac } = dayTimeFromUnixMs(ms, utcOffsetMinutes);
    return this.fromJdn(jdn, frac);
  }

  /** 由日序与当日比例得到元历日期。 */
  fromJdn(jdn: number, frac = 0): YuanDate {
    const year = yuanYearOfJdn(jdn);
    const dayIndex = jdn - yuanYearStartJdn(year); // 0 ~ 359
    const month = Math.floor(dayIndex / YUAN_DAYS_PER_MONTH) + 1;
    const day = (dayIndex % YUAN_DAYS_PER_MONTH) + 1;
    const xun = Math.floor((day - 1) / YUAN_DAYS_PER_XUN);
    const dayInXun = ((day - 1) % YUAN_DAYS_PER_XUN) + 1;
    const seasonIndex = Math.floor((month - 1) / YUAN_MONTHS_PER_SEASON);
    return {
      kind: 'yuan',
      jdn,
      year,
      month,
      monthName: YUAN_MONTH_NAMES[month - 1]!,
      monthBranch: monthBranchName(month),
      season: YUAN_SEASON_NAMES[seasonIndex]!,
      seasonIndex,
      xun,
      xunName: YUAN_XUN_NAMES[xun]!,
      dayInXun,
      day,
      dayName: chineseDayName(day),
      stemName: stemName(dayInXun - 1),
      dayOfYear: dayIndex + 1,
      daysInYear: YUAN_DAYS_PER_YEAR,
      xunOfYear: Math.floor(dayIndex / YUAN_DAYS_PER_XUN) + 1,
      time: clockTimeOf(frac),
    };
  }

  /** 元历日期 -> 日序（月 1 ~ 12 */
  toJdn(year: number, month: number, day: number): number {
    return yuanMonthStartJdn(year, month) + (day - 1);
  }

  /** 元历日期 -> 对应的公历日期。 */
  toGregorian(year: number, month: number, day: number): { year: number; month: number; day: number } {
    return jdnToGregorian(this.toJdn(year, month, day));
  }

  /** 元历某年岁首（一月一日）对应的公历日期 */
  yearStartGregorian(year: number): { year: number; month: number; day: number } {
    return jdnToGregorian(yuanYearStartJdn(year));
  }

  /** 四个季的起始月（1、4、7、10）。 */
  seasonStartMonths(): number[] {
    return [1, 4, 7, 10];
  }

  /** 按指定风格格式化元历日期。 */
  format(date: YuanDate, style: 'full' | 'brief' | 'month-day' = 'full'): string {
    const monthPart = `${date.monthName}月（${date.monthBranch}）${date.xunName}`;
    if (style === 'month-day') return `${monthPart} ${date.dayName}`;
    if (style === 'brief') return `元历${date.year}年 ${monthPart} ${date.dayName}`;
    return `元历${date.year}年 ${date.season}·${monthPart} ${date.dayName}（${date.stemName}日）`;
  }

  /** 元历日期 + 时刻的完整文本。 */
  formatWithTime(date: YuanDate): string {
    const pad = (n: number) => String(n).padStart(2, '0');
    return `${this.format(date, 'brief')} ${pad(date.time.hour)}:${pad(date.time.minute)}`;
  }

  /** 元历月的天数（恒为 30 */
  monthDays(): number {
    return YUAN_DAYS_PER_MONTH;
  }
}

/** 默认实例。 */
export const yuanCalendar = new YuanCalendar();
