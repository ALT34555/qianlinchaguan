/** 农历编历算法（时宪历规则） */
import {
  civilJdnOfJde as civilJdnOfJdeIn,
  gregorianToJdn,
  gregorianYearOf,
  jdeToJdUt,
  jdUtToJde,
  jdnToGregorian,
  norm360,
  dayTimeFromUnixMs,
} from './JulianDay';
import {
  lunationIndexOf,
  newMoonJde,
  nextMajorTermJde,
  solarTermJde,
  winterSolsticeJde,
} from './ChineseAstronomy';
import {
  LUNAR_MONTH_NAMES,
  MAJOR_TERM_NAMES,
  SOLAR_TERM_NAMES,
  branchName,
  chineseDayName,
  dayGanZhiName,
  ganZhiName,
  stemName,
  yearGanZhiIndex,
  zodiacOfYear,
} from './GanZhi';
import type { ChineseDate, SolarTermInfo } from './types';

/** 农历使用的日界时区偏移：东八区（UTC+8）。 */
export const CHINESE_UTC_OFFSET_MINUTES = 480;

/** 一个农历月。 */
interface LunarMonth {
  /** 月序 1 ~ 12（闰月与所闰之月同号）。 */
  number: number;
  isLeap: boolean;
  /** 初一所在日序（东八区）。 */
  startJdn: number;
  /** 下月初一所在日序（排他上界）。 */
  endJdn: number;
  /** 本月天数，29 或 30。 */
  days: number;
  /** 本月是否含中气。 */
  hasMajorTerm: boolean;
  /** 月名，如"正月""闰四月"。 */
  name: string;
}

/** 一"岁"：自某年十一月至次年十一月之前的月序表。 */
interface SuiTable {
  /** 起点冬至所在的公历年。 */
  solsticeYear: number;
  months: LunarMonth[];
  /** 正月在 months 中的下标。 */
  zhengyueIndex: number;
}

/** 二十四节气的太阳视黄经（小寒起 */
function termLongitude(index: number): number {
  return norm360(285 + 15 * index);
}

export class ChineseCalendar {
  private readonly suiCache = new Map<number, SuiTable>();
  private readonly termCache = new Map<number, SolarTermInfo[]>();

  /** 力学时 JDE 对应的农历日序（东八区日界）。 */
  civilJdnOfJde(jde: number): number {
    return civilJdnOfJdeIn(jde, CHINESE_UTC_OFFSET_MINUTES);
  }

  /** 第 k 个朔所在的东八区日序（即农历初一的日序）。 */
  newMoonJdn(k: number): number {
    return this.civilJdnOfJde(newMoonJde(k));
  }

  /** 不晚于某日序的最后一次朔的序号 k。 */
  lunationIndexBefore(jdn: number): number {
    let k = lunationIndexOf(jdn + 0.5);
    while (this.newMoonJdn(k) > jdn) k -= 1;
    while (this.newMoonJdn(k + 1) <= jdn) k += 1;
    return k;
  }

  /** 取"岁"表 */
  getSui(solsticeYear: number): SuiTable {
    const cached = this.suiCache.get(solsticeYear);
    if (cached) return cached;
    const built = this.buildSui(solsticeYear);
    this.suiCache.set(solsticeYear, built);
    return built;
  }

  private buildSui(solsticeYear: number): SuiTable {
    // 十一月
    const wsJdn = this.civilJdnOfJde(winterSolsticeJde(solsticeYear));
    const startK = this.lunationIndexBefore(wsJdn);
    // 次年十一月作为排他上界
    const nextWsJdn = this.civilJdnOfJde(winterSolsticeJde(solsticeYear + 1));
    const endK = this.lunationIndexBefore(nextWsJdn);

    const months: LunarMonth[] = [];
    for (let k = startK; k < endK; k++) {
      const startJdn = this.newMoonJdn(k);
      const endJdn = this.newMoonJdn(k + 1);
      months.push({
        number: 0,
        isLeap: false,
        startJdn,
        endJdn,
        days: endJdn - startJdn,
        hasMajorTerm: false,
        name: '',
      });
    }

    for (const m of months) m.hasMajorTerm = this.containsMajorTerm(m);

    // 13 个月 => 置闰
    if (months.length !== 12 && months.length !== 13) {
      throw new Error(`农历岁表月数异常（冬至年 ${solsticeYear} 共 ${months.length} 个月）。`);
    }
    let leapIndex = -1;
    if (months.length === 13) {
      leapIndex = months.findIndex((m, i) => i > 0 && !m.hasMajorTerm);
      // 13 个月里必然存在不含中气的月
      if (leapIndex < 0) {
        throw new Error(`农历置闰失败：冬至年 ${solsticeYear} 的 13 个月中找不到无中气之月。`);
      }
    }

    let nextNumber = 11;
    let previousNumber = 11;
    for (let i = 0; i < months.length; i++) {
      const m = months[i]!;
      if (i === leapIndex) {
        m.isLeap = true;
        m.number = previousNumber;
      } else {
        m.number = ((nextNumber - 1) % 12) + 1;
        previousNumber = m.number;
        nextNumber += 1;
      }
      m.name = `${m.isLeap ? '闰' : ''}${LUNAR_MONTH_NAMES[m.number - 1]!}`;
    }

    const zhengyueIndex = months.findIndex((m) => m.number === 1 && !m.isLeap);
    if (zhengyueIndex < 0) {
      throw new Error(`农历置闰失败：冬至年 ${solsticeYear} 的月序表中找不到正月。`);
    }
    return { solsticeYear, months, zhengyueIndex };
  }

  /** 判断某月是否含中气（中气的东八区日序落在本月的日 */
  private containsMajorTerm(m: LunarMonth): boolean {
    // 本月初一 00:00（东八区）对应的力学时
    const startJde = jdUtToJde(m.startJdn - 0.5 - CHINESE_UTC_OFFSET_MINUTES / 1440);
    const { jde } = nextMajorTermJde(startJde);
    const zqJdn = this.civilJdnOfJde(jde);
    return zqJdn >= m.startJdn && zqJdn < m.endJdn;
  }

  /** 在岁表中查找包含某日序的月份 */
  private findMonth(jdn: number): { month: LunarMonth; lunarYear: number } | undefined {
    const g = gregorianYearOf(jdn);
    // // 岁表区间定位
    for (const solsticeYear of [g - 1, g]) {
      const sui = this.getSui(solsticeYear);
      const index = sui.months.findIndex((m) => jdn >= m.startJdn && jdn < m.endJdn);
      if (index < 0) continue;
      // 正月之前的月（十一、十二月）属于本岁起点那一年
      const lunarYear = index < sui.zhengyueIndex ? solsticeYear : solsticeYear + 1;
      return { month: sui.months[index]!, lunarYear };
    }
    return undefined;
  }

  /** 某农历年的春节（正月初一）的日序。 */
  springFestivalJdn(lunarYear: number): number {
    const sui = this.getSui(lunarYear - 1);
    return sui.months[sui.zhengyueIndex]!.startJdn;
  }

  /** 某农历年春节的公历日期。 */
  springFestival(lunarYear: number): { year: number; month: number; day: number } {
    return jdnToGregorian(this.springFestivalJdn(lunarYear));
  }

  /** 某农历年正月初一的公历日期字符串（YYYY-MM */
  springFestivalText(lunarYear: number): string {
    const { year, month, day } = this.springFestival(lunarYear);
    return `${year}-${String(month).padStart(2, '0')}-${String(day).padStart(2, '0')}`;
  }

  /** 由日序求农历日期。 */
  fromJdn(jdn: number): ChineseDate {
    const found = this.findMonth(jdn);
    if (!found) throw new Error(`无法为日序 ${jdn} 定位农历月。`);
    const { month, lunarYear } = found;
    const day = jdn - month.startJdn + 1;
    const today = this.solarTermsOfYear(gregorianYearOf(jdn)).find((t) => t.jdn === jdn);
    const date: ChineseDate = {
      kind: 'chinese',
      jdn,
      year: lunarYear,
      month: month.number,
      day,
      isLeapMonth: month.isLeap,
      monthName: month.name,
      dayName: chineseDayName(day),
      yearGanZhi: ganZhiName(yearGanZhiIndex(lunarYear)),
      zodiac: zodiacOfYear(lunarYear),
      dayGanZhi: dayGanZhiName(jdn),
      monthGanZhi: this.monthGanZhiOf(jdn),
      daysInMonth: month.days,
      nextSolarTerm: this.nextSolarTermAfter(jdn),
    };
    if (today) date.solarTerm = today.name;
    return date;
  }

  /** 由 Unix 毫秒时间戳求农历日期。 */
  fromUnixMs(ms: number): ChineseDate {
    const { jdn } = dayTimeFromUnixMs(ms, CHINESE_UTC_OFFSET_MINUTES);
    return this.fromJdn(jdn);
  }

  /** 月柱干支（以节气为界的"月建" */
  monthGanZhiOf(jdn: number): string {
    const g = gregorianYearOf(jdn);
    // 汇总相邻三年的"节"（不含中气）
    const jie: { jdn: number; offset: number }[] = [];
    for (const year of [g - 1, g, g + 1]) {
      for (const term of this.solarTermsOfYear(year)) {
        if (term.isMajor) continue;
        // 节的视黄经为 285° + 30°k
        const offset = (((Math.round((term.longitude - 315) / 30) % 12) + 12) % 12);
        jie.push({ jdn: term.jdn, offset });
      }
    }
    jie.sort((a, b) => a.jdn - b.jdn);
    let offset = jie[0]!.offset;
    for (const item of jie) {
      if (item.jdn <= jdn) offset = item.offset;
    }
    // 年柱以立春（当年第 3 个节气）为界
    const liChunJdn = this.solarTermsOfYear(g)[2]!.jdn;
    const yearForStem = jdn >= liChunJdn ? g : g - 1;
    const yearStem = (((yearForStem - 4) % 10) + 10) % 10;
    const firstMonthStem = ((yearStem % 5) * 2 + 2) % 10; // 五虎遁：甲己之年丙作首
    return `${stemName((firstMonthStem + offset) % 10)}${branchName(2 + offset)}`;
  }

  /** 某公历年的二十四节气（小寒起）。结果带缓存。 */
  solarTermsOfYear(year: number): SolarTermInfo[] {
    const cached = this.termCache.get(year);
    if (cached) return cached;
    const list: SolarTermInfo[] = [];
    const baseGuess = gregorianToJdn(year, 1, 6) + 0.5;
    for (let i = 0; i < 24; i++) {
      const longitude = termLongitude(i);
      const jde = solarTermJde(longitude, baseGuess + i * 15.2184);
      // 交节时刻换算为东八区当地日与当日比例
      const local = jdeToJdUt(jde) + 0.5 + CHINESE_UTC_OFFSET_MINUTES / 1440;
      const jdn = Math.floor(local);
      list.push({
        name: SOLAR_TERM_NAMES[i]!,
        longitude,
        jdn,
        frac: local - jdn,
        isMajor: i % 2 === 1,
      });
    }
    this.termCache.set(year, list);
    return list;
  }

  /** 某日之后的第一个节气。 */
  nextSolarTermAfter(jdn: number): SolarTermInfo {
    const year = gregorianYearOf(jdn);
    const thisYear = this.solarTermsOfYear(year).find((t) => t.jdn > jdn);
    if (thisYear) return thisYear;
    return this.solarTermsOfYear(year + 1)[0]!;
  }

  /** 某日之后（含当日）的第一个中气。 */
  nextMajorTermAfter(jdn: number): { name: string; jdn: number } {
    const year = gregorianYearOf(jdn);
    const found = this.solarTermsOfYear(year).find((t) => t.isMajor && t.jdn >= jdn);
    if (found) return { name: found.name, jdn: found.jdn };
    const first = this.solarTermsOfYear(year + 1).find((t) => t.isMajor)!;
    return { name: first.name, jdn: first.jdn };
  }

  /** 距离下一个节气还有几天。 */
  daysToNextSolarTerm(jdn: number): number {
    return this.nextSolarTermAfter(jdn).jdn - jdn;
  }

  /** 由太阳视黄经取中气名（供置闰规则展示）。 */
  majorTermNameOf(longitude: number): string {
    const index = (((Math.round((norm360(longitude) - 270) / 30) % 12) + 12) % 12);
    return MAJOR_TERM_NAMES[index]!;
  }

  /** 按指定风格格式化农历日期。 */
  format(date: ChineseDate, style: 'full' | 'month-day' | 'year' = 'full'): string {
    const monthDay = `${date.monthName}${date.dayName}`;
    if (style === 'month-day') return monthDay;
    if (style === 'year') return `${date.yearGanZhi}年`;
    return `${date.yearGanZhi}年（${date.zodiac}）${monthDay}`;
  }
}

/** 默认实例。 */
export const chineseCalendar = new ChineseCalendar();
