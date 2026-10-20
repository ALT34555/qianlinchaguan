import type { StorageLike } from './StorageLike';
export type { StorageLike } from './StorageLike';
import { defaultMysterySettings } from './MysterySettings.ts';
import { playerProfile, type PlayerProfile } from '../entities/PlayerProfile';
import { t } from '../i18n';
import { DEFAULT_RENDER_DISTANCE } from './config';
import { MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE } from './RenderDistance';

export const TEXT_SIZE_BASIS = 1.3;

export const ACTIONS = ['forward', 'back', 'left', 'right', 'jump', 'sprint', 'crouch', 'debug', 'atlas'] as const;
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

export type JoystickStyle = 'jxqy' | 'bagua' | 'luopan' | 'lotus' | 'minimal' | 'cross' | 'octagon' | 'classic';
export const GRAPHICS_PRESETS = ['extreme', 'high', 'balanced', 'smooth', 'low', 'minimal'] as const;
export type GraphicsPreset = typeof GRAPHICS_PRESETS[number];
export const DENSE_VEGETATION_MULTIPLIERS = [1, 1.2, 2, 4, 8, 16] as const;
export function denseVegetationLevel(preset: GraphicsPreset): number {
  return preset === 'extreme' ? 5 : preset === 'high' ? 3 : preset === 'balanced' ? 1 : 0;
}
export const TOUCH_ITEMS = ['move', 'sprint', 'jump', 'crouch'] as const;
export type TouchItem = typeof TOUCH_ITEMS[number];
export interface TouchPoint { x: number; y: number; scale?: number }

function defaultTouchLayout(): Record<TouchItem, TouchPoint> {
  const width = Math.max(214, typeof innerWidth === 'number' ? innerWidth : 1280);
  const height = Math.max(320, typeof innerHeight === 'number' ? innerHeight : 800);
  const unit = Math.min(1, width / 400, height / 420);
  const point = (right: number, bottom: number): TouchPoint => ({ x: (1 - right * unit / (width - 62)) * 100, y: (1 - bottom * unit / (height - 62)) * 100 });
  return { move: { x: 4, y: 74 }, sprint: point(150, 140), jump: point(40, 238), crouch: point(130, 36) };
}

export interface GameSettings {
  graphicsPreset?: GraphicsPreset;
  antialias?: boolean;
  ren_ming?: string;
  profile?: PlayerProfile;
  font?: string;
  applyPaletteToInterface?: boolean;
  interfacePalette?: { mainColor: string; trimColor: string };
  safeX?: number;
  safeY?: number;
  uiScale?: number;
  textScale?: number;
  textScaleBasis?: number;
  vegetationDistance?: number;
  denseVegetation?: number;
  resolution?: string;
  touchOpacity?: number;
  crosshair?: 'none' | 'dot' | 'cross' | 'aim' | 'ring';
  touchLayout?: { move: TouchPoint; actions?: TouchPoint; sprint?: TouchPoint; jump?: TouchPoint; crouch?: TouchPoint };
  joystickStyle?: JoystickStyle;
  fov: number;
  renderDistance: number;
  frameRate?: number;
  timeSpeed: number;
  viewMode: '2d' | '3d';
  touchControls?: boolean;
  particles?: boolean;
  advancedVisuals?: boolean;
  waterReflections?: boolean;
  mirrorReflections?: boolean;
  weatherAtmosphere?: boolean;
  naturalPhysics?: boolean;
  decorativeClouds?: boolean;
  statusFeedback?: boolean;
  showTopBar?: boolean;
  menuAutoPause?: boolean;
  keyBindings: Record<GameAction, string>;
}
export const SETTINGS_KEY = 'qianlin.settings.v1';
export function graphicsPreset(preset: NonNullable<GameSettings['graphicsPreset']>): Partial<GameSettings> {
  const visuals = preset === 'balanced' || preset === 'high' || preset === 'extreme';
  const display: Record<GraphicsPreset, Partial<GameSettings>> = {
    extreme: { resolution: '2560x1440', renderDistance: 10, vegetationDistance: 8, frameRate: 120 },
    high: { resolution: '1920x1080', renderDistance: 8, vegetationDistance: 5, frameRate: 60 },
    balanced: { resolution: 'auto', renderDistance: DEFAULT_RENDER_DISTANCE, vegetationDistance: 3, frameRate: 60 },
    smooth: { resolution: '960x540', renderDistance: 3, vegetationDistance: 2, frameRate: 60 },
    low: { resolution: '1280x720', renderDistance: 3, vegetationDistance: 2, frameRate: 30 },
    minimal: { resolution: '320x240', renderDistance: 2, vegetationDistance: 2, frameRate: 24 },
  };
  return { ...display[preset], graphicsPreset: preset, denseVegetation: denseVegetationLevel(preset), advancedVisuals: visuals, antialias: visuals,
    waterReflections: visuals, weatherAtmosphere: visuals, naturalPhysics: visuals, decorativeClouds: visuals,
    mirrorReflections: false, particles: visuals, statusFeedback: preset !== 'minimal' };
}
export function defaultSettings(): GameSettings {
  const mobile=typeof navigator!=='undefined'&&(/Android|iPhone|iPad/i.test(navigator.userAgent));
  const memory = typeof navigator === 'undefined' ? undefined : (navigator as Navigator & {deviceMemory?:number}).deviceMemory;
  const weak = mobile || (memory !== undefined && memory <= 4) || (typeof navigator !== 'undefined' && navigator.hardwareConcurrency > 0 && navigator.hardwareConcurrency <= 2);
  return { ...graphicsPreset(weak ? 'low' : 'balanced'), ren_ming: '赤', profile: playerProfile(), applyPaletteToInterface: true, interfacePalette: { mainColor: '#CB3A56', trimColor: '#5D3F51' }, font: 'system', safeX: 0, safeY: 0, uiScale: 1, textScale: 1, textScaleBasis: TEXT_SIZE_BASIS, vegetationDistance: weak?2:3, resolution: weak?'1280x720':'auto', touchOpacity: 1, crosshair: 'dot', touchLayout: defaultTouchLayout(), joystickStyle: 'jxqy', fov: 75, renderDistance: weak?3:DEFAULT_RENDER_DISTANCE, frameRate: weak?30:60, timeSpeed: 72, viewMode: '2d', touchControls: typeof navigator !== 'undefined' && navigator.maxTouchPoints > 0, particles: !weak, advancedVisuals: !weak, showTopBar: true, menuAutoPause: false, keyBindings: {
    forward: 'KeyW', back: 'KeyS', left: 'KeyA', right: 'KeyD', jump: 'Space',
    sprint: 'ShiftLeft', crouch: 'ControlLeft', debug: 'F12', atlas: 'KeyM',
  } };
}
export function validBinding(code: unknown): code is string {
  return typeof code === 'string' && /^(Key[A-Z]|Digit[0-9]|Arrow(Up|Down|Left|Right)|Space|Shift(Left|Right)|Control(Left|Right)|F([1-9]|1[0-2])|Enter|Backspace|Tab|Backquote|Minus|Equal|BracketLeft|BracketRight|Backslash|Semicolon|Quote|Comma|Period|Slash)$/.test(code);
}
function canonicalKey(code: string): string { return code.replace(/(Shift|Control)(Left|Right)/, '$1'); }
export function validateSettings(value: GameSettings): GameSettings {
  const oldBindings = value.keyBindings as GameSettings['keyBindings'] & { descend?: string };
  const bindings={...defaultSettings().keyBindings,...value.keyBindings, crouch: value.keyBindings?.crouch ?? oldBindings?.descend ?? 'ControlLeft'};
  delete (bindings as Record<string, string>).descend;
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
  if (!(GRAPHICS_PRESETS as readonly string[]).includes(preset)) throw new Error(t('settings.invalidChoice'));
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
  const denseVegetation = range(value.denseVegetation, denseVegetationLevel(preset), 0, 5);
  if (!Number.isInteger(denseVegetation)) throw new Error(t('settings.invalidChoice'));
  const rawFrameRate = value.frameRate as number | string | undefined | null;
  const frameRate = rawFrameRate === undefined || rawFrameRate === null || rawFrameRate === '' ? UNLIMITED_FRAME_RATE : Number(rawFrameRate);
  if (!Number.isInteger(frameRate) || (frameRate !== UNLIMITED_FRAME_RATE && (frameRate < MIN_FRAME_RATE || frameRate > MAX_FRAME_RATE))) throw new Error(t('error.frameRate'));
  const font = value.font ?? 'system', crosshair = value.crosshair ?? 'dot', resolution = value.resolution ?? 'auto';
  if (!(typeof font === 'string' && /^(system|song|round|mod:[^\x00-\x1f]{1,200})$/.test(font)) || !['none', 'dot', 'cross', 'aim', 'ring'].includes(crosshair)
    || !['auto', '320x240', '640x480', '960x540', '1280x720', '1920x1080', '2560x1440'].includes(resolution)) throw new Error(t('settings.invalidChoice'));
  const layout = value.touchLayout ?? defaults.touchLayout!;
  const touchLayout = Object.fromEntries(TOUCH_ITEMS.map((key, index) => {
    const width = Math.max(214, (typeof innerWidth === 'number' ? innerWidth : 1280) * (1 - 2 * (value.safeX ?? 0) / 100) / (value.uiScale ?? 1));
    const legacyScale = Math.min(layout.actions?.scale ?? 1, width / 214);
    const point = layout[key] ?? (key !== 'move' && layout.actions ? { ...layout.actions, x: Math.max(0, Math.min(100, ((width - 214 * legacyScale) * layout.actions.x / 100 + (index - 1) * 76 * legacyScale) / (width - 62 * legacyScale) * 100)) } : undefined);
    const fallback = defaults.touchLayout![key]!;
    return [key, { x: range(point?.x, fallback.x, 0, 100), y: range(point?.y, fallback.y, 0, 100), scale: range(point?.scale, 1, .5, 2) }];
  })) as NonNullable<GameSettings['touchLayout']>;
  const joystickStyle = (value.joystickStyle ?? defaults.joystickStyle ?? 'jxqy') as JoystickStyle;
  if (!['jxqy', 'bagua', 'luopan', 'lotus', 'minimal', 'cross', 'octagon', 'classic'].includes(joystickStyle)) throw new Error(t('settings.invalidChoice'));
  const profile = playerProfile({ ...value.profile, name: value.ren_ming ?? value.profile?.name });
  const interfacePalette = playerProfile({ ...profile, ...(value.interfacePalette ?? (value.applyPaletteToInterface === false ? { mainColor: '#CB3A56', trimColor: '#5D3F51' } : profile)) });
  const legacyVisuals = typeof value.advancedVisuals === 'boolean' ? value.advancedVisuals : defaults.advancedVisuals;
  return { interfacePalette: { mainColor: interfacePalette.mainColor, trimColor: interfacePalette.trimColor }, applyPaletteToInterface: value.applyPaletteToInterface !== false, graphicsPreset: preset, antialias: typeof value.antialias === 'boolean' ? value.antialias : defaults.antialias, ren_ming: profile.name, profile, font, crosshair, resolution, vegetationDistance, denseVegetation, touchLayout, joystickStyle,
    waterReflections: typeof value.waterReflections === 'boolean' ? value.waterReflections : legacyVisuals,
    weatherAtmosphere: typeof value.weatherAtmosphere === 'boolean' ? value.weatherAtmosphere : legacyVisuals,
    naturalPhysics: typeof value.naturalPhysics === 'boolean' ? value.naturalPhysics : legacyVisuals,
    decorativeClouds: typeof value.decorativeClouds === 'boolean' ? value.decorativeClouds : legacyVisuals,
    mirrorReflections: false, statusFeedback: typeof value.statusFeedback === 'boolean' ? value.statusFeedback : true,
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
