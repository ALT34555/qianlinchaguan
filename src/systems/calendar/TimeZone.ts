/** 经度与时区换算 */
import { MS_PER_DAY } from './JulianDay';

const ZONE_DEGREES = 15;

/** 世界 X 坐标 → 经度（东正西负）。 */
export function longitudeOfWorldX(x: number, circumference: number): number {
  if (!(circumference > 0)) return 0;
  const longitude = x / circumference * 360;
  return ((longitude + 180) % 360 + 360) % 360 - 180;
}

/** 经度 → 整点时区序号（15° 一区）。 */
export function timeZoneOfLongitude(longitude: number): number {
  return Math.max(-12, Math.min(12, Math.round(longitude / ZONE_DEGREES)));
}

/** 经度 → UTC 偏移分钟。 */
export function timeZoneOffsetMinutes(longitude: number): number {
  return timeZoneOfLongitude(longitude) * 60;
}

/** Unix 毫秒 → UTC 当日比例。 */
export function utcDayFraction(unixMs: number): number {
  return ((unixMs / MS_PER_DAY) % 1 + 1) % 1;
}

/** Unix 毫秒 → UTC 当日小时（0-24）。 */
export function utcHourOfDay(unixMs: number): number {
  return utcDayFraction(unixMs) * 24;
}

/** UTC 转 stepHours 整除日照时刻 */
export function sunHoursOf(unixMs: number, stepHours = 0.5): number {
  if (!(stepHours > 0)) return 0;
  return Math.floor(utcHourOfDay(unixMs) / stepHours + 1e-9) * stepHours;
}

/** UTC 当日比例 + 时区偏移 → 当地当日比例。 */
export function localDayFraction(utcFraction: number, offsetMinutes: number): number {
  const shifted = utcFraction + offsetMinutes / 1440;
  return ((shifted % 1) + 1) % 1;
}

/** UTC 偏移 → 展示文本。 */
export function formatUtcOffset(offsetMinutes: number): string {
  if (!Number.isFinite(offsetMinutes)) return 'UTC';
  const total = Math.round(offsetMinutes);
  if (total === 0) return 'UTC';
  const sign = total < 0 ? '-' : '+';
  const abs = Math.abs(total);
  const minutes = abs % 60;
  return `UTC${sign}${Math.floor(abs / 60)}${minutes ? `:${String(minutes).padStart(2, '0')}` : ''}`;
}
