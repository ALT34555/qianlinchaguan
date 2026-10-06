/** 公历换算器（Proleptic） */
import {
  daysInMonth,
  gregorianToJdn,
  isLeapYear,
  jdnToGregorian,
  weekdayOf,
  clockTimeOf,
  dayTimeFromUnixMs,
} from './JulianDay';
import { WEEKDAY_NAMES } from './GanZhi';
import type { GregorianDate } from './types';

/** 公历日期格式。 */
export type GregorianStyle = 'iso' | 'chinese' | 'short';

export class GregorianCalendar {
  /** 民用日界所在的时区偏移（分钟），默认东八区。 */
  readonly utcOffsetMinutes: number;

  constructor(utcOffsetMinutes = 480) {
    this.utcOffsetMinutes = utcOffsetMinutes;
  }

  /** 由日序与当日比例得到公历日期。 */
  fromDayTime(jdn: number, frac: number): GregorianDate {
    const { year, month, day } = jdnToGregorian(jdn);
    const leap = isLeapYear(year);
    const jan1 = gregorianToJdn(year, 1, 1);
    const weekday = weekdayOf(jdn);
    const { isoWeekYear, isoWeek } = isoWeekOf(jdn);
    return {
      kind: 'gregorian',
      jdn,
      year,
      month,
      day,
      weekday,
      weekdayName: WEEKDAY_NAMES[weekday]!,
      isLeapYear: leap,
      daysInMonth: daysInMonth(year, month),
      dayOfYear: jdn - jan1 + 1,
      isoWeek,
      isoWeekYear,
      quarter: Math.floor((month - 1) / 3) + 1,
      time: clockTimeOf(frac),
    };
  }

  /** 由 Unix 毫秒时间戳得到公历日期（按本实例的 */
  fromUnixMs(ms: number): GregorianDate {
    const { jdn, frac } = dayTimeFromUnixMs(ms, this.utcOffsetMinutes);
    return this.fromDayTime(jdn, frac);
  }

  /** 由年月日构造日序。 */
  toJdn(year: number, month: number, day: number): number {
    return gregorianToJdn(year, month, day);
  }

  /** 该年该月的天数。 */
  monthDays(year: number, month: number): number {
    return daysInMonth(year, month);
  }

  /** 某年的日数。 */
  yearDays(year: number): number {
    return isLeapYear(year) ? 366 : 365;
  }

  /** 两个日序之间相差的天数（后者减前者）。 */
  daysBetween(fromJdn: number, toJdn: number): number {
    return toJdn - fromJdn;
  }

  /** 按指定风格格式化公历日期。 */
  format(date: GregorianDate, style: GregorianStyle = 'chinese'): string {
    const pad = (n: number) => String(n).padStart(2, '0');
    if (style === 'iso') return `${date.year}-${pad(date.month)}-${pad(date.day)}`;
    if (style === 'short') return `${date.month}月${date.day}日`;
    return `${date.year}年${date.month}月${date.day}日 ${date.weekdayName}`;
  }

  /** 格式化时刻，默认 24 小时制。 */
  formatTime(date: GregorianDate, withSeconds = false): string {
    const pad = (n: number) => String(n).padStart(2, '0');
    const base = `${pad(date.time.hour)}:${pad(date.time.minute)}`;
    return withSeconds ? `${base}:${pad(date.time.second)}` : base;
  }
}

/** ISO 8601 周序号与所属年份。 */
export function isoWeekOf(jdn: number): { isoWeekYear: number; isoWeek: number } {
  const weekday = weekdayOf(jdn);
  const isoDow = weekday === 0 ? 7 : weekday; // 周一 = 1 … 周日 = 7
  const thursday = jdn + (4 - isoDow); // 该周周四决定周所属年份
  const isoWeekYear = jdnToGregorian(thursday).year;
  const jan1 = gregorianToJdn(isoWeekYear, 1, 1);
  return { isoWeekYear, isoWeek: Math.floor((thursday - jan1) / 7) + 1 };
}

/** 默认实例：东八区。 */
export const gregorian = new GregorianCalendar(480);
