/**
 * 日历系统公共类型。
 *
 * 三套历法（公历 / 农历 / 元历）共用同一个"时刻"基准：
 *   日序 jdn（整数儒略日序，等价于该日正午的儒略日）
 *   + 当日比例 frac（0 = 当地零点，0.5 = 当地正午）。
 *
 * 约定：
 *  - 农历需要天文推算，其日界固定按东八区（UTC+8）判定，与官方农历历书一致；
 *  - 公历与元历属于民用历，日界按可配置时区判定（默认同样为东八区）。
 */

/** 历法种类标识。 */
export type CalendarKind = 'gregorian' | 'chinese' | 'yuan';

/**
 * 世界使用的历法模式（创建世界时选定，之后不可更改，因为季节划分随之而定）：
 *  - 'real'：真实历法，同时使用公历与农历，以四立（立春/立夏/立秋/立冬）划分四季；
 *  - 'yuan'：简化历法（元历），每 3 个月一季、每季恒为 90 日。
 */
export type CalendarMode = 'real' | 'yuan';

/** 一个"日期时刻"：整数日序 + 当日比例。 */
export interface DayTime {
  /** 儒略日序（整数，对应该日正午）。 */
  jdn: number;
  /** 当地时区内自零点起算的比例，取值 [0, 1)。 */
  frac: number;
}

/** 时 / 分 / 秒 / 毫秒。 */
export interface ClockTime {
  hour: number;
  minute: number;
  second: number;
  /** 秒内毫秒数，0 ~ 999。 */
  millisecond: number;
}

/**
 * 季节信息（真实历法与元历共用的统一结构）。
 *
 *  - 真实历法：以四立为季首（交节当日即算入新季，与月建的日级约定一致）；
 *  - 元历：一月至三月为春、四月至六月为夏……每季恒为 90 日。
 */
export interface SeasonInfo {
  /** 季序号：0 = 春，1 = 夏，2 = 秋，3 = 冬。 */
  index: number;
  /** 季名：春 / 夏 / 秋 / 冬。 */
  name: string;
  /** 本季第几日，1 起。 */
  dayOfSeason: number;
  /** 本季总日数（真实历法约 87 ~ 94 日，元历恒为 90 日）。 */
  daysInSeason: number;
  /** 本季季首的日序。 */
  startJdn: number;
  /** 本季内进度 [0, 1)，含当日时刻。 */
  progress: number;
  /** 连续季相 [0, 4) = index + progress；用于季节渐变等环境表现。 */
  phase: number;
  /**
   * 等效太阳视黄经（度，0 ~ 360）。
   * 真实历法为天文推算值；元历按季相折算（季首 = 四立对应的 315° / 45° / 135° / 225°），
   * 便于昼长、日高等环境计算对两种历法统一处理。
   */
  solarLongitude: number;
}

/** 十二时辰信息。 */
export interface ShichenInfo {
  /** 地支序号：0 = 子（23:00—01:00）… 11 = 亥。 */
  index: number;
  /** 时辰名，如"子时"。 */
  name: string;
  /** 时辰的前一小时为"初"、后一小时为"正"（如 23 点为子初、0 点为子正）。 */
  half: '初' | '正';
  /** 刻序号 0 ~ 3（每小时四刻，每刻 15 分钟）。 */
  ke: number;
  /** 完整写法，如"子初三刻""午正初刻"。 */
  label: string;
}

/** 二十四节气中的一项。 */
export interface SolarTermInfo {
  /** 节气名，如"立春""冬至"。 */
  name: string;
  /** 该节气对应的太阳视黄经（度，0 ~ 360）。 */
  longitude: number;
  /** 该节气所在的当地（东八区）日序。 */
  jdn: number;
  /** 交节时刻在当日所占比例（0 = 当日零点，0.5 = 当日正午）。 */
  frac: number;
  /** 是否为中气（十二中气用于置闰）。 */
  isMajor: boolean;
}

/** 公历（格里高利历）日期信息。 */
export interface GregorianDate {
  kind: 'gregorian';
  jdn: number;
  year: number;
  month: number;
  day: number;
  /** 0 = 周日，6 = 周六。 */
  weekday: number;
  weekdayName: string;
  isLeapYear: boolean;
  daysInMonth: number;
  /** 年内第几天，1 起。 */
  dayOfYear: number;
  /** ISO 8601 周序号（周一为一周之首）。 */
  isoWeek: number;
  /** ISO 8601 周所属年份（跨年周会与 year 不同）。 */
  isoWeekYear: number;
  /** 季度，1 ~ 4。 */
  quarter: number;
  time: ClockTime;
}

/** 农历（阴阳合历）日期信息。 */
export interface ChineseDate {
  kind: 'chinese';
  jdn: number;
  /** 农历年号 = 该年春节所在的公历年。 */
  year: number;
  /** 月序 1 ~ 12；闰月与所闰之月同号。 */
  month: number;
  /** 日 1 ~ 30。 */
  day: number;
  isLeapMonth: boolean;
  /** 如"正月""闰四月""腊月"。 */
  monthName: string;
  /** 如"初一""廿三""三十"。 */
  dayName: string;
  /** 年干支，如"丙午"。 */
  yearGanZhi: string;
  /** 生肖，如"马"。 */
  zodiac: string;
  /** 日干支（六十甲子记日，连续不断）。 */
  dayGanZhi: string;
  /** 月柱干支（以节气为界的月建，如"庚寅"）。 */
  monthGanZhi: string;
  /** 本月天数，29 或 30。 */
  daysInMonth: number;
  /** 当日节气名（当日不是节气则为 undefined）。 */
  solarTerm?: string;
  /** 下一个节气。 */
  nextSolarTerm: SolarTermInfo;
}

/** 元历（本项目原创的极简历法：纯算术，不涉及天文机制）日期信息。 */
export interface YuanDate {
  kind: 'yuan';
  jdn: number;
  /** 元历年号（元历元年一月一日 = 公历 2000 年 3 月 20 日）。 */
  year: number;
  /** 月序 1 ~ 12。 */
  month: number;
  /** 月名：孟春 / 仲春 / 季春 … 季冬。 */
  monthName: string;
  /** 月建地支：一月为寅、二月为卯 … 十二月为丑。 */
  monthBranch: string;
  /** 季名：春 / 夏 / 秋 / 冬。 */
  season: string;
  /** 季序号 0 ~ 3。 */
  seasonIndex: number;
  /** 旬序号 0 = 上旬，1 = 中旬，2 = 下旬。 */
  xun: number;
  xunName: string;
  /** 旬内第几日，1 ~ 10。 */
  dayInXun: number;
  /** 月内第几日，1 ~ 30。 */
  day: number;
  /** 如"初一""廿三""三十"。 */
  dayName: string;
  /** 旬内天干日名：甲 / 乙 / 丙 … 癸（一旬十日对应十天干）。 */
  stemName: string;
  /** 年内第几日，1 起。 */
  dayOfYear: number;
  /** 年内总日数，恒为 360。 */
  daysInYear: number;
  /** 年内第几旬，1 起（每年恒为 36 旬）。 */
  xunOfYear: number;
  time: ClockTime;
}

/** 同一时刻在三套历法下的完整快照。 */
export interface CalendarSnapshot {
  instant: DayTime;
  gregorian: GregorianDate;
  chinese: ChineseDate;
  yuan: YuanDate;
  season?: SeasonInfo;
  shichen?: ShichenInfo;
}
