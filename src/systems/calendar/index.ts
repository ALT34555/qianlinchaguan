/** 日历系统统一出口 */
export type {
  CalendarKind,
  CalendarSnapshot,
  ChineseDate,
  ClockTime,
  DayTime,
  GregorianDate,
  SolarTermInfo,
  YuanDate,
} from './types';

export {
  DAYS_PER_CENTURY,
  J2000,
  MS_PER_DAY,
  UNIX_EPOCH_JDN,
  civilJdnOfJd,
  civilJdnOfJde,
  clockTimeOf,
  dayTimeFromUnixMs,
  dayTimeOfJulianDay,
  daysInMonth,
  decimalYearOfJde,
  deltaTSeconds,
  gregorianToJdn,
  gregorianYearOf,
  isLeapYear,
  jdUtToJde,
  jdeToJdUt,
  jdnToGregorian,
  julianDayOf,
  norm180,
  norm360,
  unixMsFromDayTime,
  weekdayOf,
} from './JulianDay';

export {
  EARTHLY_BRANCHES,
  HEAVENLY_STEMS,
  LUNAR_MONTH_NAMES,
  MAJOR_TERM_NAMES,
  SOLAR_TERM_NAMES,
  WEEKDAY_NAMES,
  YUAN_MONTH_NAMES,
  YUAN_SEASON_NAMES,
  YUAN_XUN_NAMES,
  ZODIAC_ANIMALS,
  branchName,
  chineseDayName,
  chineseNumber,
  dayGanZhiIndex,
  dayGanZhiName,
  ganZhiName,
  stemName,
  yearGanZhiIndex,
  zodiacOfYear,
} from './GanZhi';

export {
  MEAN_LUNATION_DAYS,
  NEW_MOON_EPOCH_JDE,
  lunarElongationDeg,
  lunationIndexOf,
  moonLongitudeDeg,
  newMoonJde,
  newMoonJdeBefore,
  newMoonJdeOnOrAfter,
  nextMajorTermJde,
  solarTermJde,
  sunApparentLongitudeDeg,
  winterSolsticeJde,
} from './ChineseAstronomy';

export { GregorianCalendar, gregorian, isoWeekOf } from './GregorianCalendar';
export type { GregorianStyle } from './GregorianCalendar';

export { CHINESE_UTC_OFFSET_MINUTES, ChineseCalendar, chineseCalendar } from './ChineseCalendar';

export {
  YUAN_DAYS_PER_MONTH,
  YUAN_DAYS_PER_SEASON,
  YUAN_DAYS_PER_XUN,
  YUAN_DAYS_PER_YEAR,
  YUAN_EPOCH_JDN,
  YUAN_MONTHS_PER_SEASON,
  YUAN_MONTHS_PER_YEAR,
  YUAN_XUNS_PER_MONTH,
  YUAN_XUNS_PER_YEAR,
  YuanCalendar,
  yuanCalendar,
  yuanMonthStartJdn,
  yuanYearDays,
  yuanYearOfJdn,
  yuanYearStartJdn,
} from './YuanCalendar';

export { CalendarClock, CalendarSystem } from './CalendarSystem';
export type { CalendarClockOptions, CalendarSystemOptions } from './CalendarSystem';

export { CalendarHud, injectHudStyles } from './CalendarHud';
export type { CalendarHudOptions, CalendarHudPosition } from './CalendarHud';
