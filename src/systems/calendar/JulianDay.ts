/**
 * 儒略日序（JDN）与时间尺度换算工具。
 *
 * 说明：
 *  - 本项目内部统一用"整数日序 jdn"表示一天，jdn 等于该日正午的儒略日（JD）整数部分；
 *    因此某瞬间的儒略日 = jdn - 0.5 + frac。
 *  - 天文推算使用力学时 TT（儒略日记作 JDE），与民用时之间的差为 ΔT；
 *    ΔT 采用 Espenak & Meeus 的分段多项式（NASA 日食目录所用的同一套公式）。
 */

/** 1970-01-01（Unix 纪元）的儒略日序。 */
export const UNIX_EPOCH_JDN = 2440588;

/** J2000.0 历元：2000-01-01 12:00 TT。 */
export const J2000 = 2451545.0;

/** 一日的毫秒数。 */
export const MS_PER_DAY = 86400000;

/** 儒略世纪长度（日）。 */
export const DAYS_PER_CENTURY = 36525;

const D2R = Math.PI / 180;

/** 是否为格里高利历闰年。 */
export function isLeapYear(year: number): boolean {
  return (year % 4 === 0 && year % 100 !== 0) || year % 400 === 0;
}

/** 某年某月的天数（公历）。 */
export function daysInMonth(year: number, month: number): number {
  if (month === 2) return isLeapYear(year) ? 29 : 28;
  if (month === 4 || month === 6 || month === 9 || month === 11) return 30;
  return 31;
}

/** 公历日期 -> 儒略日序。使用 Fliegel–Van Flandern 公式。 */
export function gregorianToJdn(year: number, month: number, day: number): number {
  const a = Math.floor((14 - month) / 12);
  const y = year + 4800 - a;
  const m = month + 12 * a - 3;
  return (
    day +
    Math.floor((153 * m + 2) / 5) +
    365 * y +
    Math.floor(y / 4) -
    Math.floor(y / 100) +
    Math.floor(y / 400) -
    32045
  );
}

/** 儒略日序 -> 公历日期。 */
export function jdnToGregorian(jdn: number): { year: number; month: number; day: number } {
  const a = jdn + 32044;
  const b = Math.floor((4 * a + 3) / 146097);
  const c = a - Math.floor((146097 * b) / 4);
  const d = Math.floor((4 * c + 3) / 1461);
  const e = c - Math.floor((1461 * d) / 4);
  const m = Math.floor((5 * e + 2) / 153);
  return {
    day: e - Math.floor((153 * m + 2) / 5) + 1,
    month: m + 3 - 12 * Math.floor(m / 10),
    year: 100 * b + d - 4800 + Math.floor(m / 10),
  };
}

/** 取公历日期的年份。 */
export function gregorianYearOf(jdn: number): number {
  return jdnToGregorian(jdn).year;
}

/** 星期序号：0 = 周日 … 6 = 周六。 */
export function weekdayOf(jdn: number): number {
  return (((jdn + 1) % 7) + 7) % 7;
}

/** 把角度规范到 [0, 360)。 */
export function norm360(deg: number): number {
  const r = deg % 360;
  return r < 0 ? r + 360 : r;
}

/** 把角度规范到 (-180, 180]。 */
export function norm180(deg: number): number {
  const r = norm360(deg);
  return r > 180 ? r - 360 : r;
}

/** 瞬间的儒略日（JD，UT 或 TT 视输入尺度而定）。 */
export function julianDayOf(jdn: number, frac: number): number {
  return jdn - 0.5 + frac;
}

/** 由儒略日拆出整数日序与当日比例（frac 恒为 [0, 1)）。 */
export function dayTimeOfJulianDay(jd: number): { jdn: number; frac: number } {
  const shifted = jd + 0.5;
  const jdn = Math.floor(shifted);
  return { jdn, frac: shifted - jdn };
}

/** Unix 毫秒 -> 指定时区的日序与当日比例。 */
export function dayTimeFromUnixMs(ms: number, utcOffsetMinutes = 0): { jdn: number; frac: number } {
  const jd = ms / MS_PER_DAY + UNIX_EPOCH_JDN - 0.5;
  return dayTimeOfJulianDay(jd + utcOffsetMinutes / 1440);
}

/** 指定时区的日序与当日比例 -> Unix 毫秒。 */
export function unixMsFromDayTime(jdn: number, frac: number, utcOffsetMinutes = 0): number {
  const jd = julianDayOf(jdn, frac) - utcOffsetMinutes / 1440;
  return Math.round((jd - (UNIX_EPOCH_JDN - 0.5)) * MS_PER_DAY);
}

/** 由当日比例拆出时 / 分 / 秒 / 毫秒。 */
export function clockTimeOf(frac: number): { hour: number; minute: number; second: number; millisecond: number } {
  const f = ((frac % 1) + 1) % 1;
  // 用四舍五入到毫秒，避免 frac = 13/24 这类情况下浮点下取整丢 1 毫秒而显示成 12:59:59.999
  const total = Math.min(Math.round(f * MS_PER_DAY), MS_PER_DAY - 1);
  const second = Math.floor(total / 1000);
  return {
    hour: Math.floor(second / 3600),
    minute: Math.floor(second / 60) % 60,
    second: second % 60,
    millisecond: total % 1000,
  };
}

/**
 * ΔT = TT - UT，单位秒。
 * 采用 Espenak & Meeus 为 NASA 日食目录给出的分段多项式。
 * @param decimalYear 以年为单位的小数年（如 2026.5）
 */
export function deltaTSeconds(decimalYear: number): number {
  const y = decimalYear;
  if (y < -500) {
    const u = (y - 1820) / 100;
    return -20 + 32 * u * u;
  }
  if (y < 500) {
    const u = y / 100;
    return (
      10583.6 -
      1014.41 * u +
      33.78311 * u ** 2 -
      5.952053 * u ** 3 -
      0.1798452 * u ** 4 +
      0.022174192 * u ** 5 +
      0.0090316521 * u ** 6
    );
  }
  if (y < 1600) {
    const u = (y - 1000) / 100;
    return (
      1574.2 -
      556.01 * u +
      71.23472 * u ** 2 +
      0.319781 * u ** 3 -
      0.8503463 * u ** 4 -
      0.005050998 * u ** 5 +
      0.0083572073 * u ** 6
    );
  }
  if (y < 1700) {
    const t = y - 1600;
    return 120 - 0.9808 * t - 0.01532 * t * t + (t * t * t) / 7129;
  }
  if (y < 1800) {
    const t = y - 1700;
    return 8.83 + 0.1603 * t - 0.0059285 * t * t + 0.00013336 * t ** 3 - t ** 4 / 1174000;
  }
  if (y < 1860) {
    const t = y - 1800;
    return (
      13.72 -
      0.332447 * t +
      0.0068612 * t * t +
      0.0041116 * t ** 3 -
      0.00037436 * t ** 4 +
      0.0000121272 * t ** 5 -
      0.0000001699 * t ** 6 +
      0.000000000875 * t ** 7
    );
  }
  if (y < 1900) {
    const t = y - 1860;
    return (
      7.62 + 0.5737 * t - 0.251754 * t * t + 0.01680668 * t ** 3 - 0.0004473624 * t ** 4 + t ** 5 / 233174
    );
  }
  if (y < 1920) {
    const t = y - 1900;
    return -2.79 + 1.494119 * t - 0.0598939 * t * t + 0.0061966 * t ** 3 - 0.000197 * t ** 4;
  }
  if (y < 1941) {
    const t = y - 1920;
    return 21.2 + 0.84493 * t - 0.0761 * t * t + 0.0020936 * t ** 3;
  }
  if (y < 1961) {
    const t = y - 1950;
    return 29.07 + 0.407 * t - (t * t) / 233 + (t * t * t) / 2547;
  }
  if (y < 1986) {
    const t = y - 1975;
    return 45.45 + 1.067 * t - (t * t) / 260 - (t * t * t) / 718;
  }
  if (y < 2005) {
    const t = y - 2000;
    return (
      63.86 +
      0.3345 * t -
      0.060374 * t * t +
      0.0017275 * t ** 3 +
      0.000651814 * t ** 4 +
      0.00002373599 * t ** 5
    );
  }
  if (y < 2050) {
    const t = y - 2000;
    return 62.92 + 0.32217 * t + 0.005589 * t * t;
  }
  if (y < 2150) {
    const u = (y - 1820) / 100;
    return -20 + 32 * u * u - 0.5628 * (2150 - y);
  }
  const u = (y - 1820) / 100;
  return -20 + 32 * u * u;
}

/** 由力学时 JDE 推算小数年（用于查 ΔT）。 */
export function decimalYearOfJde(jde: number): number {
  return 2000 + (jde - J2000) / 365.25;
}

/** 力学时 JDE -> 世界时 JD（约等，忽略 UT1 与 UTC 之差）。 */
export function jdeToJdUt(jde: number): number {
  return jde - deltaTSeconds(decimalYearOfJde(jde)) / 86400;
}

/** 世界时 JD -> 力学时 JDE。 */
export function jdUtToJde(jd: number): number {
  const guess = jd + deltaTSeconds(2000 + (jd - J2000) / 365.25) / 86400;
  return jd + deltaTSeconds(decimalYearOfJde(guess)) / 86400;
}

/** 某一瞬间在指定时区所处的"当地日"日序。 */
export function civilJdnOfJd(jd: number, utcOffsetMinutes: number): number {
  return Math.floor(jd + 0.5 + utcOffsetMinutes / 1440);
}

/** 力学时 JDE 在指定时区所处的"当地日"日序。 */
export function civilJdnOfJde(jde: number, utcOffsetMinutes: number): number {
  return civilJdnOfJd(jdeToJdUt(jde), utcOffsetMinutes);
}

/** 弧度制三角函数的小工具（避免在热点循环里反复乘 PI/180）。 */
export function sinDeg(deg: number): number {
  return Math.sin(deg * D2R);
}

/** 见 sinDeg。 */
export function cosDeg(deg: number): number {
  return Math.cos(deg * D2R);
}
