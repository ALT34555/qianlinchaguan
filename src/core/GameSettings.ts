import type { StorageLike } from './StorageLike';
export type { StorageLike } from './StorageLike';
import { defaultMysterySettings } from './MysterySettings.ts';
import { renName } from '../entities/PlayerIdentity';
import { t } from '../i18n';
import { DEFAULT_RENDER_DISTANCE } from './config';
import { MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE } from './RenderDistance';

export const TEXT_SIZE_BASIS = 1.3;

export const ACTIONS = ['forward', 'back', 'left', 'right', 'jump', 'sprint', 'descend', 'debug', 'atlas'] as const;
export type GameAction = typeof ACTIONS[number];

export const FRAME_RATE_STOPS = [24, 30, 60, 90, 100, 120, 180, 240, 300] as const;
export const MIN_FRAME_RATE = 24;
export const MAX_FRAME_RATE = 300;
export const UNLIMITED_FRAME_RATE = 0;

export function frameRateStops(deviceMax: number): number[] {
  const usable = FRAME_RATE_STOPS.filter(value => value <= deviceMax);
  return [...(usable.length ? usable : [MIN_FRAME_RATE]), UNLIMITED_FRAME_RATE];
}

/** 操作名称 */
export function actionLabel(action: GameAction): string {
  return t(`action.${action}`);
}

export interface GameSettings {
  graphicsPreset?: 'balanced' | 'low' | 'minimal';
  antialias?: boolean;
  ren_ming?: string;
  font?: string;
  safeX?: number;
  safeY?: number;
  uiScale?: number;
  textScale?: number;
  textScaleBasis?: number;
  vegetationDistance?: number;
  resolution?: string;
  touchOpacity?: number;
  crosshair?: 'none' | 'dot' | 'cross' | 'aim' | 'ring';
  touchLayout?: Record<'move' | 'actions', { x: number; y: number }>;
  fov: number;
  renderDistance: number;
  frameRate?: number;
  timeSpeed: number;
  viewMode: '2d' | '3d';
  touchControls?: boolean;
  particles?: boolean;
  advancedVisuals?: boolean;
  showTopBar?: boolean;
  menuAutoPause?: boolean;
  keyBindings: Record<GameAction, string>;
}
export const SETTINGS_KEY = 'qianlin.settings.v1';
export function graphicsPreset(preset: NonNullable<GameSettings['graphicsPreset']>): Partial<GameSettings> {
  return preset === 'balanced'
    ? { graphicsPreset: preset, resolution: 'auto', renderDistance: DEFAULT_RENDER_DISTANCE, vegetationDistance: 3, frameRate: 60, advancedVisuals: true, antialias: true }
    : { graphicsPreset: preset, resolution: preset === 'minimal' ? '960x540' : '1280x720', renderDistance: preset === 'minimal' ? 2 : 3, vegetationDistance: 2, frameRate: 30, advancedVisuals: false, antialias: false };
}
export function defaultSettings(): GameSettings {
  const mobile=typeof navigator!=='undefined'&&(/Android|iPhone|iPad/i.test(navigator.userAgent));
  const memory = typeof navigator === 'undefined' ? undefined : (navigator as Navigator & {deviceMemory?:number}).deviceMemory;
  const weak = mobile || (memory !== undefined && memory <= 4) || (typeof navigator !== 'undefined' && navigator.hardwareConcurrency > 0 && navigator.hardwareConcurrency <= 2);
  return { ...graphicsPreset(weak ? 'low' : 'balanced'), ren_ming: '赤', font: 'system', safeX: 0, safeY: 0, uiScale: 1, textScale: 1, textScaleBasis: TEXT_SIZE_BASIS, vegetationDistance: weak?2:3, resolution: weak?'1280x720':'auto', touchOpacity: 1, crosshair: 'dot', touchLayout: { move: { x: 4, y: 74 }, actions: { x: 80, y: 78 } }, fov: 75, renderDistance: weak?3:DEFAULT_RENDER_DISTANCE, frameRate: weak?30:60, timeSpeed: 72, viewMode: '2d', touchControls: typeof navigator !== 'undefined' && navigator.maxTouchPoints > 0, particles: true, advancedVisuals: !weak, showTopBar: true, menuAutoPause: false, keyBindings: {
    forward: 'KeyW', back: 'KeyS', left: 'KeyA', right: 'KeyD', jump: 'Space',
    sprint: 'ShiftLeft', descend: 'ControlLeft', debug: 'F12', atlas: 'KeyM',
  } };
}
export function validBinding(code: unknown): code is string {
  return typeof code === 'string' && /^(Key[A-Z]|Digit[0-9]|Arrow(Up|Down|Left|Right)|Space|Shift(Left|Right)|Control(Left|Right)|F([1-9]|1[0-2])|Enter|Backspace|Tab|Backquote|Minus|Equal|BracketLeft|BracketRight|Backslash|Semicolon|Quote|Comma|Period|Slash)$/.test(code);
}
function canonicalKey(code: string): string { return code.replace(/(Shift|Control)(Left|Right)/, '$1'); }
export function validateSettings(value: GameSettings): GameSettings {
  const bindings={...defaultSettings().keyBindings,...value.keyBindings};
  const renderDistance = value.renderDistance === undefined ? DEFAULT_RENDER_DISTANCE : value.renderDistance;
  if (!Number.isInteger(renderDistance) || renderDistance < MIN_RENDER_DISTANCE || renderDistance > MAX_RENDER_DISTANCE) throw new Error(t('error.renderDistance'));
  if (!Number.isFinite(value.fov) || value.fov < 30 || value.fov > 120) throw new Error(t('error.fov'));
  if (!Number.isFinite(value.timeSpeed) || value.timeSpeed < 0 || value.timeSpeed > 86400) throw new Error(t('error.timeSpeed'));
  if (!['2d', '3d'].includes(value.viewMode)) throw new Error(t('error.viewMode'));
  const used = new Set<string>();
  for (const action of ACTIONS) {
    const code = bindings[action];
    if (!validBinding(code)) throw new Error(t('error.invalidBinding', { action: actionLabel(action) }));
    const key = canonicalKey(code);
    if (used.has(key)) throw new Error(t('error.duplicateBinding'));
    used.add(key);
  }
  const defaults = defaultSettings();
  const preset = value.graphicsPreset ?? defaults.graphicsPreset!;
  if (!['balanced', 'low', 'minimal'].includes(preset)) throw new Error(t('settings.invalidChoice'));
  const range = (n: number | undefined, fallback: number, min: number, max: number) => {
    const result = n ?? fallback;
    if (!Number.isFinite(result) || result < min || result > max) throw new Error(t('settings.invalidRange', { min, max }));
    return result;
  };
  const requestedTextScale = range(value.textScale, 1, .25, 4);
  const textScale = value.textScaleBasis === undefined && value.textScale !== undefined
    ? Math.max(.25, requestedTextScale / TEXT_SIZE_BASIS) : requestedTextScale;
  const requestedVegetation = range(value.vegetationDistance, renderDistance, 2, 16);
  if (!Number.isInteger(requestedVegetation)) throw new Error(t('error.renderDistance'));
  const vegetationDistance = Math.min(requestedVegetation, renderDistance);
  const rawFrameRate = value.frameRate as number | string | undefined | null;
  const frameRate = rawFrameRate === undefined || rawFrameRate === null || rawFrameRate === '' ? UNLIMITED_FRAME_RATE : Number(rawFrameRate);
  if (!Number.isInteger(frameRate) || (frameRate !== UNLIMITED_FRAME_RATE && (frameRate < MIN_FRAME_RATE || frameRate > MAX_FRAME_RATE))) throw new Error(t('error.frameRate'));
  const font = value.font ?? 'system', crosshair = value.crosshair ?? 'dot', resolution = value.resolution ?? 'auto';
  if (!(typeof font === 'string' && /^(system|song|round|mod:[^\x00-\x1f]{1,200})$/.test(font)) || !['none', 'dot', 'cross', 'aim', 'ring'].includes(crosshair)
    || !['auto', '320x240', '640x480', '960x540', '1280x720', '1920x1080', '2560x1440'].includes(resolution)) throw new Error(t('settings.invalidChoice'));
  const layout = value.touchLayout ?? defaults.touchLayout!;
  const touchLayout = Object.fromEntries(['move', 'actions'].map(key => {
    const point = layout[key as keyof typeof layout];
    return [key, { x: range(point?.x, defaults.touchLayout![key as keyof typeof layout].x, 0, 100), y: range(point?.y, defaults.touchLayout![key as keyof typeof layout].y, 0, 100) }];
  })) as NonNullable<GameSettings['touchLayout']>;
  return { graphicsPreset: preset, antialias: typeof value.antialias === 'boolean' ? value.antialias : defaults.antialias, ren_ming: renName(value.ren_ming), font, crosshair, resolution, vegetationDistance, touchLayout,
    advancedVisuals: typeof value.advancedVisuals === 'boolean' ? value.advancedVisuals : defaults.advancedVisuals,
    safeX: range(value.safeX, 0, 0, 35), safeY: range(value.safeY, 0, 0, 35), uiScale: range(value.uiScale, 1, .25, 4), textScale, textScaleBasis: TEXT_SIZE_BASIS, touchOpacity: range(value.touchOpacity, 1, .1, 1),
    fov: value.fov, renderDistance, frameRate, timeSpeed: value.timeSpeed, viewMode: value.viewMode, touchControls: typeof value.touchControls === 'boolean' ? value.touchControls : defaults.touchControls, showTopBar: typeof value.showTopBar === 'boolean' ? value.showTopBar : true, particles: typeof value.particles === 'boolean' ? value.particles : true, menuAutoPause: typeof value.menuAutoPause === 'boolean' ? value.menuAutoPause : false, keyBindings: bindings };
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
  // 空闲方向键也可移动
  const arrows: Partial<Record<GameAction, string>> = { forward: 'ArrowUp', back: 'ArrowDown', left: 'ArrowLeft', right: 'ArrowRight' };
  const arrow = arrows[action];
  if (arrow && code !== arrow && !Object.values(bindings).includes(arrow)) return [code, arrow];
  return [code];
}
/** 按键显示名 */
export function keyLabel(code: string): string {
  const key = `key.${code}`;
  const label = t(key);
  return label === key ? code.replace(/^Key|^Digit/, '') : label;
}
