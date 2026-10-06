/** 干支、生肖与中文数字日名等命名工具 */

/** 十天干。 */
export const HEAVENLY_STEMS = ['甲', '乙', '丙', '丁', '戊', '己', '庚', '辛', '壬', '癸'] as const;

/** 十二地支。 */
export const EARTHLY_BRANCHES = ['子', '丑', '寅', '卯', '辰', '巳', '午', '未', '申', '酉', '戌', '亥'] as const;

/** 十二生肖（与地支一一对应）。 */
export const ZODIAC_ANIMALS = ['鼠', '牛', '虎', '兔', '龙', '蛇', '马', '羊', '猴', '鸡', '狗', '猪'] as const;

/** 公历星期名，下标 0 = 周日。 */
export const WEEKDAY_NAMES = ['星期日', '星期一', '星期二', '星期三', '星期四', '星期五', '星期六'] as const;

/** 二十四节气名称 */
export const SOLAR_TERM_NAMES = [
  '小寒', '大寒', '立春', '雨水', '惊蛰', '春分',
  '清明', '谷雨', '立夏', '小满', '芒种', '夏至',
  '小暑', '大暑', '立秋', '处暑', '白露', '秋分',
  '寒露', '霜降', '立冬', '小雪', '大雪', '冬至',
] as const;

/** 十二中气（用于农历置闰） */
export const MAJOR_TERM_NAMES = [
  '冬至', '大寒', '雨水', '春分', '谷雨', '小满',
  '夏至', '大暑', '处暑', '秋分', '霜降', '小雪',
] as const;

/** 农历月名（下标 0 = 正月）。 */
export const LUNAR_MONTH_NAMES = [
  '正月', '二月', '三月', '四月', '五月', '六月',
  '七月', '八月', '九月', '十月', '冬月', '腊月',
] as const;

/** 元历月名：每季三个月依次为孟 / 仲 / 季。 */
export const YUAN_MONTH_NAMES = [
  '孟春', '仲春', '季春', '孟夏', '仲夏', '季夏',
  '孟秋', '仲秋', '季秋', '孟冬', '仲冬', '季冬',
] as const;

/** 四季名（真实历法与元历共用） */
export const SEASON_NAMES = ['春', '夏', '秋', '冬'] as const;

/** 元历季名（与 SEASON_NAMES 为同一张表 */
export const YUAN_SEASON_NAMES = SEASON_NAMES;

/** 元历旬名。 */
export const YUAN_XUN_NAMES = ['上旬', '中旬', '下旬'] as const;

/** 一小时内四刻的名称（每刻 15 分钟）。 */
export const SHICHEN_KE_NAMES = ['初刻', '一刻', '二刻', '三刻'] as const;

/** 中文数字 0 ~ 30 的写法。 */
const CHINESE_DIGITS = ['〇', '一', '二', '三', '四', '五', '六', '七', '八', '九'];

/** 数字（0 ~ 99）转中文。 */
export function chineseNumber(value: number): string {
  if (!Number.isFinite(value)) return String(value);
  const n = Math.floor(Math.abs(value));
  if (n < 10) return CHINESE_DIGITS[n]!;
  if (n < 20) return n === 10 ? '十' : `十${CHINESE_DIGITS[n % 10]}`;
  if (n < 100) {
    const tens = Math.floor(n / 10);
    const ones = n % 10;
    return `${CHINESE_DIGITS[tens]}十${ones === 0 ? '' : CHINESE_DIGITS[ones]}`;
  }
  return String(n);
}

/** 农历 / 元历的月内日名 */
export function chineseDayName(day: number): string {
  if (day < 1) return '—';
  if (day <= 10) return `初${CHINESE_DIGITS[day]}`;
  if (day < 20) return `十${day === 10 ? '' : CHINESE_DIGITS[day % 10]}`;
  if (day === 20) return '二十';
  if (day < 30) return `廿${CHINESE_DIGITS[day % 10]}`;
  if (day === 30) return '三十';
  return String(day);
}

/** 六十甲子序号 -> 干支名 */
export function ganZhiName(index: number): string {
  const i = ((index % 60) + 60) % 60;
  return `${HEAVENLY_STEMS[i % 10]}${EARTHLY_BRANCHES[i % 12]}`;
}

/** 六十甲子序号 -> 仅天干 / 仅地支 */
export function stemName(index: number): string {
  return HEAVENLY_STEMS[((index % 10) + 10) % 10]!;
}

/** 见 stemName。 */
export function branchName(index: number): string {
  return EARTHLY_BRANCHES[((index % 12) + 12) % 12]!;
}

/** 由公元年份求"年干支"序号（0 = 甲子） */
export function yearGanZhiIndex(year: number): number {
  return (((year - 4) % 60) + 60) % 60;
}

/** 由公元年份取生肖。 */
export function zodiacOfYear(year: number): string {
  const idx = yearGanZhiIndex(year);
  return ZODIAC_ANIMALS[idx % 12]!;
}

/** 连续日干支序号 */
export function dayGanZhiIndex(jdn: number): number {
  return (((jdn + 49) % 60) + 60) % 60;
}

/** 由日序取日干支名。 */
export function dayGanZhiName(jdn: number): string {
  return ganZhiName(dayGanZhiIndex(jdn));
}
