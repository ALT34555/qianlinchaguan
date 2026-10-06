import { daysInMonth, gregorianToJdn, unixMsFromDayTime } from './JulianDay';
import { yuanCalendar } from './YuanCalendar';

export interface StartDate { year: number; month: number; day: number }
export const DEFAULT_YUAN_DATE: StartDate = { year: 13, month: 10, day: 1 };
export function todayDate(now = new Date()): StartDate {
  return { year: now.getFullYear(), month: now.getMonth() + 1, day: now.getDate() };
}
export function dateInputValue(date: StartDate): string {
  return `${String(date.year).padStart(4, '0')}-${String(date.month).padStart(2, '0')}-${String(date.day).padStart(2, '0')}`;
}
export function startDateUnixMs(mode: 'real' | 'yuan', date: StartDate, utcOffsetMinutes: number): number {
  const { year, month, day } = date;
  if (![year, month, day].every(Number.isInteger) || year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 ||
      day > (mode === 'yuan' ? 30 : daysInMonth(year, month))) throw new Error('起始日期无效，请检查年月日。');
  const jdn = mode === 'yuan' ? yuanCalendar.toJdn(year, month, day) : gregorianToJdn(year, month, day);
  // 新世界从所选日期的早上八点开始
  return unixMsFromDayTime(jdn, 8 / 24, utcOffsetMinutes);
}
