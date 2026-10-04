/**
 * 日历系统统一出口。
 *
 * 三套历法：
 *  - 公历 GregorianCalendar：民用历，含星期、ISO 周等派生信息，范围不限；
 *  - 农历 ChineseCalendar：天文推算的阴阳合历，含二十四节气、闰月、干支、生肖；
 *  - 元历 YuanCalendar：本项目原创的极简历法（24 小时/日、10 日/旬、3 旬/月、3 月/季、
 *    12 月/年 = 360 日，不设闰年与岁余、不作天文推算），详见 YuanCalendar.ts 的模块注释。
 *
 * 快速上手：
 *   import { CalendarSystem, CalendarClock, CalendarHud } from '../systems/calendar';
 *   const calendars = new CalendarSystem();
 *   const clock = new CalendarClock({ dayLengthSeconds: 1200 }); // 20 分钟一昼夜
 *   const hud = new CalendarHud(calendars, clock, { position: 'top-left', refreshIntervalMs: 250 });
 *   // 每帧：clock.advance(dt); hud.update();
 */
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
