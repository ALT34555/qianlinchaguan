/**
 * 元历 —— 本项目原创的极简历法（纯算术，不涉及任何天文机制）。
 *
 * 基本单位：
 *   1 日 = 24 小时（1 小时 = 60 分，1 分 = 60 秒）
 *   1 旬 = 10 日          上旬 1—10 日，中旬 11—20 日，下旬 21—30 日
 *   1 月 = 3 旬 = 30 日
 *   1 季 = 3 月 = 90 日
 *   1 年 = 4 季 = 12 月 = 360 日
 *          春 1—3 月，夏 4—6 月，秋 7—9 月，冬 10—12 月
 *
 * 设计原则：
 *   **一年恒为 360 日：不设闰年、不设岁余，不做任何天文推算。**
 *   因此"元历日期 ↔ 日序"的换算只是整数乘加，规则可心算、结果可预测，
 *   任意一年的结构都完全相同（12 月 × 30 日、36 旬、4 季、每旬 10 日）。
 *
 * 纪元：
 *   元历元年一月一日 = 公历 2000 年 3 月 20 日（仅为约定的换算锚点）。
 *   由于一年固定 360 日，比回归年短约 5.24 日，元历岁首相对公历会逐年提前约 5.24 日
 *   （约 69 年提前一整年），元历的春夏秋冬因而会逐渐与现实的季节脱钩。
 *   这是"极简、不考虑天文机制"的必然结果；若需要历法与季节同步，请使用公历或农历。
 *
 * 命名：
 *   月名取"孟 / 仲 / 季 + 季名"：孟春、仲春、季春、孟夏 … 季冬；
 *   月建地支：一月为寅、二月为卯 …… 十二月为丑；
 *   日名：沿用"初一 … 三十"；
 *   旬内十日依次配十天干（旬首为甲），故"上旬戊日"即该月第五日，
 *   一旬之内天干固定，比农历的连续六十甲子更容易心算。
 */
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

/** 元历纪元：元年一月一日 = 公历 2000 年 3 月 20 日。 */
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

/** 由日序求元历年号（纪元之前为 0 或负数，纯整数运算）。 */
export function yuanYearOfJdn(jdn: number): number {
  return Math.floor((jdn - YUAN_EPOCH_JDN) / YUAN_DAYS_PER_YEAR) + 1;
}

export class YuanCalendar {
  /** 由日序与当日比例得到元历日期。 */
  fromDayTime(dayTime: DayTime): YuanDate {
    return this.fromJdn(dayTime.jdn, dayTime.frac);
  }

  /** 由 Unix 毫秒时间戳得到元历日期（按给定时区，默认东八区）。 */
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

  /** 元历日期 -> 日序（月 1 ~ 12，日 1 ~ 30）。 */
  toJdn(year: number, month: number, day: number): number {
    return yuanMonthStartJdn(year, month) + (day - 1);
  }

  /** 元历日期 -> 对应的公历日期。 */
  toGregorian(year: number, month: number, day: number): { year: number; month: number; day: number } {
    return jdnToGregorian(this.toJdn(year, month, day));
  }

  /**
   * 元历某年岁首（一月一日）对应的公历日期。
   * 因一年固定 360 日，该日期相对公历逐年提前约 5.24 日。
   */
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

  /** 元历月的天数（恒为 30，保留接口以便与其它历法统一）。 */
  monthDays(): number {
    return YUAN_DAYS_PER_MONTH;
  }
}

/** 默认实例。 */
export const yuanCalendar = new YuanCalendar();
