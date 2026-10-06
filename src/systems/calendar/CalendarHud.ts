/** 日历 HUD */
import type { CalendarSystem, CalendarClock } from './CalendarSystem';
import { t } from '../../i18n';

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
  /** 自动刷新毫秒数 */
  refreshIntervalMs?: number;
}

const STYLE_ID = 'calendar-hud-style';

const HUD_CSS = `
/* 配色引用 theme.css 的令牌，因此自动跟随"古朴中式"主题与语言切换。 */
.calendar-hud {
  position: fixed;
  z-index: 20;
  min-width: 236px;
  padding: 10px 13px;
  border-radius: var(--ql-radius, 3px);
  background: var(--ql-paper, #e5d3aa);
  color: var(--ql-ink, #4d4030);
  box-shadow:
    inset 0 0 0 2px var(--ql-paper, #e5d3aa),
    inset 0 0 0 3px var(--ql-line, rgba(77, 64, 48, .3)),
    0 4px 18px rgba(58, 47, 33, .28);
  font-family: var(--ql-font-body, system-ui, sans-serif);
  font-size: 13px;
  line-height: 1.6;
  pointer-events: none;
}
.calendar-hud.top-left { top: 12px; left: 12px; }
.calendar-hud.top-right { top: 12px; right: 12px; }
.calendar-hud.bottom-left { bottom: 12px; left: 12px; }
.calendar-hud.bottom-right { bottom: 12px; right: 12px; }
.calendar-hud-title {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 6px;
  padding-bottom: 5px;
  border-bottom: 1px solid var(--ql-line-soft, rgba(77, 64, 48, .16));
  color: var(--ql-cinnabar, #a61b29);
  font-family: var(--ql-font-display, serif);
  font-size: 13px;
  letter-spacing: var(--ql-track-label, .14em);
}
.calendar-hud-key {
  padding: 0 6px;
  border-radius: var(--ql-radius, 3px);
  background: var(--ql-seal-on, #894e54);
  color: var(--ql-ink-inv, #fbf4e3);
  font-family: var(--ql-font-body, system-ui, sans-serif);
  font-size: 10.5px;
  letter-spacing: var(--ql-track-body, .02em);
}
.calendar-hud-row { display: flex; gap: 8px; }
.calendar-hud-label {
  flex: 0 0 auto;
  min-width: 38px;
  color: var(--ql-ink-3, #8a7a5f);
  font-size: 12px;
}
/* 拉丁文字比两个汉字宽，英文下放宽标签栏 */
:lang(en) .calendar-hud-label { min-width: 66px; }
.calendar-hud-value { flex: 1 1 auto; }
.calendar-hud-gregorian .calendar-hud-value { color: var(--ql-ink, #4d4030); font-family: var(--ql-font-mono, monospace); }
.calendar-hud-chinese .calendar-hud-value { color: var(--ql-cinnabar, #a61b29); }
.calendar-hud-yuan .calendar-hud-value { color: var(--ql-seal-on, #894e54); }
.calendar-hud-term .calendar-hud-value { color: var(--ql-ink-2, #6a5942); font-size: 12px; }
.calendar-hud-clock {
  margin-top: 5px;
  padding-top: 5px;
  border-top: 1px solid var(--ql-line-faint, rgba(77, 64, 48, .09));
  font-size: 12px;
  color: var(--ql-ink-2, #6a5942);
  font-family: var(--ql-font-mono, monospace);
}
`;

/** 一行"标签 + 数值"。 */
interface HudRow {
  row: HTMLDivElement;
  label: HTMLSpanElement;
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
  private readonly titleEl: HTMLDivElement;
  /** 标题语言包键 */
  private readonly titleText: string;
  private readonly titleKey: string | null;
  private visible = true;
  private lastText = '';

  constructor(
    private readonly calendars: CalendarSystem,
    private readonly clock: CalendarClock,
    options: CalendarHudOptions = {},
  ) {
    const {
      position = 'top-left',
      title,
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

    this.titleKey = title === undefined ? 'hud.title' : null;
    this.titleText = title ?? t('hud.title');
    const titleEl = doc.createElement('div');
    titleEl.className = 'calendar-hud-title';
    titleEl.textContent = this.titleText;
    this.titleEl = titleEl;
    if (hotkey) {
      const keyEl = doc.createElement('span');
      keyEl.className = 'calendar-hud-key';
      keyEl.textContent = hotkey;
      titleEl.appendChild(keyEl);
    }
    this.element.appendChild(titleEl);

    this.gregorianRow = this.createRow('hud.gregorian', 'calendar-hud-gregorian');
    this.chineseRow = this.createRow('hud.chinese', 'calendar-hud-chinese');
    this.termRow = showSolarTerm ? this.createRow('hud.solarTerm', 'calendar-hud-term') : undefined;
    this.yuanRow = this.createRow('hud.yuan', 'calendar-hud-yuan');
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

  private createRow(labelKey: string, className: string): HudRow {
    const doc = this.ownerDocument;
    const row = doc.createElement('div');
    row.className = `calendar-hud-row ${className}`;
    const labelEl = doc.createElement('span');
    labelEl.className = 'calendar-hud-label';
    labelEl.textContent = t(labelKey);
    const value = doc.createElement('span');
    value.className = 'calendar-hud-value';
    row.appendChild(labelEl);
    row.appendChild(value);
    this.element.appendChild(row);
    return { row, label: labelEl, value };
  }

  /** 语言变更后刷新标题与行标签 */
  applyLocale(): void {
    if (this.titleKey) this.titleEl.textContent = t(this.titleKey);
    else this.titleEl.textContent = this.titleText;
    this.gregorianRow.label.textContent = t('hud.gregorian');
    this.chineseRow.label.textContent = t('hud.chinese');
    if (this.termRow) this.termRow.label.textContent = t('hud.solarTerm');
    this.yuanRow.label.textContent = t('hud.yuan');
    this.update();
  }

  /** 最近一次刷新得到的整体文本（多行） */
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
    // 注意
    const clockTime = gregorian.time;
    const clockText = t('hud.clock', { time: `${pad(clockTime.hour)}:${pad(clockTime.minute)}:${pad(clockTime.second)}`, xun: yuan.xunOfYear });
    if (this.clockEl) this.clockEl.textContent = clockText;

    let termText = '';
    if (this.termRow) {
      if (chinese.solarTerm) {
        termText = t('hud.termToday', { term: chinese.solarTerm });
      } else {
        termText = t('hud.termNext', { term: chinese.nextSolarTerm.name, days: chinese.nextSolarTerm.jdn - chinese.jdn });
      }
      this.termRow.value.textContent = termText;
    }

    const lines = [t('hud.line.gregorian', { value: gregorianText }), t('hud.line.chinese', { value: chineseText })];
    if (termText) lines.push(t('hud.line.solarTerm', { value: termText }));
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
