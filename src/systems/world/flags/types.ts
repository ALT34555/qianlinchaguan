/** 旗帜与旗杆系统数据模型 */

export type V3 = [number, number, number];
export type RGB = [number, number, number];

export type FlagShape =
  | 'rect'
  | 'square'
  | 'swallowtail'
  | 'pennant'
  | 'notched'
  | 'roundFly'
  | 'gonfalon';

export const FLAG_SHAPES: readonly FlagShape[] = [
  'rect', 'square', 'swallowtail', 'pennant', 'notched', 'roundFly', 'gonfalon',
];

export const FLAG_SHAPE_CODES: Readonly<Record<FlagShape, number>> = Object.freeze(
  Object.fromEntries(FLAG_SHAPES.map((s, i) => [s, i])) as Record<FlagShape, number>,
);

export function flagShapeOfCode(code: number): FlagShape {
  return FLAG_SHAPES[code] ?? 'rect';
}

export type FlagOrientation = 'fly' | 'hang';

export const FLAG_ORIENTATIONS: readonly FlagOrientation[] = ['fly', 'hang'];

export type FlagChargeKind =
  | 'none'
  | 'square';

export const FLAG_CHARGE_KINDS: readonly FlagChargeKind[] = ['none', 'square'];

export type FlagpoleKind =
  | 'ground'
  | 'horizontal'
  | 'stick';

export const FLAGPOLE_KINDS: readonly FlagpoleKind[] = ['ground', 'horizontal', 'stick'];

export type FlagMount =
  | 'masthead'
  | 'midmast'
  | 'armFly'
  | 'armHang';

export const FLAG_MOUNTS: readonly FlagMount[] = ['masthead', 'midmast', 'armFly', 'armHang'];

export type FlagColorSlot =
  | 'field'
  | 'trim'
  | 'charge'
  | 'emblem'
  | 'pole'
  | 'metal'
  | 'stone'
  | 'cord';

export const FLAG_COLOR_SLOTS: readonly FlagColorSlot[] = [
  'field', 'trim', 'charge', 'emblem', 'pole', 'metal', 'stone', 'cord',
];

export type FlagMaterial =
  | 'cloth'
  | 'trim'
  | 'charge'
  | 'emblem'
  | 'pole'
  | 'metal'
  | 'stone';

export const FLAG_MATERIALS: readonly FlagMaterial[] = [
  'cloth', 'trim', 'charge', 'emblem', 'pole', 'metal', 'stone',
];

export type FlagpoleBaseStyle = 'none' | 'slab' | 'step' | 'stone' | 'cross';

export const FLAGPOLE_BASE_STYLES: readonly FlagpoleBaseStyle[] = [
  'none', 'slab', 'step', 'stone', 'cross',
];

export type FlagpoleFinialStyle = 'none' | 'knob' | 'ball' | 'spear' | 'flame' | 'cross';

export const FLAGPOLE_FINIAL_STYLES: readonly FlagpoleFinialStyle[] = [
  'none', 'knob', 'ball', 'spear', 'flame', 'cross',
];

export type FlagpoleCollarStyle = 'none' | 'ring' | 'band';

export const FLAGPOLE_COLLAR_STYLES: readonly FlagpoleCollarStyle[] = ['none', 'ring', 'band'];

export type FlagpolePlateStyle = 'none' | 'plate' | 'bracket';

export const FLAGPOLE_PLATE_STYLES: readonly FlagpolePlateStyle[] = ['none', 'plate', 'bracket'];

export interface FlagWaveSpec {
  amplitude?: number;
  waves?: number;
  ripple?: number;
  phase?: number;
  droop?: number;
  twist?: number;
  columns?: number;
  bands?: number;
  doubleSided?: boolean;
}

export interface ResolvedFlagWave {
  amplitude: number;
  waves: number;
  ripple: number;
  phase: number;
  droop: number;
  twist: number;
  columns: number;
  bands: number;
  doubleSided: boolean;
}

export interface FlagChargeSpec {
  kind?: FlagChargeKind;
  size?: number;
  offsetX?: number;
  offsetY?: number;
  rotation?: number;
  lift?: number;
  slot?: FlagColorSlot;
}

export interface ResolvedFlagCharge {
  kind: FlagChargeKind;
  size: number;
  offsetX: number;
  offsetY: number;
  rotation: number;
  lift: number;
  slot: FlagColorSlot;
}

export interface FlagPalette {
  field?: string;
  trim?: string;
  charge?: string;
  emblem?: string;
  pole?: string;
  metal?: string;
  stone?: string;
  cord?: string;
}

export type ResolvedFlagPalette = Partial<Record<FlagColorSlot, string>>;

export interface FlagParams {
  shape?: FlagShape;
  fly?: number;
  hoist?: number;
  ratio?: number;
  trimWidth?: number;
  sleeve?: boolean;
  wave?: FlagWaveSpec;
  charge?: FlagChargeSpec;
}

export interface FlagDef {
  id: string;
  name: string;
  latin?: string;
  shape: FlagShape;
  params?: FlagParams;
  palette?: FlagPalette;
  tags?: string[];
  source?: string;
}

export interface FlagFile {
  flags: FlagDef[];
}

export interface FlagpoleBaseSpec {
  style?: FlagpoleBaseStyle;
  radius?: number;
  height?: number;
  sides?: number;
}

export interface FlagpoleFinialSpec {
  style?: FlagpoleFinialStyle;
  size?: number;
}

export interface FlagpoleCollarSpec {
  style?: FlagpoleCollarStyle;
  size?: number;
}

export interface FlagpolePlateSpec {
  style?: FlagpolePlateStyle;
  width?: number;
  height?: number;
  depth?: number;
}

export interface FlagpoleArmSpec {
  length?: number;
  at?: number;
  rise?: number;
  radius?: number;
  sides?: number;
  tipKnob?: number;
  brace?: number;
}

export interface FlagpoleParams {
  height?: number;
  radius?: number;
  sides?: number;
  tipRatio?: number;
  segments?: number;
  joints?: number;
  base?: FlagpoleBaseSpec;
  finial?: FlagpoleFinialSpec;
  collar?: FlagpoleCollarSpec;
  plate?: FlagpolePlateSpec;
  arm?: FlagpoleArmSpec | null;
  cord?: number;
  tiltX?: number;
  tiltZ?: number;
}

export interface FlagpoleDef {
  id: string;
  name: string;
  latin?: string;
  kind: FlagpoleKind;
  params?: FlagpoleParams;
  palette?: FlagPalette;
  tags?: string[];
  source?: string;
}

export interface FlagpoleFile {
  poles: FlagpoleDef[];
}

export interface FlagpoleArmResolved {
  length: number;
  at: number;
  rise: number;
  radius: number;
  sides: number;
  tipKnob: number;
  brace: number;
}

export interface ResolvedFlagpoleParams {
  height: number;
  radius: number;
  sides: number;
  tipRatio: number;
  segments: number;
  joints: number;
  base: Required<FlagpoleBaseSpec>;
  finial: Required<FlagpoleFinialSpec>;
  collar: Required<FlagpoleCollarSpec>;
  plate: Required<FlagpolePlateSpec>;
  arm: FlagpoleArmResolved | null;
  cord: number;
  tiltX: number;
  tiltZ: number;
}

export interface FlagPaletteEntry {
  default: string;
  note?: string;
}

export interface FlagPaletteFile {
  cloth?: Record<string, FlagPaletteEntry>;
  hardware?: Record<string, FlagPaletteEntry>;
}

export interface FlagEmblemDef {
  id: string;
  name: string;
  latin?: string;
  charge?: FlagChargeSpec;
  palette?: FlagPalette;
  tags?: string[];
}

export interface FlagEmblemFile {
  emblems: FlagEmblemDef[];
}

export interface FlagFaceSink {
  push(pos: number[], col: RGB, nrm: V3, mat: FlagMaterial): void;
  tri(a: V3, b: V3, c: V3, col: RGB, mat: FlagMaterial): void;
  quad(a: V3, b: V3, c: V3, d: V3, col: RGB, mat: FlagMaterial): void;
}

export interface FlagClothSurface {
  pointAt(u: number, v: number): V3;
  normalAt(u: number, v: number): V3;
}

export interface FlagChargeContext {
  sink: FlagFaceSink;
  surface: FlagClothSurface;
  spec: ResolvedFlagCharge;
  color: RGB;
  mat: FlagMaterial;
  fly: number;
  hoist: number;
  shapeSeed: number;
}

export type FlagChargePainter = (ctx: FlagChargeContext) => void;

export interface FlagClothDef {
  id: string;
  name: string;
  latin?: string;
  shape: FlagShape;
  fly: number;
  hoist: number;
  trimWidth: number;
  sleeve: boolean;
  wave: ResolvedFlagWave;
  charge: ResolvedFlagCharge;
  palette: ResolvedFlagPalette;
  wind: number;
  shapeSeed: number;
  buildSeed: number;
  tags: readonly string[];
}

export interface FlagpoleBuildDef {
  id: string;
  name: string;
  latin?: string;
  kind: FlagpoleKind;
  params: ResolvedFlagpoleParams;
  palette: ResolvedFlagPalette;
  collars: readonly number[];
  shapeSeed: number;
  buildSeed: number;
  tags: readonly string[];
}

export interface FlagMountDef {
  id: FlagMount;
  label: string;
  latin: string;
  kinds: readonly FlagpoleKind[];
  orientation: FlagOrientation;
  needsArm: boolean;
  forbidsArm?: boolean;
}

export interface FlagMountSolution {
  origin: V3;
  rotZ: number;
  yaw: number;
  collars: readonly number[];
  clearance: number;
}

export interface FlagAttachment extends FlagMountSolution {
  mount: FlagMount;
  orientation: FlagOrientation;
}

export interface FlagBuildOptions {
  shape?: FlagShape;
  fly?: number;
  hoist?: number;
  ratio?: number;
  trimWidth?: number;
  sleeve?: boolean;
  wave?: FlagWaveSpec;
  charge?: FlagChargeSpec;
  palette?: FlagPalette;
  wind?: number;
  shapeSeed?: number;
  scale?: number;
  emblem?: string;
}

export interface FlagpoleBuildOptions {
  height?: number;
  radius?: number;
  sides?: number;
  tipRatio?: number;
  segments?: number;
  joints?: number;
  base?: FlagpoleBaseSpec;
  finial?: FlagpoleFinialSpec;
  collar?: FlagpoleCollarSpec;
  plate?: FlagpolePlateSpec;
  arm?: FlagpoleArmSpec;
  cord?: number;
  tiltX?: number;
  tiltZ?: number;
  palette?: FlagPalette;
  shapeSeed?: number;
  scale?: number;
}

export interface FlagAssemblyOptions extends FlagBuildOptions {
  flag: string;
  pole: string;
  mount?: FlagMount;
  poleOptions?: FlagpoleBuildOptions;
  yaw?: number;
}

export interface FlagDeployAnchor {
  x: number;
  z: number;
  y: number;
  mode: 'ground' | 'wall' | 'hand';
}

export interface FlagPlacement {
  x: number;
  z: number;
  y: number;
  pole: string;
  flag: string;
  mount: FlagMount;
  yaw: number;
  scale: number;
  wind: number;
  shapeSeed: number;
}

export interface SurfaceQuery {
  height(x: number, z: number): number;
  waterLevel(x: number, z: number): number;
  surface(x: number, z: number): number;
}
