import { t } from '../i18n';

export const ACTIONS = ['forward', 'back', 'left', 'right', 'jump', 'sprint', 'descend', 'debug'] as const;
export type GameAction = typeof ACTIONS[number];

/**
 * 操作名称。
 *
 * 注意：这里从"导出常量表"改成了函数。文案属于多语言资源，
 * 必须在**渲染时**求值，否则切换语言后已取到的字符串不会更新。
 */
export function actionLabel(action: GameAction): string {
  return t(`action.${action}`);
}

export interface GameSettings {
  fov: number;
  timeSpeed: number;
  viewMode: '2d' | '3d';
  touchControls?: boolean;
  keyBindings: Record<GameAction, string>;
}
export interface StorageLike {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}
export const SETTINGS_KEY = 'qianlin.settings.v1';
export function defaultSettings(): GameSettings {
  return { fov: 75, timeSpeed: 72, viewMode: '2d', touchControls: typeof navigator !== 'undefined' && navigator.maxTouchPoints > 0, keyBindings: {
    forward: 'KeyW', back: 'KeyS', left: 'KeyA', right: 'KeyD', jump: 'Space',
    sprint: 'ShiftLeft', descend: 'ControlLeft', debug: 'F12',
  } };
}
export function validBinding(code: unknown): code is string {
  return typeof code === 'string' && /^(Key[A-Z]|Digit[0-9]|Arrow(Up|Down|Left|Right)|Space|Shift(Left|Right)|Control(Left|Right)|F([1-9]|1[0-2])|Enter|Backspace|Tab|Backquote|Minus|Equal|BracketLeft|BracketRight|Backslash|Semicolon|Quote|Comma|Period|Slash)$/.test(code);
}
function canonicalKey(code: string): string { return code.replace(/(Shift|Control)(Left|Right)/, '$1'); }
export function validateSettings(value: GameSettings): GameSettings {
  if (!Number.isFinite(value.fov) || value.fov < 30 || value.fov > 120) throw new Error(t('error.fov'));
  if (!Number.isFinite(value.timeSpeed) || value.timeSpeed < 0 || value.timeSpeed > 86400) throw new Error(t('error.timeSpeed'));
  if (!['2d', '3d'].includes(value.viewMode)) throw new Error(t('error.viewMode'));
  const used = new Set<string>();
  for (const action of ACTIONS) {
    const code = value.keyBindings?.[action];
    if (!validBinding(code)) throw new Error(t('error.invalidBinding', { action: actionLabel(action) }));
    const key = canonicalKey(code);
    if (used.has(key)) throw new Error(t('error.duplicateBinding'));
    used.add(key);
  }
  return { fov: value.fov, timeSpeed: value.timeSpeed, viewMode: value.viewMode, touchControls: typeof value.touchControls === 'boolean' ? value.touchControls : defaultSettings().touchControls, keyBindings: { ...value.keyBindings } };
}
export function loadSettings(storage: StorageLike): GameSettings {
  try {
    const raw = storage.getItem(SETTINGS_KEY);
    return raw ? validateSettings(JSON.parse(raw)) : defaultSettings();
  } catch { return defaultSettings(); }
}
export function saveSettings(storage: StorageLike, value: GameSettings): GameSettings {
  const result = validateSettings(value);
  storage.setItem(SETTINGS_KEY, JSON.stringify(result));
  return result;
}
export function actionCodes(action: GameAction, bindings: GameSettings['keyBindings']): string[] {
  const code = bindings[action];
  if (/^(Shift|Control)(Left|Right)$/.test(code)) return [code.replace(/(Left|Right)$/, 'Left'), code.replace(/(Left|Right)$/, 'Right')];
  // 方向键作为默认 WASD 的快捷替代；改键后让出对应方向键。
  const arrows: Partial<Record<GameAction, string>> = { forward: 'ArrowUp', back: 'ArrowDown', left: 'ArrowLeft', right: 'ArrowRight' };
  const arrow = arrows[action];
  if (arrow && code === defaultSettings().keyBindings[action] && !Object.values(bindings).includes(arrow)) return [code, arrow];
  return [code];
}
/** 按键显示名；未在语言包中登记时回退为 KeyA → A、Digit1 → 1 的简写。 */
export function keyLabel(code: string): string {
  const key = `key.${code}`;
  const label = t(key);
  return label === key ? code.replace(/^Key|^Digit/, '') : label;
}
