/**
 * 日历系统门面：把公历 / 农历 / 元历三套历法绑在同一个时刻上，
 * 并提供"游戏时间 -> 历法日期"的时钟，供 HUD、存档、日志统一取用。
 *
 * 典型用法：
 *   const calendars = new CalendarSystem();
 *   const clock = new CalendarClock({ epochUnixMs: Date.now(), dayLengthSeconds: 1200 });
 *   // 每帧
 *   clock.advance(dt);
 *   const snap = calendars.snapshotFromUnixMs(clock.unixMs);
 *   console.log(calendars.formatLine(snap));
 */
import { dayTimeFromUnixMs, unixMsFromDayTime } from './JulianDay';
import { GregorianCalendar } from './GregorianCalendar';
import { ChineseCalendar } from './ChineseCalendar';
import { YuanCalendar } from './YuanCalendar';
import type { CalendarSnapshot, DayTime, CalendarMode, ShichenInfo, SeasonInfo } from './types';

/** 日历系统配置。 */
export interface CalendarSystemOptions {
  /** 公历与元历使用的民用时区偏移（分钟），默认东八区 480，与农历一致。 */
  utcOffsetMinutes?: number;
  /** 历法模式：'real' 真实历法 或 'yuan' 简化历法。 */
  mode?: CalendarMode;
}

export class CalendarSystem {
  /** 公历 / 元历的民用时区偏移。 */
  readonly utcOffsetMinutes: number;
  readonly mode: CalendarMode;
  readonly gregorian: GregorianCalendar;
  readonly chinese: ChineseCalendar;
  readonly yuan: YuanCalendar;

  constructor(options: CalendarSystemOptions = {}) {
    this.utcOffsetMinutes = options.utcOffsetMinutes ?? 480;
    this.mode = options.mode ?? 'real';
    this.gregorian = new GregorianCalendar(this.utcOffsetMinutes);
    this.chinese = new ChineseCalendar();
    this.yuan = new YuanCalendar();
  }

  /** 由"日序 + 当日比例"取三历快照。 */
  snapshotFromDayTime(dayTime: DayTime): CalendarSnapshot {
    const snap: CalendarSnapshot = {
      instant: dayTime,
      gregorian: this.gregorian.fromDayTime(dayTime.jdn, dayTime.frac),
      chinese: this.chinese.fromJdn(dayTime.jdn),
      yuan: this.yuan.fromDayTime(dayTime),
    };
    snap.shichen = this.calculateShichen(dayTime);
    snap.season = this.calculateSeason(snap);
    return snap;
  }

  /** 由 Unix 毫秒时间戳取三历快照。 */
  snapshotFromUnixMs(ms: number): CalendarSnapshot {
    return this.snapshotFromDayTime(dayTimeFromUnixMs(ms, this.utcOffsetMinutes));
  }

  /** 取当前系统时刻的三历快照。 */
  snapshotNow(): CalendarSnapshot {
    return this.snapshotFromUnixMs(Date.now());
  }

  /** 由三历快照还原 Unix 毫秒时间戳。 */
  unixMsOf(snapshot: CalendarSnapshot): number {
    return unixMsFromDayTime(snapshot.instant.jdn, snapshot.instant.frac, this.utcOffsetMinutes);
  }

  /** 计算时辰信息。 */
  calculateShichen(dayTime: DayTime): ShichenInfo {
    // 0.0 -> 00:00 (子正), 0.5 -> 12:00 (午正)
    // 1 hour = 1/24 of a day.
    // 23:00 - 01:00 is Zi (index 0).
    // offset by 1 hour (1/24) to make 00:00 fall in the middle of Zi.
    const fracShifted = (dayTime.frac + 1 / 24) % 1;
    const shichenIndex = Math.floor(fracShifted * 12);
    const hourWithinShichen = (fracShifted * 12) % 1; // 0 to 1 (2 hours)
    
    // half: 0 to 0.5 is Chu (初), 0.5 to 1.0 is Zheng (正)
    const half = hourWithinShichen < 0.5 ? '初' : '正';
    // 1 shichen = 2 hours = 8 ke. 1 hour = 4 ke.
    // Ke index within the half: 0 to 3
    const ke = Math.floor((hourWithinShichen % 0.5) * 8);

    const name = ['子', '丑', '寅', '卯', '辰', '巳', '午', '未', '申', '酉', '戌', '亥'][shichenIndex] + '时';
    const keNames = ['初刻', '一刻', '二刻', '三刻'];
    
    return {
      index: shichenIndex,
      name,
      half,
      ke,
      label: `${name.charAt(0)}${half}${keNames[ke]}`
    };
  }

  /** 计算季节信息。 */
  calculateSeason(snap: CalendarSnapshot): SeasonInfo {
    const names = ['春', '夏', '秋', '冬'];
    if (this.mode === 'yuan') {
      const { seasonIndex, month, day } = snap.yuan;
      const daysInSeason = 90;
      const dayOfSeason = ((month - 1) % 3) * 30 + day;
      const progress = (dayOfSeason - 1 + snap.instant.frac) / daysInSeason;
      
      const solarLongitudeOffsets = [315, 45, 135, 225];
      const solarLongitude = (solarLongitudeOffsets[seasonIndex] + progress * 90) % 360;

      return {
        index: seasonIndex,
        name: names[seasonIndex],
        dayOfSeason,
        daysInSeason,
        startJdn: snap.instant.jdn - dayOfSeason + 1,
        progress,
        phase: seasonIndex + progress,
        solarLongitude
      };
    } else {
      // Real mode (use Chinese calendar terms: LiChun, LiXia, LiQiu, LiDong)
      // 315=LiChun, 45=LiXia, 135=LiQiu, 225=LiDong
      const gregorianYear = snap.gregorian.year;
      // Get all terms for this year and previous year just in case
      let terms = [
        ...this.chinese.solarTermsOfYear(gregorianYear - 1),
        ...this.chinese.solarTermsOfYear(gregorianYear),
        ...this.chinese.solarTermsOfYear(gregorianYear + 1)
      ];
      // filter out only the season starts
      const seasonStarts = terms.filter(t => t.longitude === 315 || t.longitude === 45 || t.longitude === 135 || t.longitude === 225);
      
      let currentSeasonStart = seasonStarts[0];
      let nextSeasonStart = seasonStarts[1];
      for (let i = 0; i < seasonStarts.length - 1; i++) {
        if (snap.instant.jdn >= seasonStarts[i].jdn && snap.instant.jdn < seasonStarts[i+1].jdn) {
          currentSeasonStart = seasonStarts[i];
          nextSeasonStart = seasonStarts[i+1];
          break;
        }
      }
      
      let seasonIndex = 0;
      if (currentSeasonStart.longitude === 315) seasonIndex = 0;
      else if (currentSeasonStart.longitude === 45) seasonIndex = 1;
      else if (currentSeasonStart.longitude === 135) seasonIndex = 2;
      else if (currentSeasonStart.longitude === 225) seasonIndex = 3;

      const startJdn = currentSeasonStart.jdn;
      const daysInSeason = nextSeasonStart.jdn - startJdn;
      const dayOfSeason = snap.instant.jdn - startJdn + 1;
      const progress = (dayOfSeason - 1 + snap.instant.frac) / daysInSeason;
      
      // Calculate true solar longitude based on interpolation between solar terms (or just simplified)
      // For a better estimation, find the last term and next term
      let lastTerm = terms[0];
      let nextTerm = terms[1];
      for (let i = 0; i < terms.length - 1; i++) {
        if (snap.instant.jdn >= terms[i].jdn && snap.instant.jdn < terms[i+1].jdn) {
          lastTerm = terms[i];
          nextTerm = terms[i+1];
          break;
        }
      }
      const termLength = nextTerm.jdn - lastTerm.jdn;
      const termProgress = (snap.instant.jdn - lastTerm.jdn + snap.instant.frac) / termLength;
      let solarLongitude = lastTerm.longitude + termProgress * 15;
      if (solarLongitude < 0) solarLongitude += 360;
      if (solarLongitude >= 360) solarLongitude -= 360;

      return {
        index: seasonIndex,
        name: names[seasonIndex],
        dayOfSeason,
        daysInSeason,
        startJdn,
        progress,
        phase: seasonIndex + progress,
        solarLongitude
      };
    }
  }

  /** 单行摘要，如「公历 2026-02-17 星期二 ｜ 农历 丙午年正月初一 ｜ 元历27年 孟春月上旬 初一」。 */
  formatLine(snapshot: CalendarSnapshot): string {
    const g = this.gregorian.format(snapshot.gregorian, 'iso');
    const c = this.chinese.format(snapshot.chinese, 'full');
    const y = this.yuan.format(snapshot.yuan, 'brief');
    return `公历 ${g} ${snapshot.gregorian.weekdayName} ｜ 农历 ${c} ｜ ${y}`;
  }

  /** 多行明细，供调试面板逐行显示。 */
  formatLines(snapshot: CalendarSnapshot): string[] {
    const lines: string[] = [];
    lines.push(`公历：${this.gregorian.format(snapshot.gregorian)}`);
    lines.push(`农历：${this.chinese.format(snapshot.chinese)}　${snapshot.chinese.dayGanZhi}日`);
    if (snapshot.chinese.solarTerm) lines.push(`节气：今日 ${snapshot.chinese.solarTerm}`);
    else lines.push(`节气：距${snapshot.chinese.nextSolarTerm.name}还有 ${snapshot.chinese.nextSolarTerm.jdn - snapshot.chinese.jdn} 天`);
    lines.push(`元历：${this.yuan.format(snapshot.yuan)}`);
    lines.push(
      `时刻：${this.gregorian.formatTime(snapshot.gregorian, true)}` +
        `（元历第 ${snapshot.yuan.xunOfYear} 旬）`,
    );
    return lines;
  }
}

/** 日历时钟配置。 */
export interface CalendarClockOptions {
  /** 游戏内起始时刻（Unix 毫秒）。默认取当前系统时刻。 */
  epochUnixMs?: number;
  /** 游戏内一昼夜对应的现实秒数：86400 表示与现实同速，1200 表示 20 分钟一天。 */
  dayLengthSeconds?: number;
  /** 是否暂停。 */
  paused?: boolean;
}

/**
 * 把现实经过的时间映射为游戏内时刻。
 * 时钟不依赖 three.js，也不读取任何全局状态，便于单独测试。
 */
export class CalendarClock {
  private epochUnixMsValue: number;
  private gameSeconds = 0;
  private dayLengthSecondsValue: number;
  paused: boolean;

  constructor(options: CalendarClockOptions = {}) {
    this.epochUnixMsValue = options.epochUnixMs ?? Date.now();
    this.dayLengthSecondsValue = options.dayLengthSeconds ?? 86400;
    this.paused = options.paused ?? false;
  }

  /** 游戏内一昼夜对应的现实秒数。 */
  get dayLengthSeconds(): number {
    return this.dayLengthSecondsValue;
  }

  set dayLengthSeconds(value: number) {
    if (!Number.isFinite(value) || value <= 0) throw new Error('dayLengthSeconds 必须为正数。');
    this.dayLengthSecondsValue = value;
  }

  /** 当前游戏时刻（Unix 毫秒）。 */
  get unixMs(): number {
    return this.epochUnixMsValue + this.gameSeconds * 1000;
  }

  /** 游戏内已经过的秒数。 */
  get elapsedGameSeconds(): number {
    return this.gameSeconds;
  }

  /** 时间流速：游戏内秒 / 现实秒。 */
  get speed(): number {
    return 86400 / this.dayLengthSecondsValue;
  }

  /** 按现实经过的秒数推进时钟。 */
  advance(realDeltaSeconds: number): void {
    if (this.paused) return;
    if (!Number.isFinite(realDeltaSeconds) || realDeltaSeconds <= 0) return;
    this.gameSeconds += realDeltaSeconds * this.speed;
  }

  /** 直接设置游戏时刻（会重置已过秒数）。 */
  setUnixMs(ms: number): void {
    this.epochUnixMsValue = ms;
    this.gameSeconds = 0;
  }

  /** 在当前时刻基础上平移若干游戏内日。 */
  shiftDays(days: number): void {
    this.gameSeconds += days * 86400;
  }
}
