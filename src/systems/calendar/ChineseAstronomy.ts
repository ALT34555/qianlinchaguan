/** 农历所需的太阳 / 月球位置推算 */
import { J2000, DAYS_PER_CENTURY, norm360, norm180, gregorianToJdn, sinDeg } from './JulianDay';

/** 平均朔望月长度（日）。 */
export const MEAN_LUNATION_DAYS = 29.530588861;

/** k = 0 的平朔时刻（JDE */
export const NEW_MOON_EPOCH_JDE = 2451550.25981;

/** 月日视黄经差的平均变化率（度 / 日） */
const MEAN_ELONGATION_RATE = 12.190749;

/** 太阳视黄经的平均变化率（度 / 日）。 */
const MEAN_SUN_RATE = 0.98564736;

/** 月球黄经周期项（Meeus 表 47.A 的经度 */
const MOON_LONGITUDE_TERMS: readonly (readonly [number, number, number, number, number])[] = [
  [0, 0, 1, 0, 6288774],
  [2, 0, -1, 0, 1274027],
  [2, 0, 0, 0, 658314],
  [0, 0, 2, 0, 213618],
  [0, 1, 0, 0, -185116],
  [0, 0, 0, 2, -114332],
  [2, 0, -2, 0, 58793],
  [2, -1, -1, 0, 57066],
  [2, 0, 1, 0, 53322],
  [2, -1, 0, 0, 45758],
  [0, 1, -1, 0, -40923],
  [1, 0, 0, 0, -34720],
  [0, 1, 1, 0, -30383],
  [2, 0, 0, -2, 15327],
  [0, 0, 1, 2, -12528],
  [0, 0, 1, -2, 10980],
  [4, 0, -1, 0, 10675],
  [0, 0, 3, 0, 10034],
  [4, 0, -2, 0, 8548],
  [2, 1, -1, 0, -7888],
  [2, 1, 0, 0, -6766],
  [1, 0, -1, 0, -5163],
  [1, 1, 0, 0, 4987],
  [2, -1, 1, 0, 4036],
  [2, 0, 2, 0, 3994],
  [4, 0, 0, 0, 3861],
  [2, 0, -3, 0, 3665],
  [0, 1, -2, 0, -2689],
  [2, 0, -1, 2, -2602],
  [2, -1, -2, 0, 2390],
  [1, 0, 1, 0, -2348],
  [2, -2, 0, 0, 2236],
  [0, 1, 2, 0, -2120],
  [0, 2, 0, 0, -2069],
  [2, -2, -1, 0, 2048],
  [2, 0, 1, -2, -1773],
  [2, 0, 0, 2, -1595],
  [4, -1, -1, 0, 1215],
  [0, 0, 2, 2, -1110],
  [3, 0, -1, 0, -892],
  [2, 1, 1, 0, -810],
  [4, -1, -2, 0, 759],
  [0, 2, -1, 0, -713],
  [2, 2, -1, 0, -700],
  [2, 1, -2, 0, 691],
  [2, -1, 0, -2, 596],
  [4, 0, 1, 0, 549],
  [0, 0, 4, 0, 537],
  [4, -1, 0, 0, 520],
  [1, 0, -2, 0, -487],
  [2, 1, 0, -2, -399],
  [0, 0, 2, -2, -381],
  [1, 1, 1, 0, 351],
  [3, 0, -2, 0, -340],
  [4, 0, -3, 0, 330],
  [2, -1, 2, 0, 327],
  [0, 2, 1, 0, -323],
  [1, 1, -1, 0, 299],
  [2, 0, 3, 0, 294],
  [2, 0, -1, -2, 0],
];

/** 月亮平近点角与升交点等基本引数（单位：度）。 */
function moonArguments(T: number): { lp: number; d: number; m: number; mp: number; f: number } {
  return {
    // 月球平黄经
    lp: 218.3164477 + 481267.88123421 * T - 0.0015786 * T * T + T ** 3 / 538841 - T ** 4 / 65194000,
    // 日月平距角
    d: 297.8501921 + 445267.1114034 * T - 0.0018819 * T * T + T ** 3 / 545868 - T ** 4 / 113065000,
    // 太阳平近点角
    m: 357.5291092 + 35999.0502909 * T - 0.0001536 * T * T + T ** 3 / 24490000,
    // 月球平近点角
    mp: 134.9633964 + 477198.8675055 * T + 0.0087414 * T * T + T ** 3 / 69699 - T ** 4 / 14712000,
    // 月球升交点平黄经（纬度参数）
    f: 93.272095 + 483202.0175233 * T - 0.0036539 * T * T - T ** 3 / 3526000 + T ** 4 / 863310000,
  };
}

/** 月球地心视黄经（度） */
export function moonLongitudeDeg(jde: number): number {
  const T = (jde - J2000) / DAYS_PER_CENTURY;
  const { lp, d, m, mp, f } = moonArguments(T);
  const e = 1 - 0.002516 * T - 0.0000074 * T * T;
  const e2 = e * e;
  const a1 = 119.75 + 131.849 * T;
  const a2 = 53.09 + 479264.29 * T;

  // 附加项（金星 / 木星摄动与地球扁率项）
  let sum = 3958 * sinDeg(a1) + 1962 * sinDeg(lp - f) + 318 * sinDeg(a2);

  for (let i = 0; i < MOON_LONGITUDE_TERMS.length; i++) {
    const t = MOON_LONGITUDE_TERMS[i]!;
    const arg = t[0] * d + t[1] * m + t[2] * mp + t[3] * f;
    let coeff = t[4];
    if (t[1] === 1 || t[1] === -1) coeff *= e;
    else if (t[1] === 2 || t[1] === -2) coeff *= e2;
    sum += coeff * sinDeg(arg);
  }
  return norm360(lp + sum * 1e-6);
}

/** 太阳视黄经（度） */
export function sunApparentLongitudeDeg(jde: number): number {
  const T = (jde - J2000) / DAYS_PER_CENTURY;
  const L0 = 280.46646 + 36000.76983 * T + 0.0003032 * T * T;
  const M = 357.52911 + 35999.05029 * T - 0.0001537 * T * T;
  const C =
    (1.914602 - 0.004817 * T - 0.000014 * T * T) * sinDeg(M) +
    (0.019993 - 0.000101 * T) * sinDeg(2 * M) +
    0.000289 * sinDeg(3 * M);
  const trueLongitude = L0 + C;
  const omega = 125.04 - 1934.136 * T;
  // -0.00569° 为光行差
  return norm360(trueLongitude - 0.00569 - 0.00478 * sinDeg(omega));
}

/** 日月视黄经差（度） */
export function lunarElongationDeg(jde: number): number {
  return norm180(moonLongitudeDeg(jde) - sunApparentLongitudeDeg(jde));
}

/** 由近似时刻估算朔望月序号 k（k = 0 对应 */
export function lunationIndexOf(jd: number): number {
  return Math.round((jd - NEW_MOON_EPOCH_JDE) / MEAN_LUNATION_DAYS);
}

/** 第 k 个朔（新月）的力学时儒略日 */
export function newMoonJde(k: number): number {
  let jde = NEW_MOON_EPOCH_JDE + MEAN_LUNATION_DAYS * k;
  for (let i = 0; i < 12; i++) {
    const diff = lunarElongationDeg(jde);
    jde -= diff / MEAN_ELONGATION_RATE;
    if (Math.abs(diff) < 1e-7) break;
  }
  return jde;
}

/** 不晚于给定时刻（JDE 或 JD */
export function newMoonJdeBefore(jd: number): number {
  let k = lunationIndexOf(jd);
  let jde = newMoonJde(k);
  while (jde > jd) {
    k -= 1;
    jde = newMoonJde(k);
  }
  while (newMoonJde(k + 1) <= jd) {
    k += 1;
    jde = newMoonJde(k);
  }
  return jde;
}

/** 不早于给定时刻的那一次朔。 */
export function newMoonJdeOnOrAfter(jd: number): number {
  let k = lunationIndexOf(jd);
  let jde = newMoonJde(k);
  while (jde < jd) {
    k += 1;
    jde = newMoonJde(k);
  }
  while (newMoonJde(k - 1) >= jd) {
    k -= 1;
    jde = newMoonJde(k);
  }
  return jde;
}

/** 求太阳视黄经等于指定值（度）的时刻 */
export function solarTermJde(targetLongitude: number, guessJde: number): number {
  const target = norm360(targetLongitude);
  let jde = guessJde;
  for (let i = 0; i < 20; i++) {
    const diff = norm180(sunApparentLongitudeDeg(jde) - target);
    jde -= diff / MEAN_SUN_RATE;
    if (Math.abs(diff) < 1e-8) break;
  }
  return jde;
}

/** 某公历年的冬至时刻（力学时 JDE） */
export function winterSolsticeJde(year: number): number {
  return solarTermJde(270, gregorianToJdn(year, 12, 21) + 0.5);
}

/** 不早于给定时刻的下一个中气 */
export function nextMajorTermJde(jd: number): { longitude: number; jde: number; index: number } {
  const lon = sunApparentLongitudeDeg(jd);
  const k = Math.ceil((lon - 270) / 30 - 1e-9);
  const target = norm360(270 + 30 * k);
  const guess = jd + norm360(target - lon) / MEAN_SUN_RATE;
  return { longitude: target, jde: solarTermJde(target, guess), index: ((k % 12) + 12) % 12 };
}
