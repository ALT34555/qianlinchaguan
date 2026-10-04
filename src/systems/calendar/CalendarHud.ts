/**
 * 日历 HUD：把公历 / 农历 / 元历（以及节气、干支）显示在屏幕上。
 *
 * 特点：
 *  - 自带样式并在首次创建时注入 <style>，不需要改动 src/ui/style.css；
 *  - 只依赖真实 DOM，不依赖 three.js，可独立挂在任意容器上；
 *  - 元素全部由代码创建（不使用 innerHTML），便于在无浏览器环境下做单测；
 *  - update() 成本很低，可每帧调用，也可用 refreshIntervalMs 让它自行定时刷新。
 *
 * 用法：
 *   const hud = new CalendarHud(calendars, clock, { position: 'top-left' });
 *   // 每帧：hud.update()   （或交给 refreshIntervalMs 自动刷新）
 *   // 关闭：hud.dispose()
 */
import type { CalendarSystem, CalendarClock } from './CalendarSystem';

/** HUD 停靠位置。 */
export type CalendarHudPosition = 'top-left' | 'top-right' | 'bottom-left' | 'bottom-right';

export interface CalendarHudOptions {
  /** 挂载的父节点，默认 document.body。 */
  parent?: HTMLElement;
  /** 停靠位置，默认左上角。 */
  position?: CalendarHudPosition;
  /** 标题文字，默认"历法"。 */
  title?: string;
  /** 右上角按键提示文字，默认不显示。 */
  hotkey?: string;
  /** 是否显示节气行，默认显示。 */
  showSolarTerm?: boolean;
  /** 是否显示时刻 / 旬信息行，默认显示。 */
  showClock?: boolean;
  /** 是否注入内置样式，默认注入。 */
  injectStyles?: boolean;
  /** 自动刷新毫秒数；省略则需手动调用 update()。 */
  refreshIntervalMs?: number;
}

const STYLE_ID = 'calendar-hud-style';

const HUD_CSS = `
.calendar-hud {
  position: fixed;
  z-index: 20;
  min-width: 236px;
  padding: 10px 12px;
  border-radius: 10px;
  background: rgba(12, 14, 20, 0.72);
  box-shadow: 0 4px 18px rgba(0, 0, 0, 0.35);
  font-size: 13px;
  line-height: 1.6;
  color: #e8edf6;
  pointer-events: none;
}
.calendar-hud.top-left { top: 12px; left: 12px; }
.calendar-hud.top-right { top: 12px; right: 12px; }
.calendar-hud.bottom-left { bottom: 12px; left: 12px; }
.calendar-hud.bottom-right { bottom: 12px; right: 12px; }
.calendar-hud-title {
  margin-bottom: 6px;
  font-size: 13px;
  color: #c9d3e6;
}
.calendar-hud-key {
  float: right;
  padding: 0 6px;
  border-radius: 4px;
  background: rgba(255, 255, 255, 0.15);
  font-size: 11px;
}
.calendar-hud-row { display: flex; gap: 8px; }
.calendar-hud-label {
  flex: 0 0 36px;
  color: #8f9bb3;
  font-size: 12px;
}
.calendar-hud-value { flex: 1 1 auto; }
.calendar-hud-gregorian .calendar-hud-value { color: #e8edf6; }
.calendar-hud-chinese .calendar-hud-value { color: #ffd27a; }
.calendar-hud-yuan .calendar-hud-value { color: #9fe0b8; }
.calendar-hud-term .calendar-hud-value { color: #9fc4ff; font-size: 12px; }
.calendar-hud-clock {
  margin-top: 4px;
  font-size: 12px;
  color: #aab4c8;
}
`;

/** 一行"标签 + 数值"。 */
interface HudRow {
  row: HTMLDivElement;
  value: HTMLSpanElement;
}

export class CalendarHud {
  /** 根元素。 */
  readonly element: HTMLDivElement;

  private readonly gregorianRow: HudRow;
  private readonly chineseRow: HudRow;
  private readonly termRow: HudRow | undefined;
  private readonly yuanRow: HudRow;
  private readonly clockEl: HTMLDivElement | undefined;
  private readonly timer: number | undefined;
  private readonly ownerDocument: Document;
  private visible = true;
  private lastText = '';

  constructor(
    private readonly calendars: CalendarSystem,
    private readonly clock: CalendarClock,
    options: CalendarHudOptions = {},
  ) {
    const {
      position = 'top-left',
      title = '历法',
      hotkey,
      showSolarTerm = true,
      showClock = true,
      injectStyles = true,
    } = options;

    this.ownerDocument = (options.parent ?? document.body).ownerDocument;
    if (injectStyles) injectHudStyles(this.ownerDocument);

    const doc = this.ownerDocument;
    this.element = doc.createElement('div');
    this.element.className = `calendar-hud ${position}`;

    const titleEl = doc.createElement('div');
    titleEl.className = 'calendar-hud-title';
    titleEl.textContent = title;
    if (hotkey) {
      const keyEl = doc.createElement('span');
      keyEl.className = 'calendar-hud-key';
      keyEl.textContent = hotkey;
      titleEl.appendChild(keyEl);
    }
    this.element.appendChild(titleEl);

    this.gregorianRow = this.createRow('公历', 'calendar-hud-gregorian');
    this.chineseRow = this.createRow('农历', 'calendar-hud-chinese');
    this.termRow = showSolarTerm ? this.createRow('节气', 'calendar-hud-term') : undefined;
    this.yuanRow = this.createRow('元历', 'calendar-hud-yuan');
    if (showClock) {
      const clockEl = doc.createElement('div');
      clockEl.className = 'calendar-hud-clock';
      this.element.appendChild(clockEl);
      this.clockEl = clockEl;
    }

    (options.parent ?? doc.body).appendChild(this.element);
    this.update();

    this.timer =
      options.refreshIntervalMs && options.refreshIntervalMs > 0
        ? (setInterval(() => this.update(), options.refreshIntervalMs) as unknown as number)
        : undefined;
  }

  private createRow(label: string, className: string): HudRow {
    const doc = this.ownerDocument;
    const row = doc.createElement('div');
    row.className = `calendar-hud-row ${className}`;
    const labelEl = doc.createElement('span');
    labelEl.className = 'calendar-hud-label';
    labelEl.textContent = label;
    const value = doc.createElement('span');
    value.className = 'calendar-hud-value';
    row.appendChild(labelEl);
    row.appendChild(value);
    this.element.appendChild(row);
    return { row, value };
  }

  /** 最近一次刷新得到的整体文本（多行），便于测试与日志复用。 */
  get text(): string {
    return this.lastText;
  }

  get isVisible(): boolean {
    return this.visible;
  }

  /** 挂到另一个父节点上。 */
  mount(parent: HTMLElement): void {
    parent.appendChild(this.element);
  }

  setVisible(visible: boolean): void {
    this.visible = visible;
    this.element.style.display = visible ? '' : 'none';
  }

  toggle(): void {
    this.setVisible(!this.visible);
  }

  /** 按当前时钟刷新文本。 */
  update(): void {
    const snap = this.calendars.snapshotFromUnixMs(this.clock.unixMs);
    const { gregorian, chinese, yuan } = snap;
    const gregorianText = this.calendars.gregorian.format(gregorian, 'chinese');
    const chineseText = `${this.calendars.chinese.format(chinese)}　${chinese.dayGanZhi}日`;
    const yuanText = this.calendars.yuan.format(yuan, 'brief');

    this.gregorianRow.value.textContent = gregorianText;
    this.chineseRow.value.textContent = chineseText;
    this.yuanRow.value.textContent = yuanText;

    const pad = (n: number) => String(n).padStart(2, '0');
    const t = gregorian.time;
    const clockText = `${pad(t.hour)}:${pad(t.minute)}:${pad(t.second)}　元历第 ${yuan.xunOfYear} 旬`;
    if (this.clockEl) this.clockEl.textContent = clockText;

    let termText = '';
    if (this.termRow) {
      if (chinese.solarTerm) {
        termText = `今日 ${chinese.solarTerm}`;
      } else {
        termText = `距${chinese.nextSolarTerm.name} ${chinese.nextSolarTerm.jdn - chinese.jdn} 天`;
      }
      this.termRow.value.textContent = termText;
    }

    const lines = [`公历 ${gregorianText}`, `农历 ${chineseText}`];
    if (termText) lines.push(`节气 ${termText}`);
    lines.push(yuanText);
    if (this.clockEl) lines.push(clockText);
    this.lastText = lines.join('\n');
  }

  /** 移除元素并清理定时器。 */
  dispose(): void {
    if (this.timer !== undefined) clearInterval(this.timer);
    this.element.remove();
  }
}

/** 注入 HUD 内置样式（幂等）。 */
export function injectHudStyles(doc: Document = document): void {
  if (doc.getElementById(STYLE_ID)) return;
  const style = doc.createElement('style');
  style.id = STYLE_ID;
  style.textContent = HUD_CSS;
  doc.head.appendChild(style);
}
