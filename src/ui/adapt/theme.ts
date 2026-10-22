// 主题状态：令牌注入与持久化
import { parseCssColor, contrastRatio, mixHex } from './color';

const KEY = 'qianlin-ui-1018';
const DEFAULTS = { theme: 'auto', primary: '#CB3A56', accent: '#5D3F51' };

export interface ThemeState {
  theme: 'light' | 'dark' | 'auto';
  primary: string;
  accent: string;
}

export const state: ThemeState = { ...(DEFAULTS as ThemeState) };
const listeners = new Set<(s: ThemeState) => void>();
let rows: Array<{ token: string; light: string; dark: string }> = [];
let mediaQuery: MediaQueryList | null = null;

/** 测试环境无 window，推迟取用 */
function darkMedia(): MediaQueryList | null {
  if (mediaQuery === null && typeof window !== 'undefined') mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
  return mediaQuery;
}

export function subscribe(fn: (s: ThemeState) => void): () => void {
  listeners.add(fn);
  return () => listeners.delete(fn);
}

function emit(): void { listeners.forEach(fn => fn(state)); }

function persist(): void {
  try { localStorage.setItem(KEY, JSON.stringify(state)); } catch { /* 忽略隐私模式 */ }
}

function restore(): void {
  try {
    const saved = JSON.parse(localStorage.getItem(KEY) || 'null');
    if (saved) Object.assign(state, saved);
  } catch { /* 忽略损坏数据 */ }
}

export function resolvedTheme(): 'light' | 'dark' {
  return state.theme === 'auto' ? (darkMedia()?.matches ? 'dark' : 'light') : state.theme;
}

/* 主色上的文字按明度自动选 */
const ON_PRIMARY = { paper: '#FBECDE', slate: '#5E616D', ink: '#1D1B1C' };
const SLATE_OK = 4.5;

function pickOnPrimary(bg: string): string {
  const ranked = Object.values(ON_PRIMARY)
    .map(hex => ({ hex, ratio: contrastRatio(bg, hex) }))
    .sort((a, b) => b.ratio - a.ratio);
  const [best, runner] = ranked;
  if (runner.hex === ON_PRIMARY.slate && runner.ratio >= SLATE_OK) return ON_PRIMARY.slate;
  return best.hex;
}

function readToken(name: string): string | null {
  const probe = document.createElement('span');
  probe.style.cssText = `position:absolute;left:-9999px;top:0;width:1px;height:1px;pointer-events:none;background-color:var(${name})`;
  document.documentElement.appendChild(probe);
  const parsed = parseCssColor(getComputedStyle(probe).backgroundColor);
  probe.remove();
  return parsed ? parsed.hex : null;
}

const TEXT_FLOOR: Array<[string, number]> = [['--q-ink', 7], ['--q-ink-2', 4.5], ['--q-ink-3', 2.6]];

function darkenUntil(color: string, bg: string, anchor: string, floor: number): string {
  let lo = 0, hi = 1;
  for (let i = 0; i < 14; i++) {
    const mid = (lo + hi) / 2;
    if (contrastRatio(mixHex(color, anchor, mid), bg) >= floor) hi = mid;
    else lo = mid;
  }
  return mixHex(color, anchor, hi);
}

function clampText(): void {
  const bg = readToken('--q-bg');
  if (!bg) return;
  const anchor = resolvedTheme() === 'dark' ? '#FBECDE' : '#1D1B1C';
  const root = document.documentElement;
  for (const [token, floor] of TEXT_FLOOR) {
    const color = readToken(token);
    if (!color || contrastRatio(color, bg) >= floor) continue;
    root.style.setProperty(token, darkenUntil(color, bg, anchor, floor));
  }
}

/* 主色/装饰色作前景时，按纸面自动压深或提亮 */
const FG_FLOOR = 4.5;
const FG_PAIRS: Array<[string, string]> = [['--q-primary-ui', '--q-primary-text'], ['--q-accent-ui', '--q-accent-text']];

function readableOn(color: string, backgrounds: string[], anchor: string): string {
  const ok = (value: string): boolean => backgrounds.every(bg => contrastRatio(value, bg) >= FG_FLOOR);
  if (ok(color)) return color;
  for (let step = 1; step < 20; step++) {
    const next = mixHex(color, anchor, step / 20);
    if (ok(next)) return next;
  }
  return mixHex(color, anchor, 1);
}

function clampForeground(): void {
  const surface = readToken('--q-surface');
  const primary = readToken('--q-primary-ui');
  const accent = readToken('--q-accent-ui');
  const papers = ['--q-surface', '--q-surface-hi', '--q-bg']
    .map(readToken)
    .filter((value): value is string => value !== null);
  if (!papers.length) return;
  const backgrounds = [...papers];
  if (surface && primary) backgrounds.push(mixHex(surface, primary, .12));
  if (surface && accent) backgrounds.push(mixHex(surface, accent, .1));
  const anchor = resolvedTheme() === 'dark' ? '#ffffff' : '#1d1b1c';
  const root = document.documentElement;
  for (const [source, target] of FG_PAIRS) {
    const color = readToken(source);
    if (color) root.style.setProperty(target, readableOn(color, backgrounds, anchor));
  }
}

function apply(): void {
  const root = document.documentElement;
  const dark = resolvedTheme() === 'dark';
  root.dataset.theme = dark ? 'dark' : 'light';
  for (const row of rows) root.style.setProperty(row.token, dark ? row.dark : row.light);
  root.style.setProperty('--q-primary', state.primary);
  root.style.setProperty('--q-accent', state.accent);
  root.style.setProperty('--q-on-primary', pickOnPrimary(readToken('--q-primary-ui') || state.primary));
  clampText();
  clampForeground();
  // 昼夜切换后由订阅方重算 canvas 与文献色
  document.dispatchEvent(new Event('q-theme-change'));
}

export async function initTheme(tokenRowsData: Array<{ token: string; light: string; dark: string }>): Promise<void> {
  restore();
  rows = tokenRowsData;
  apply();
  darkMedia()?.addEventListener('change', () => { if (state.theme === 'auto') { apply(); emit(); } });
}

export function update(patch: Partial<ThemeState>, options: { animate?: boolean; x?: number; y?: number } = {}): void {
  Object.assign(state, patch);
  if (options.animate) {
    const { x = innerWidth / 2, y = 40 } = options;
    document.documentElement.style.setProperty('--tx', `${x}px`);
    document.documentElement.style.setProperty('--ty', `${y}px`);
    if ((document as Document & { startViewTransition?: (cb: () => void) => void }).startViewTransition) {
      (document as Document & { startViewTransition: (cb: () => void) => void }).startViewTransition(apply);
    } else apply();
  } else apply();
  persist();
  emit();
}

export function toggleTheme(x?: number, y?: number): void {
  const next = resolvedTheme() === 'dark' ? 'light' : 'dark';
  update({ theme: next }, { animate: true, x, y });
}

/** 外部按需读取当前昼夜，不打扰主题状态 */
export function subscribeTheme(fn: (theme: 'light' | 'dark') => void): () => void {
  const handler = () => fn(resolvedTheme());
  document.addEventListener('q-theme-change', handler);
  return () => document.removeEventListener('q-theme-change', handler);
}

export function setThemeMode(mode: 'light' | 'dark' | 'auto'): void {
  update({ theme: mode });
}

/** 外部接管主色，只重算不广播 */
export function adoptPalette(primary: string, accent: string): void {
  if (state.primary === primary && state.accent === accent) return;
  state.primary = primary;
  state.accent = accent;
  persist();
  apply();
}

export function reset(): void {
  update({ ...(DEFAULTS as ThemeState) });
}
