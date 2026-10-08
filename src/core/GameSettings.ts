import { defaultMysterySettings } from './MysterySettings.ts';
import { renName } from '../entities/PlayerIdentity';
import { t } from '../i18n';
import { DEFAULT_RENDER_DISTANCE } from './config';
import { MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE } from './RenderDistance';

export const ACTIONS = ['forward', 'back', 'left', 'right', 'jump', 'sprint', 'descend', 'debug'] as const;
export type GameAction = typeof ACTIONS[number];

/** 操作名称 */
export function actionLabel(action: GameAction): string {
  return t(`action.${action}`);
}

export interface GameSettings {
  ren_ming?: string;
  font?: string;
  safeX?: number;
  safeY?: number;
  uiScale?: number;
  textScale?: number;
  vegetationDistance?: number;
  resolution?: string;
  touchOpacity?: number;
  crosshair?: 'none' | 'dot' | 'cross' | 'aim' | 'ring';
  touchLayout?: Record<'move' | 'actions', { x: number; y: number }>;
  fov: number;
  renderDistance: number;
  timeSpeed: number;
  viewMode: '2d' | '3d';
  touchControls?: boolean;
  particles?: boolean;
  showTopBar?: boolean;
  menuAutoPause?: boolean;
  keyBindings: Record<GameAction, string>;
}
export interface StorageLike {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}
export const SETTINGS_KEY = 'qianlin.settings.v1';
export function defaultSettings(): GameSettings {
  return { ren_ming: '赤', font: 'system', safeX: 0, safeY: 0, uiScale: 1, textScale: 1.3, vegetationDistance: DEFAULT_RENDER_DISTANCE, resolution: 'auto', touchOpacity: 1, crosshair: 'dot', touchLayout: { move: { x: 4, y: 74 }, actions: { x: 80, y: 78 } }, fov: 75, renderDistance: DEFAULT_RENDER_DISTANCE, timeSpeed: 72, viewMode: '2d', touchControls: typeof navigator !== 'undefined' && navigator.maxTouchPoints > 0, particles: true, showTopBar: true, menuAutoPause: false, keyBindings: {
    forward: 'KeyW', back: 'KeyS', left: 'KeyA', right: 'KeyD', jump: 'Space',
    sprint: 'ShiftLeft', descend: 'ControlLeft', debug: 'F12',
  } };
}
export function validBinding(code: unknown): code is string {
  return typeof code === 'string' && /^(Key[A-Z]|Digit[0-9]|Arrow(Up|Down|Left|Right)|Space|Shift(Left|Right)|Control(Left|Right)|F([1-9]|1[0-2])|Enter|Backspace|Tab|Backquote|Minus|Equal|BracketLeft|BracketRight|Backslash|Semicolon|Quote|Comma|Period|Slash)$/.test(code);
}
function canonicalKey(code: string): string { return code.replace(/(Shift|Control)(Left|Right)/, '$1'); }
export function validateSettings(value: GameSettings): GameSettings {
  const renderDistance = value.renderDistance === undefined ? DEFAULT_RENDER_DISTANCE : value.renderDistance;
  if (!Number.isInteger(renderDistance) || renderDistance < MIN_RENDER_DISTANCE || renderDistance > MAX_RENDER_DISTANCE) throw new Error(t('error.renderDistance'));
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
  const defaults = defaultSettings();
  const range = (n: number | undefined, fallback: number, min: number, max: number) => {
    const result = n ?? fallback;
    if (!Number.isFinite(result) || result < min || result > max) throw new Error(t('settings.invalidRange', { min, max }));
    return result;
  };
  const requestedVegetation = range(value.vegetationDistance, renderDistance, 2, 16);
  if (!Number.isInteger(requestedVegetation)) throw new Error(t('error.renderDistance'));
  const vegetationDistance = Math.min(requestedVegetation, renderDistance);
  const font = value.font ?? 'system', crosshair = value.crosshair ?? 'dot', resolution = value.resolution ?? 'auto';
  if (!(typeof font === 'string' && /^(system|song|round|mod:[^\x00-\x1f]{1,200})$/.test(font)) || !['none', 'dot', 'cross', 'aim', 'ring'].includes(crosshair)
    || !['auto', '320x240', '640x480', '960x540', '1280x720', '1920x1080', '2560x1440'].includes(resolution)) throw new Error(t('settings.invalidChoice'));
  const layout = value.touchLayout ?? defaults.touchLayout!;
  const touchLayout = Object.fromEntries(['move', 'actions'].map(key => {
    const point = layout[key as keyof typeof layout];
    return [key, { x: range(point?.x, defaults.touchLayout![key as keyof typeof layout].x, 0, 100), y: range(point?.y, defaults.touchLayout![key as keyof typeof layout].y, 0, 100) }];
  })) as NonNullable<GameSettings['touchLayout']>;
  return { ren_ming: renName(value.ren_ming), font, crosshair, resolution, vegetationDistance, touchLayout,
    safeX: range(value.safeX, 0, 0, 35), safeY: range(value.safeY, 0, 0, 35), uiScale: range(value.uiScale, 1, .25, 4), textScale: range(value.textScale, 1.3, .25, 4), touchOpacity: range(value.touchOpacity, 1, .1, 1),
    fov: value.fov, renderDistance, timeSpeed: value.timeSpeed, viewMode: value.viewMode, touchControls: typeof value.touchControls === 'boolean' ? value.touchControls : defaults.touchControls, showTopBar: typeof value.showTopBar === 'boolean' ? value.showTopBar : true, particles: typeof value.particles === 'boolean' ? value.particles : true, menuAutoPause: typeof value.menuAutoPause === 'boolean' ? value.menuAutoPause : false, keyBindings: { ...value.keyBindings } };
}
export function loadSettings(storage: StorageLike): GameSettings {
  try {
    const raw = storage.getItem(SETTINGS_KEY);
    return raw ? validateSettings({ ...JSON.parse(raw), ...defaultMysterySettings() }) : defaultSettings();
  } catch { return defaultSettings(); }
}
export function saveSettings(storage: StorageLike, value: GameSettings): GameSettings {
  const result = validateSettings(value);
  const { timeSpeed, menuAutoPause, ...global } = result;
  storage.setItem(SETTINGS_KEY, JSON.stringify(global));
  return { ...result, ...defaultMysterySettings() };
}
export function actionCodes(action: GameAction, bindings: GameSettings['keyBindings']): string[] {
  const code = bindings[action];
  if (/^(Shift|Control)(Left|Right)$/.test(code)) return [code.replace(/(Left|Right)$/, 'Left'), code.replace(/(Left|Right)$/, 'Right')];
  // 方向键作为默认 WASD 的快捷替代
  const arrows: Partial<Record<GameAction, string>> = { forward: 'ArrowUp', back: 'ArrowDown', left: 'ArrowLeft', right: 'ArrowRight' };
  const arrow = arrows[action];
  if (arrow && code === defaultSettings().keyBindings[action] && !Object.values(bindings).includes(arrow)) return [code, arrow];
  return [code];
}
/** 按键显示名 */
export function keyLabel(code: string): string {
  const key = `key.${code}`;
  const label = t(key);
  return label === key ? code.replace(/^Key|^Digit/, '') : label;
}
