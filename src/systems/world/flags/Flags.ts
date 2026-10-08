/** 旗帜注册表 */

import emblemsJson from '../../../../content/data/world/flags/emblems.json';
import flagsJson from '../../../../content/data/world/flags/flags.json';
import palettesJson from '../../../../content/data/world/flags/palettes.json';
import polesJson from '../../../../content/data/world/flags/poles.json';
import { hashString } from '../../../core/math/Random';
import { assertNoTableVersion } from '../../../core/version';
import {
  FLAG_GLOBAL_DEFAULTS, FLAG_MOUNT_TABLE, FLAG_SHAPE_TABLE, FLAG_WAVE_DEFAULTS,
  FLAGPOLE_GLOBAL_DEFAULTS, FLAGPOLE_KIND_DEFAULTS, buildFlagCloth, buildFlagpole,
  chargePainterOf, resolveFlagMount, type FlagShapeDef,
} from './archetypes';
import { FlagBuilder, scaleGeometry } from './geometry';
import type { FlagGeometry } from './geometry';
import {
  FLAG_COLOR_SLOTS, FLAG_MOUNTS, FLAG_SHAPES, FLAGPOLE_BASE_STYLES, FLAGPOLE_COLLAR_STYLES,
  FLAGPOLE_FINIAL_STYLES, FLAGPOLE_KINDS, FLAGPOLE_PLATE_STYLES,
  type FlagAssemblyOptions, type FlagAttachment, type FlagBuildOptions, type FlagClothDef, type FlagDef,
  type FlagEmblemDef, type FlagEmblemFile, type FlagFile, type FlagMount, type FlagMountSolution, type FlagOrientation,
  type FlagPalette, type FlagPaletteEntry, type FlagPaletteFile, type FlagParams, type FlagShape,
  type FlagpoleBuildDef, type FlagpoleBuildOptions, type FlagpoleDef, type FlagpoleFile, type FlagpoleKind,
  type FlagpoleParams, type ResolvedFlagCharge, type ResolvedFlagPalette, type ResolvedFlagpoleParams,
  type V3,
} from './types';

const palettes = palettesJson as FlagPaletteFile;

/** 旗帜与构件色板表 */
export const FLAG_PALETTES: ReadonlyMap<string, FlagPaletteEntry> = new Map<string, FlagPaletteEntry>([
  ...Object.entries(palettes.cloth ?? {}),
  ...Object.entries(palettes.hardware ?? {}),
]);

const HEX = /^#[0-9a-fA-F]{6}$/;

export function resolveFlagColor(value: string): string {
  if (/^#[0-9a-fA-F]{3}$/.test(value)) return '#' + [...value.slice(1)].map(c => c + c).join('');
  if (HEX.test(value)) return value;
  if (value.startsWith('#')) throw new Error(`[Flags] 颜色须为 #RGB 或 #RRGGBB: ${value}`);
  const color = paletteEntry(value).default;
  if (!HEX.test(color)) throw new Error(`[Flags] 色板键 ${value} 的色值非法: ${color}`);
  return color;
}

function paletteEntry(key: string): FlagPaletteEntry {
  const e = FLAG_PALETTES.get(key);
  if (!e) {
    throw new Error(`[Flags] 未知色板键 "${key}"，可用键: ${[...FLAG_PALETTES.keys()].sort().join(', ')}`);
  }
  return e;
}

export const FLAG_SHAPE_PALETTE: Readonly<Record<FlagShape, FlagPalette>> = Object.freeze({
  rect: { field: 'vermilion', trim: 'gold', charge: 'ivory', emblem: 'gold' },
  square: { field: 'azure', trim: 'ivory', charge: 'ivory', emblem: 'gold' },
  swallowtail: { field: 'gold', trim: 'crimson', charge: 'crimson', emblem: 'crimson' },
  pennant: { field: 'crimson', trim: 'ivory', charge: 'ivory', emblem: 'gold' },
  notched: { field: 'indigo', trim: 'silver', charge: 'ivory', emblem: 'silver' },
  roundFly: { field: 'jade', trim: 'gold', charge: 'ivory', emblem: 'gold' },
  gonfalon: { field: 'purple', trim: 'gold', charge: 'ivory', emblem: 'gold' },
});

export const FLAGPOLE_KIND_PALETTE: Readonly<Record<FlagpoleKind, FlagPalette>> = Object.freeze({
  ground: { pole: 'oakWood', metal: 'bronze', stone: 'granite', cord: 'hemp' },
  horizontal: { pole: 'iron', metal: 'iron', stone: 'granite', cord: 'hemp' },
  stick: { pole: 'bamboo', metal: 'brass', stone: 'granite', cord: 'hemp' },
});

export function resolveFlagPalette(base: FlagPalette, unit?: FlagPalette, override?: FlagPalette): ResolvedFlagPalette {
  const merged: FlagPalette = { ...base, ...(unit ?? {}), ...(override ?? {}) };
  const out: ResolvedFlagPalette = {};
  for (const slot of FLAG_COLOR_SLOTS) {
    const key = merged[slot];
    if (!key) continue;
    out[slot] = resolveFlagColor(key);
  }
  return out;
}

export interface FlagUnitDef extends FlagDef {
  source: string;
}

export interface FlagpoleUnitDef extends FlagpoleDef {
  source: string;
}

const flagFiles: readonly { file: string; data: FlagFile }[] = [
  { file: 'flags.json', data: flagsJson as FlagFile },
];

const poleFiles: readonly { file: string; data: FlagpoleFile }[] = [
  { file: 'poles.json', data: polesJson as FlagpoleFile },
];

function checkPaletteKeys(id: string, palette: FlagPalette | undefined): void {
  for (const [slot, key] of Object.entries(palette ?? {})) {
    if (!(FLAG_COLOR_SLOTS as readonly string[]).includes(slot)) {
      throw new Error(`[Flags] ${id} 的 palette 槽非法: ${slot}（可用: ${FLAG_COLOR_SLOTS.join(' / ')}）`);
    }
    resolveFlagColor(key as string);
  }
}

const flagUnits: FlagUnitDef[] = [];
const flagById = new Map<string, FlagUnitDef>();

for (const { file, data } of flagFiles) {
  assertNoTableVersion(file, data);
  if (!Array.isArray(data.flags) || data.flags.length === 0) throw new Error(`[Flags] ${file} 缺少 flags 数组`);
  for (const raw of data.flags) {
    if (!raw.id) throw new Error(`[Flags] ${file} 里有条目缺少 id`);
    if (flagById.has(raw.id)) throw new Error(`[Flags] 旗帜 id 重复: ${raw.id}`);
    if (!FLAG_SHAPES.includes(raw.shape)) {
      throw new Error(`[Flags] 旗帜 ${raw.id} 的旗形非法: ${raw.shape}（可用: ${FLAG_SHAPES.join(', ')}）`);
    }
    if (raw.params?.shape !== undefined && raw.params.shape !== raw.shape) {
      throw new Error(`[Flags] 旗帜 ${raw.id} 的 shape 与 params.shape 不一致`);
    }
    checkPaletteKeys(raw.id, raw.palette);
    const charge = raw.params?.charge?.kind;
    if (charge !== undefined) chargePainterOf(charge);
    if (raw.params?.charge?.slot !== undefined && !(FLAG_COLOR_SLOTS as readonly string[]).includes(raw.params.charge.slot)) {
      throw new Error(`[Flags] 旗帜 ${raw.id} 的图案色槽非法: ${raw.params.charge.slot}（可用: ${FLAG_COLOR_SLOTS.join(' / ')}）`);
    }
    const unit: FlagUnitDef = { ...raw, source: file };
    flagUnits.push(unit);
    flagById.set(unit.id, unit);
  }
}

const poleUnits: FlagpoleUnitDef[] = [];
const poleById = new Map<string, FlagpoleUnitDef>();

for (const { file, data } of poleFiles) {
  assertNoTableVersion(file, data);
  if (!Array.isArray(data.poles) || data.poles.length === 0) throw new Error(`[Flags] ${file} 缺少 poles 数组`);
  for (const raw of data.poles) {
    if (!raw.id) throw new Error(`[Flags] ${file} 里有条目缺少 id`);
    if (poleById.has(raw.id)) throw new Error(`[Flags] 旗杆 id 重复: ${raw.id}`);
    if (!FLAGPOLE_KINDS.includes(raw.kind)) {
      throw new Error(`[Flags] 旗杆 ${raw.id} 的杆型非法: ${raw.kind}（可用: ${FLAGPOLE_KINDS.join(', ')}）`);
    }
    checkPaletteKeys(raw.id, raw.palette);
    const base = raw.params?.base?.style;
    if (base !== undefined && !FLAGPOLE_BASE_STYLES.includes(base)) {
      throw new Error(`[Flags] 旗杆 ${raw.id} 的底座样式非法: ${base}（可用: ${FLAGPOLE_BASE_STYLES.join(', ')}）`);
    }
    const finial = raw.params?.finial?.style;
    if (finial !== undefined && !FLAGPOLE_FINIAL_STYLES.includes(finial)) {
      throw new Error(`[Flags] 旗杆 ${raw.id} 的顶饰样式非法: ${finial}（可用: ${FLAGPOLE_FINIAL_STYLES.join(', ')}）`);
    }
    const collar = raw.params?.collar?.style;
    if (collar !== undefined && !FLAGPOLE_COLLAR_STYLES.includes(collar)) {
      throw new Error(`[Flags] 旗杆 ${raw.id} 的套环样式非法: ${collar}（可用: ${FLAGPOLE_COLLAR_STYLES.join(', ')}）`);
    }
    const plate = raw.params?.plate?.style;
    if (plate !== undefined && !FLAGPOLE_PLATE_STYLES.includes(plate)) {
      throw new Error(`[Flags] 旗杆 ${raw.id} 的墙面座样式非法: ${plate}（可用: ${FLAGPOLE_PLATE_STYLES.join(', ')}）`);
    }
    if (raw.kind === 'horizontal' && raw.params?.arm === null) {
      throw new Error(`[Flags] 横向旗杆 ${raw.id} 不能去掉 arm：横杆本身就是杆身`);
    }
    const unit: FlagpoleUnitDef = { ...raw, source: file };
    poleUnits.push(unit);
    poleById.set(unit.id, unit);
  }
}

export const Flag = Object.freeze(
  Object.fromEntries(flagUnits.map((u) => [u.id, u.id])) as Record<string, string>,
);

export const Flagpole = Object.freeze(
  Object.fromEntries(poleUnits.map((u) => [u.id, u.id])) as Record<string, string>,
);

export const FLAG_DEFS: readonly FlagUnitDef[] = flagUnits;
export const FLAG_IDS: readonly string[] = flagUnits.map((u) => u.id);
export const FLAGPOLE_DEFS: readonly FlagpoleUnitDef[] = poleUnits;
export const FLAGPOLE_IDS: readonly string[] = poleUnits.map((u) => u.id);

export function getFlagDef(id: string): FlagUnitDef | undefined {
  return flagById.get(id);
}

export function getFlagpoleDef(id: string): FlagpoleUnitDef | undefined {
  return poleById.get(id);
}

export function flagsByTag(tag: string): readonly FlagUnitDef[] {
  return flagUnits.filter((u) => (u.tags ?? []).includes(tag));
}

export function polesByTag(tag: string): readonly FlagpoleUnitDef[] {
  return poleUnits.filter((u) => (u.tags ?? []).includes(tag));
}

export function flagsByShape(shape: FlagShape): readonly FlagUnitDef[] {
  return flagUnits.filter((u) => u.shape === shape);
}

export function polesByKind(kind: FlagpoleKind): readonly FlagpoleUnitDef[] {
  return poleUnits.filter((u) => u.kind === kind);
}

const emblemData = emblemsJson as FlagEmblemFile;

assertNoTableVersion('emblems.json', emblemData);

export const FLAG_EMBLEMS: readonly FlagEmblemDef[] = Object.freeze(
  Array.isArray(emblemData.emblems) ? [...emblemData.emblems] : [],
);

const emblemById = new Map<string, FlagEmblemDef>();

for (const emblem of FLAG_EMBLEMS) {
  if (!emblem.id) throw new Error('[Flags] emblems.json 里有条目缺少 id');
  if (emblemById.has(emblem.id)) throw new Error(`[Flags] 徽标 id 重复: ${emblem.id}`);
  checkPaletteKeys(emblem.id, emblem.palette);
  if (emblem.charge?.kind !== undefined) chargePainterOf(emblem.charge.kind);
  emblemById.set(emblem.id, emblem);
}

export function getEmblemDef(id: string): FlagEmblemDef | undefined {
  return emblemById.get(id);
}

export function emblemsByTag(tag: string): readonly FlagEmblemDef[] {
  return FLAG_EMBLEMS.filter((e) => (e.tags ?? []).includes(tag));
}

export function applyEmblem<T extends FlagBuildOptions>(options: T): T {
  if (!options.emblem) return options;
  const emblem = emblemById.get(options.emblem);
  if (!emblem) {
    throw new Error(`[Flags] 未知阵营徽标: ${options.emblem}（已注册 ${FLAG_EMBLEMS.length} 个，可在 content/data/world/flags/emblems.json 追加）`);
  }
  return {
    ...options,
    charge: { ...(emblem.charge ?? {}), ...(options.charge ?? {}) },
    palette: { ...(emblem.palette ?? {}), ...(options.palette ?? {}) },
  } as T;
}

function mergeUnitSize(
  shapeDef: FlagShapeDef,
  unit: FlagParams | undefined,
  options: FlagBuildOptions,
): { fly: number; hoist: number; trimWidth: number; sleeve: boolean } {
  const p = unit ?? {};
  const hoist = options.hoist ?? p.hoist ?? shapeDef.hoist;
  const ratio = options.ratio ?? p.ratio ?? shapeDef.ratio;
  const fly = options.fly ?? p.fly ?? hoist * ratio;
  if (!(fly > 0) || !(hoist > 0)) throw new Error(`[Flags] 旗帜尺寸必须为正数，收到 fly=${fly} hoist=${hoist}`);
  return {
    fly,
    hoist,
    trimWidth: Math.max(0, options.trimWidth ?? p.trimWidth ?? shapeDef.trimWidth),
    sleeve: options.sleeve ?? p.sleeve ?? FLAG_GLOBAL_DEFAULTS.sleeve,
  };
}

function resolvedChargeDefaults(): ResolvedFlagCharge {
  return {
    kind: 'none',
    size: 0.46,
    offsetX: 0,
    offsetY: 0,
    rotation: 0,
    lift: 0.012,
    slot: 'charge',
  };
}

export function resolveFlagClothDef(unit: FlagUnitDef, options: FlagBuildOptions = {}): FlagClothDef {
  const shapeDef = FLAG_SHAPE_TABLE[unit.shape];
  if (!shapeDef) throw new Error(`[Flags] 旗帜 ${unit.id} 的旗形 "${unit.shape}" 未登记原型`);
  const size = mergeUnitSize(shapeDef, unit.params, options);
  const wave = { ...FLAG_WAVE_DEFAULTS, ...(unit.params?.wave ?? {}), ...(options.wave ?? {}) };
  const charge: ResolvedFlagCharge = {
    ...resolvedChargeDefaults(),
    ...(unit.params?.charge ?? {}),
    ...(options.charge ?? {}),
  };
  chargePainterOf(charge.kind);
  if (!(FLAG_COLOR_SLOTS as readonly string[]).includes(charge.slot)) {
    throw new Error(`[Flags] 图案色槽非法: ${charge.slot}（可用: ${FLAG_COLOR_SLOTS.join(' / ')}）`);
  }
  return {
    id: unit.id,
    name: unit.name,
    latin: unit.latin,
    shape: unit.shape,
    fly: size.fly,
    hoist: size.hoist,
    trimWidth: size.trimWidth,
    sleeve: size.sleeve,
    wave: {
      amplitude: wave.amplitude,
      waves: wave.waves,
      ripple: wave.ripple,
      phase: wave.phase,
      droop: wave.droop,
      twist: wave.twist,
      columns: Math.max(2, Math.round(wave.columns ?? shapeDef.columns)),
      bands: Math.max(2, Math.round(wave.bands ?? shapeDef.bands)),
      doubleSided: wave.doubleSided,
    },
    charge,
    palette: resolveFlagPalette(FLAG_SHAPE_PALETTE[unit.shape], unit.palette, options.palette),
    wind: options.wind ?? FLAG_GLOBAL_DEFAULTS.wind,
    shapeSeed: options.shapeSeed ?? 0,
    buildSeed: hashString(`${unit.id}:${unit.shape}`),
    tags: unit.tags ?? [],
  };
}

function pickPoleNumber<K extends keyof FlagpoleParams>(
  key: K,
  options: FlagpoleBuildOptions,
  unitParams: FlagpoleParams,
  kindDefaults: FlagpoleParams,
): number {
  const value = (options as FlagpoleParams)[key] ?? unitParams[key] ?? kindDefaults[key] ?? FLAGPOLE_GLOBAL_DEFAULTS[key];
  return Number(value ?? 0);
}

export function resolveFlagpoleParams(unit: FlagpoleUnitDef, options: FlagpoleBuildOptions = {}): ResolvedFlagpoleParams {
  const kindDefaults = FLAGPOLE_KIND_DEFAULTS[unit.kind];
  const unitParams = unit.params ?? {};
  const height = pickPoleNumber('height', options, unitParams, kindDefaults);
  const radius = pickPoleNumber('radius', options, unitParams, kindDefaults);
  if (!(height > 0) || !(radius > 0)) {
    throw new Error(`[Flags] 旗杆 ${unit.id} 的 height/radius 必须为正数，收到 ${height}/${radius}`);
  }
  const base = {
    ...FLAGPOLE_GLOBAL_DEFAULTS.base!,
    ...(kindDefaults.base ?? {}),
    ...(unitParams.base ?? {}),
    ...(options.base ?? {}),
  };
  const finial = {
    ...FLAGPOLE_GLOBAL_DEFAULTS.finial!,
    ...(kindDefaults.finial ?? {}),
    ...(unitParams.finial ?? {}),
    ...(options.finial ?? {}),
  };
  const collar = {
    ...FLAGPOLE_GLOBAL_DEFAULTS.collar!,
    ...(kindDefaults.collar ?? {}),
    ...(unitParams.collar ?? {}),
    ...(options.collar ?? {}),
  };
  const plate = {
    ...FLAGPOLE_GLOBAL_DEFAULTS.plate!,
    ...(kindDefaults.plate ?? {}),
    ...(unitParams.plate ?? {}),
    ...(options.plate ?? {}),
  };
  const armSource = options.arm !== undefined ? options.arm : unitParams.arm !== undefined ? unitParams.arm : kindDefaults.arm;
  let arm: ResolvedFlagpoleParams['arm'] = null;
  if (armSource) {
    const length = armSource.length ?? 1.4;
    if (!(length > 0)) throw new Error(`[Flags] 旗杆 ${unit.id} 的横臂长度必须为正数`);
    const defaultAt = unit.kind === 'horizontal' ? height : unit.kind === 'stick' ? height * 0.985 : height * 0.86;
    arm = {
      length,
      at: Math.min(armSource.at ?? defaultAt, height),
      rise: armSource.rise ?? 0,
      radius: armSource.radius ?? radius * 0.78,
      sides: armSource.sides ?? pickPoleNumber('sides', options, unitParams, kindDefaults),
      tipKnob: armSource.tipKnob ?? 1.7,
      brace: armSource.brace ?? 0,
    };
  }
  return {
    height,
    radius,
    sides: pickPoleNumber('sides', options, unitParams, kindDefaults),
    tipRatio: pickPoleNumber('tipRatio', options, unitParams, kindDefaults),
    segments: pickPoleNumber('segments', options, unitParams, kindDefaults),
    joints: pickPoleNumber('joints', options, unitParams, kindDefaults),
    base: {
      style: base.style ?? 'none',
      radius: base.radius ?? 0,
      height: base.height ?? 0.3,
      sides: base.sides ?? 6,
    },
    finial: { style: finial.style ?? 'none', size: finial.size ?? 1 },
    collar: { style: collar.style ?? 'none', size: collar.size ?? 1 },
    plate: {
      style: plate.style ?? 'none',
      width: plate.width ?? 0.42,
      height: plate.height ?? 0.6,
      depth: plate.depth ?? 0.09,
    },
    arm,
    cord: pickPoleNumber('cord', options, unitParams, kindDefaults),
    tiltX: pickPoleNumber('tiltX', options, unitParams, kindDefaults),
    tiltZ: pickPoleNumber('tiltZ', options, unitParams, kindDefaults),
  };
}

function poleBuildDef(
  unit: FlagpoleUnitDef,
  params: ResolvedFlagpoleParams,
  options: FlagpoleBuildOptions,
  collars: readonly number[],
): FlagpoleBuildDef {
  return {
    id: unit.id,
    name: unit.name,
    latin: unit.latin,
    kind: unit.kind,
    params,
    palette: resolveFlagPalette(FLAGPOLE_KIND_PALETTE[unit.kind], unit.palette, options.palette),
    collars,
    shapeSeed: options.shapeSeed ?? 0,
    buildSeed: hashString(`${unit.id}:${unit.kind}`),
    tags: unit.tags ?? [],
  };
}

export function buildFlagById(id: string, options: FlagBuildOptions = {}): FlagGeometry {
  const opts = applyEmblem(options);
  const unit = flagById.get(id);
  if (!unit) throw new Error(`[Flags] 未知旗帜 id: ${id}（可用: ${FLAG_IDS.join(', ')}）`);
  const def = resolveFlagClothDef(unit, opts);
  const geo = buildFlagCloth(def);
  const scale = opts.scale ?? 1;
  if (scale !== 1) scaleGeometry(geo, scale);
  return geo;
}

export function buildFlagpoleById(id: string, options: FlagpoleBuildOptions & { collars?: readonly number[] } = {}): FlagGeometry {
  const unit = poleById.get(id);
  if (!unit) throw new Error(`[Flags] 未知旗杆 id: ${id}（可用: ${FLAGPOLE_IDS.join(', ')}）`);
  const params = resolveFlagpoleParams(unit, options);
  const geo = buildFlagpole(poleBuildDef(unit, params, options, options.collars ?? []));
  const scale = options.scale ?? 1;
  if (scale !== 1) scaleGeometry(geo, scale);
  return geo;
}

export function mountsOf(kind: FlagpoleKind, shape: FlagShape, params?: ResolvedFlagpoleParams): readonly FlagMount[] {
  const shapeDef = FLAG_SHAPE_TABLE[shape];
  if (!shapeDef) return [];
  return FLAG_MOUNTS.filter((m) => {
    const def = FLAG_MOUNT_TABLE[m];
    if (!def.kinds.includes(kind)) return false;
    if (!shapeDef.orientations.includes(def.orientation)) return false;
    if (params && def.needsArm && !params.arm) return false;
    if (params && def.forbidsArm && params.arm) return false;
    return true;
  });
}

export function defaultMountOf(unit: FlagpoleUnitDef, params: ResolvedFlagpoleParams, shape: FlagShape): FlagMount {
  const shapeDef = FLAG_SHAPE_TABLE[shape];
  const candidates = mountsOf(unit.kind, shape, params);
  if (candidates.length === 0) {
    throw new Error(`[Flags] ${unit.kind} 型旗杆 ${unit.id} 无法挂载 ${shape} 旗形（该杆没有可用挂载点，或旗形朝向不匹配）`);
  }
  const orientation: FlagOrientation = shapeDef?.defaultOrientation ?? 'fly';
  const sameOrientation = candidates.filter((m) => FLAG_MOUNT_TABLE[m].orientation === orientation);
  const pool = sameOrientation.length > 0 ? sameOrientation : candidates;
  const head = pool.indexOf('masthead');
  if (head >= 0) return 'masthead';
  const armFly = pool.indexOf('armFly');
  if (armFly >= 0) return 'armFly';
  return pool[0];
}

export interface FlagSocketDef {
  name: 'socket_mount' | 'socket_emblem' | 'socket_flyend' | string;
  translation: V3;
  extras?: { interface: boolean; note?: string };
}

export function assemblySocketsOf(assembly: {
  attachment: FlagMountSolution;
  fly: number;
  hoist: number;
  mount: FlagMount;
  scale?: number;
}): FlagSocketDef[] {
  const a = assembly.attachment;
  const sc = assembly.scale ?? 1;
  const o: V3 = [a.origin[0] * sc, a.origin[1] * sc, a.origin[2] * sc];
  const rz = a.rotZ ?? 0;
  const c = Math.cos(rz);
  const s = Math.sin(rz);
  const rot = (x: number, y: number): [number, number] => [x * c - y * s, x * s + y * c];
  const [ex, ey] = rot(assembly.fly / 2, assembly.hoist / 2);
  const [fx, fy] = rot(assembly.fly, assembly.hoist / 2);
  const round6 = (v: number) => (Math.abs(v) < 1e-9 ? 0 : Math.round(v * 1e6) / 1e6);
  return [
    {
      name: 'socket_mount',
      translation: [round6(o[0]), round6(o[1]), round6(o[2])],
      extras: { interface: true, note: `挂载原点 ${assembly.mount}` },
    },
    {
      name: 'socket_emblem',
      translation: [round6(o[0] + ex), round6(o[1] + ey), round6(o[2])],
      extras: { interface: true, note: '徽标插入点（旗面中心）' },
    },
    {
      name: 'socket_flyend',
      translation: [round6(o[0] + fx), round6(o[1] + fy), round6(o[2])],
      extras: { interface: true, note: '旗面外缘中心' },
    },
  ];
}

export interface FlagAssembly {
  flag: string;
  pole: string;
  mount: FlagMount;
  orientation: FlagOrientation;
  attachment: FlagAttachment;
  cloth: FlagGeometry;
  poleGeometry: FlagGeometry;
  geometry: FlagGeometry;
  clothDef: FlagClothDef;
  poleDef: FlagpoleBuildDef;
  fly: number;
  hoist: number;
  scale: number;
  yaw: number;
  sockets: readonly FlagSocketDef[];
}

export function buildFlagAssembly(options: FlagAssemblyOptions): FlagAssembly {
  const opts = applyEmblem(options);
  const flagUnit = flagById.get(opts.flag);
  if (!flagUnit) throw new Error(`[Flags] 未知旗帜 id: ${opts.flag}（可用: ${FLAG_IDS.join(', ')}）`);
  const poleUnit = poleById.get(opts.pole);
  if (!poleUnit) throw new Error(`[Flags] 未知旗杆 id: ${opts.pole}（可用: ${FLAGPOLE_IDS.join(', ')}）`);
  const shapeDef = FLAG_SHAPE_TABLE[flagUnit.shape];
  const poleOptions = opts.poleOptions ?? {};
  const params = resolveFlagpoleParams(poleUnit, poleOptions);
  const mount = opts.mount ?? defaultMountOf(poleUnit, params, flagUnit.shape);
  const mountDef = FLAG_MOUNT_TABLE[mount];
  if (!mountDef) throw new Error(`[Flags] 未知挂载方式: ${mount}（可用: ${FLAG_MOUNTS.join(', ')}）`);
  if (!shapeDef.orientations.includes(mountDef.orientation)) {
    const usable = mountsOf(poleUnit.kind, flagUnit.shape, params);
    throw new Error(`[Flags] 旗形 ${flagUnit.shape}（${shapeDef.label}）只有 ${shapeDef.orientations.join('/')} 朝向，无法使用 "${mountDef.label}"；${poleUnit.kind} 杆可用挂载: ${usable.join(', ') || '无'}`);
  }
  const clothDef = resolveFlagClothDef(flagUnit, opts);
  const solution = resolveFlagMount({
    kind: poleUnit.kind,
    mount,
    params,
    fly: clothDef.fly,
    hoist: clothDef.hoist,
  });
  const attachment: FlagAttachment = { mount, orientation: mountDef.orientation, ...solution };
  const poleDef = poleBuildDef(poleUnit, params, poleOptions, attachment.collars);
  const poleGeometry = buildFlagpole(poleDef);
  const cloth = buildFlagCloth(clothDef);
  const scale = opts.scale ?? 1;
  const merged = new FlagBuilder();
  merged.bake(poleGeometry, { scale });
  merged.bake(cloth, {
    scale,
    rotZ: attachment.rotZ,
    offset: [
      attachment.origin[0] * scale,
      attachment.origin[1] * scale,
      attachment.origin[2] * scale,
    ],
  });
  const fly = clothDef.fly * scale;
  const hoist = clothDef.hoist * scale;
  const sockets = assemblySocketsOf({ attachment, fly, hoist, scale, mount });
  return {
    flag: flagUnit.id,
    pole: poleUnit.id,
    mount,
    orientation: mountDef.orientation,
    attachment,
    cloth,
    poleGeometry,
    geometry: merged.build(),
    clothDef,
    poleDef,
    fly,
    hoist,
    scale,
    yaw: opts.yaw ?? 0,
    sockets,
  };
}

export interface FlagStat {
  id: string;
  name: string;
  shape: FlagShape;
  shapeLabel: string;
  fly: number;
  hoist: number;
  trimWidth: number;
  sleeve: boolean;
  charge: string;
  waves: number;
  amplitude: number;
  columns: number;
  bands: number;
  wind: number;
  height: number;
  radius: number;
  triangles: number;
  vertices: number;
  materials: string;
  palette: ResolvedFlagPalette;
  tags: readonly string[];
}

export function statOf(id: string, options: FlagBuildOptions = {}): FlagStat {
  const opts = applyEmblem(options);
  const unit = flagById.get(id);
  if (!unit) throw new Error(`[Flags] 未知旗帜 id: ${id}（可用: ${FLAG_IDS.join(', ')}）`);
  const def = resolveFlagClothDef(unit, opts);
  const geo = buildFlagById(id, opts);
  const shapeDef = FLAG_SHAPE_TABLE[def.shape];
  const scale = opts.scale ?? 1;
  return {
    id: def.id,
    name: def.name,
    shape: def.shape,
    shapeLabel: shapeDef?.label ?? def.shape,
    fly: def.fly * scale,
    hoist: def.hoist * scale,
    trimWidth: def.trimWidth,
    sleeve: def.sleeve,
    charge: def.charge.kind,
    waves: def.wave.waves,
    amplitude: def.wave.amplitude,
    columns: def.wave.columns,
    bands: def.wave.bands,
    wind: def.wind,
    height: geo.height,
    radius: geo.radius,
    triangles: geo.triangleCount,
    vertices: geo.vertexCount,
    materials: geo.groups.map((g) => `${g.mat}:${g.count / 3}`).join(' '),
    palette: def.palette,
    tags: def.tags,
  };
}

export interface FlagpoleStat {
  id: string;
  name: string;
  kind: FlagpoleKind;
  height: number;
  radius: number;
  sides: number;
  segments: number;
  joints: number;
  base: string;
  finial: string;
  plate: string;
  arm: string;
  triangles: number;
  vertices: number;
  materials: string;
  palette: ResolvedFlagPalette;
  tags: readonly string[];
}

export function poleStatOf(id: string, options: FlagpoleBuildOptions = {}): FlagpoleStat {
  const unit = poleById.get(id);
  if (!unit) throw new Error(`[Flags] 未知旗杆 id: ${id}（可用: ${FLAGPOLE_IDS.join(', ')}）`);
  const params = resolveFlagpoleParams(unit, options);
  const geo = buildFlagpoleById(id, options);
  const arm = params.arm;
  return {
    id: unit.id,
    name: unit.name,
    kind: unit.kind,
    height: params.height,
    radius: params.radius,
    sides: params.sides,
    segments: params.segments,
    joints: params.joints,
    base: params.base.style,
    finial: params.finial.style,
    plate: params.plate.style,
    arm: arm ? `L${arm.length.toFixed(2)}@${arm.at.toFixed(2)}` : 'none',
    triangles: geo.triangleCount,
    vertices: geo.vertexCount,
    materials: geo.groups.map((g) => `${g.mat}:${g.count / 3}`).join(' '),
    palette: resolveFlagPalette(FLAGPOLE_KIND_PALETTE[unit.kind], unit.palette, options.palette),
    tags: unit.tags ?? [],
  };
}

export interface FlagAssemblyStat {
  flag: string;
  flagName: string;
  pole: string;
  poleName: string;
  kind: FlagpoleKind;
  mount: FlagMount;
  mountLabel: string;
  orientation: FlagOrientation;
  fly: number;
  hoist: number;
  poleHeight: number;
  height: number;
  radius: number;
  bottom: number;
  triangles: number;
  vertices: number;
  materials: string;
  clothTriangles: number;
  poleTriangles: number;
  collars: readonly number[];
}

export function assemblyStatOf(options: FlagAssemblyOptions): FlagAssemblyStat {
  const assembly = buildFlagAssembly(options);
  return {
    flag: assembly.flag,
    flagName: FLAG_DEFS.find((u) => u.id === assembly.flag)?.name ?? assembly.flag,
    pole: assembly.pole,
    poleName: assembly.poleDef.name,
    kind: assembly.poleDef.kind,
    mount: assembly.mount,
    mountLabel: FLAG_MOUNT_TABLE[assembly.mount].label,
    orientation: assembly.orientation,
    fly: assembly.fly,
    hoist: assembly.hoist,
    poleHeight: assembly.poleDef.params.height * assembly.scale,
    height: assembly.geometry.height,
    radius: assembly.geometry.radius,
    bottom: assembly.geometry.bottom,
    triangles: assembly.geometry.triangleCount,
    vertices: assembly.geometry.vertexCount,
    materials: assembly.geometry.groups.map((g) => `${g.mat}:${g.count / 3}`).join(' '),
    clothTriangles: assembly.cloth.triangleCount,
    poleTriangles: assembly.poleGeometry.triangleCount,
    collars: assembly.attachment.collars,
  };
}

export function assemblyMatrixOf(): readonly {
  flag: string;
  pole: string;
  kind: FlagpoleKind;
  mounts: readonly FlagMount[];
}[] {
  const out: { flag: string; pole: string; kind: FlagpoleKind; mounts: readonly FlagMount[] }[] = [];
  for (const pole of poleUnits) {
    const params = resolveFlagpoleParams(pole);
    for (const flag of flagUnits) {
      out.push({ flag: flag.id, pole: pole.id, kind: pole.kind, mounts: mountsOf(pole.kind, flag.shape, params) });
    }
  }
  return out;
}
