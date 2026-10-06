/** 旗帜与旗杆几何原型 */

import { hashString, mulberry32 } from '../../../core/math/Random';
import {
  FlagBuilder, addV3, clamp, clamp01, crossV3, hexToRgb, lighten, mix,
  normV3, scaleV3, subV3,
} from './geometry';
import type { FlagGeometry } from './geometry';
import {
  FLAG_MOUNTS, FLAGPOLE_BASE_STYLES, FLAGPOLE_FINIAL_STYLES,
  type FlagChargeContext, type FlagChargeKind, type FlagChargePainter, type FlagClothDef,
  type FlagClothSurface, type FlagMaterial, type FlagMount, type FlagMountDef, type FlagMountSolution,
  type FlagOrientation, type FlagShape, type FlagpoleBaseSpec, type FlagpoleBuildDef, type FlagpoleFinialSpec,
  type FlagpoleKind, type FlagpoleParams, type ResolvedFlagWave, type RGB, type V3,
} from './types';

const TAU = Math.PI * 2;
const GAP = 0.04;
const HANG_GAP = 0.035;

export interface FlagShapeDef {
  code: number;
  label: string;
  latin: string;
  ratio: number;
  hoist: number;
  columns: number;
  bands: number;
  trimWidth: number;
  sleeve: boolean;
  orientations: readonly FlagOrientation[];
  defaultOrientation: FlagOrientation;
  profile: (t: number) => number;
}

function notch(t: number, width: number): number {
  return Math.max(0, 1 - Math.abs(t - 0.5) / width);
}

export const FLAG_SHAPE_TABLE: Readonly<Record<FlagShape, FlagShapeDef>> = Object.freeze({
  rect: {
    code: 0, label: '长方旗', latin: 'rect', ratio: 1.5, hoist: 1, columns: 6, bands: 5,
    trimWidth: 0.11, sleeve: true, orientations: ['fly', 'hang'], defaultOrientation: 'fly',
    profile: () => 1,
  },
  square: {
    code: 1, label: '方旗', latin: 'square', ratio: 1, hoist: 0.9, columns: 5, bands: 5,
    trimWidth: 0.1, sleeve: true, orientations: ['fly', 'hang'], defaultOrientation: 'fly',
    profile: () => 1,
  },
  swallowtail: {
    code: 2, label: '燕尾旗', latin: 'swallowtail', ratio: 1.65, hoist: 1, columns: 7, bands: 5,
    trimWidth: 0.1, sleeve: true, orientations: ['fly', 'hang'], defaultOrientation: 'fly',
    profile: (t) => 1 - 0.5 * notch(t, 0.3),
  },
  pennant: {
    code: 3, label: '三角尖旗', latin: 'pennant', ratio: 1.9, hoist: 0.8, columns: 6, bands: 5,
    trimWidth: 0.09, sleeve: true, orientations: ['fly', 'hang'], defaultOrientation: 'fly',
    profile: (t) => 1 - Math.abs(2 * t - 1),
  },
  notched: {
    code: 4, label: '缺口尾旗', latin: 'notched', ratio: 1.55, hoist: 0.95, columns: 6, bands: 5,
    trimWidth: 0.1, sleeve: true, orientations: ['fly', 'hang'], defaultOrientation: 'fly',
    profile: (t) => 1 - 0.17 * notch(t, 0.17),
  },
  roundFly: {
    code: 5, label: '圆尾旗', latin: 'roundFly', ratio: 1.35, hoist: 1, columns: 7, bands: 5,
    trimWidth: 0.1, sleeve: true, orientations: ['fly', 'hang'], defaultOrientation: 'fly',
    profile: (t) => 1 - 0.34 * (1 - Math.sin(Math.PI * t)),
  },
  gonfalon: {
    code: 6, label: '垂幅幡', latin: 'gonfalon', ratio: 1.7, hoist: 0.85, columns: 6, bands: 6,
    trimWidth: 0.09, sleeve: true, orientations: ['hang'], defaultOrientation: 'hang',
    profile: (t) => 1 - 0.38 * notch(t, 0.5),
  },
});

export const FLAG_GLOBAL_DEFAULTS = Object.freeze({
  fly: 1.5,
  hoist: 1,
  ratio: 1.5,
  trimWidth: 0.11,
  sleeve: true,
  wind: 0.75,
});

export const FLAG_WAVE_DEFAULTS: ResolvedFlagWave = Object.freeze({
  amplitude: 0.12,
  waves: 1.35,
  ripple: 0.4,
  phase: 0.22,
  droop: 0.12,
  twist: 0.55,
  columns: 6,
  bands: 5,
  doubleSided: true,
});

const BASE_DEFAULTS: Required<FlagpoleBaseSpec> = { style: 'none', radius: 0, height: 0.3, sides: 6 };
const FINIAL_DEFAULTS: Required<FlagpoleFinialSpec> = { style: 'ball', size: 1 };
const COLLAR_DEFAULTS = { style: 'ring' as const, size: 1 };
const PLATE_DEFAULTS = { style: 'none' as const, width: 0.42, height: 0.6, depth: 0.09 };
const GROUND_BASE: Required<FlagpoleBaseSpec> = { style: 'step', radius: 0, height: 0.34, sides: 6 };
const STICK_BASE: Required<FlagpoleBaseSpec> = { style: 'none', radius: 0, height: 0.2, sides: 6 };
const HORIZONTAL_BASE: Required<FlagpoleBaseSpec> = { style: 'none', radius: 0, height: 0.24, sides: 6 };
const NO_FINIAL: Required<FlagpoleFinialSpec> = { style: 'none', size: 1 };
const BAND_COLLAR = { style: 'band' as const, size: 1 };
const STICK_COLLAR = { style: 'ring' as const, size: 0.8 };
const PLATE_STANDARD = { style: 'plate' as const, width: 0.44, height: 0.62, depth: 0.09 };
const STICK_PLATE = { style: 'none' as const, width: 0.24, height: 0.3, depth: 0.05 };

export const FLAGPOLE_GLOBAL_DEFAULTS: FlagpoleParams = Object.freeze({
  height: 5.2,
  radius: 0.085,
  sides: 6,
  tipRatio: 0.74,
  segments: 3,
  joints: 2,
  base: BASE_DEFAULTS,
  finial: FINIAL_DEFAULTS,
  collar: COLLAR_DEFAULTS,
  plate: PLATE_DEFAULTS,
  arm: null,
  cord: 0,
  tiltX: 0,
  tiltZ: 0,
});

export const FLAGPOLE_KIND_DEFAULTS: Readonly<Record<FlagpoleKind, FlagpoleParams>> = Object.freeze({
  ground: {
    height: 5.2,
    radius: 0.085,
    tipRatio: 0.74,
    segments: 3,
    joints: 2,
    base: GROUND_BASE,
    finial: FINIAL_DEFAULTS,
  },
  horizontal: {
    height: 3.2,
    radius: 0.07,
    sides: 6,
    tipRatio: 0.84,
    segments: 1,
    joints: 0,
    base: HORIZONTAL_BASE,
    finial: NO_FINIAL,
    collar: BAND_COLLAR,
    plate: PLATE_STANDARD,
    arm: { length: 2.2, rise: 0, radius: 0.062, sides: 6, tipKnob: 1.75, brace: 0 },
  },
  stick: {
    height: 1.9,
    radius: 0.042,
    sides: 5,
    tipRatio: 0.95,
    segments: 1,
    joints: 0,
    base: STICK_BASE,
    finial: { style: 'knob', size: 1 },
    collar: STICK_COLLAR,
    plate: STICK_PLATE,
    arm: null,
  },
});

export const FLAG_MOUNT_TABLE: Readonly<Record<FlagMount, FlagMountDef>> = Object.freeze({
  masthead: {
    id: 'masthead', label: '杆顶飘扬', latin: 'masthead',
    kinds: ['ground', 'stick'], orientation: 'fly', needsArm: false, forbidsArm: true,
  },
  midmast: {
    id: 'midmast', label: '杆中悬挂', latin: 'midmast',
    kinds: ['ground', 'stick'], orientation: 'fly', needsArm: false, forbidsArm: true,
  },
  armFly: {
    id: 'armFly', label: '杆头飘扬', latin: 'armFly',
    kinds: ['ground', 'horizontal', 'stick'], orientation: 'fly', needsArm: true,
  },
  armHang: {
    id: 'armHang', label: '横木垂挂', latin: 'armHang',
    kinds: ['ground', 'horizontal', 'stick'], orientation: 'hang', needsArm: true,
  },
});

export interface PoleShape {
  height: number;
  radius: number;
  tipRatio: number;
}

export function radiusAt(params: PoleShape, y: number): number {
  const t = clamp01(y / Math.max(params.height, 1e-3));
  return params.radius * (1 - (1 - params.tipRatio) * t);
}

export function finialClearance(params: PoleShape & { finial: { style: string; size: number } }): number {
  const style = params.finial.style;
  if (style === 'none') return 0.05;
  if (style === 'knob') return params.radius * 2.4;
  if (style === 'ball') return params.radius * 3.6;
  return params.radius * 4.4;
}

export function clothSurface(fly: number, hoist: number, wave: ResolvedFlagWave, wind: number): FlagClothSurface {
  const amp = wave.amplitude * (0.12 + 0.88 * clamp01(wind));
  const phase = wave.phase;
  const droop = wave.droop * hoist;
  const twist = wave.twist;
  const cy = hoist / 2;
  const pointAt = (u: number, v: number): V3 => {
    const uu = clamp01(u);
    const vv = clamp01(v);
    const z = amp * (0.25 + 0.75 * uu) * Math.sin(TAU * (uu * wave.waves - phase))
      + amp * wave.ripple * Math.sin(TAU * (uu * wave.waves * 1.7 - phase * 1.3 + vv * 1.4));
    const y0 = vv * hoist - droop * uu * uu;
    const a = twist * uu;
    const dy = y0 - cy;
    const c = Math.cos(a);
    const s = Math.sin(a);
    return [uu * fly, cy + dy * c - z * s, dy * s + z * c];
  };
  const normalAt = (u: number, v: number): V3 => {
    const eps = 1 / 512;
    const fwd = u + eps <= 1;
    const up = v + eps <= 1;
    const p0 = pointAt(u, v);
    const du = fwd ? subV3(pointAt(u + eps, v), p0) : subV3(p0, pointAt(u - eps, v));
    const dv = up ? subV3(pointAt(u, v + eps), p0) : subV3(p0, pointAt(u, v - eps));
    return normV3(crossV3(du, dv));
  };
  return { pointAt, normalAt };
}

export const CHARGE_PAINTERS: ReadonlyMap<string, FlagChargePainter> = new Map<string, FlagChargePainter>([
  ['none', () => { /* 无图案 */ }],
  ['square', paintSquareCharge],
]);

export function registerFlagChargePainter(id: string, painter: FlagChargePainter): void {
  (CHARGE_PAINTERS as Map<string, FlagChargePainter>).set(id, painter);
}

function paintSquareCharge(ctx: FlagChargeContext): void {
  const { surface, sink, spec, color, mat, fly, hoist } = ctx;
  const half = (clamp(spec.size, 0.02, 4) * hoist) / 2;
  const cx = fly / 2 + spec.offsetX * fly;
  const cy = hoist / 2 + spec.offsetY * hoist;
  const c = Math.cos(spec.rotation);
  const s = Math.sin(spec.rotation);
  const corner = (sx: number, sy: number): V3 => {
    const ox = sx * half;
    const oy = sy * half;
    const u = clamp01((cx + ox * c - oy * s) / fly);
    const v = clamp01((cy + ox * s + oy * c) / hoist);
    const p = surface.pointAt(u, v);
    const n = surface.normalAt(u, v);
    return addV3(p, scaleV3(n, spec.lift));
  };
  sink.quad(corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1), color, mat);
}

export function chargePainterOf(kind: FlagChargeKind): FlagChargePainter {
  const painter = CHARGE_PAINTERS.get(kind);
  if (!painter) {
    throw new Error(`[Flags] 旗帜图案 "${kind}" 未注册，可用: ${[...CHARGE_PAINTERS.keys()].sort().join(', ')}`);
  }
  return painter;
}

export function chargeKinds(): readonly string[] {
  return [...CHARGE_PAINTERS.keys()].sort();
}

function axisEdges(head: number, tail: number, interior: number): number[] {
  const h = clamp01(head);
  const t = clamp01(tail);
  const inner = Math.max(1, Math.round(interior));
  const a = h;
  const b = 1 - t;
  const edges: number[] = [0];
  if (h > 1e-4) edges.push(h);
  for (let k = 1; k <= inner; k++) edges.push(a + (b - a) * (k / inner));
  if (t > 1e-4) edges.push(1);
  return edges;
}

export function buildFlagCloth(def: FlagClothDef): FlagGeometry {
  const shape = FLAG_SHAPE_TABLE[def.shape];
  if (!shape) throw new Error(`[Flags] 未知旗形: ${def.shape}（可用: ${Object.keys(FLAG_SHAPE_TABLE).join(', ')}）`);
  const wave = def.wave;
  const columns = Math.max(3, Math.round(wave.columns) || shape.columns);
  const bands = Math.max(3, Math.round(wave.bands) || shape.bands);
  const trimU = def.trimWidth > 0 ? Math.min(0.36, def.trimWidth / def.fly) : 0;
  const trimV = def.trimWidth > 0 ? Math.min(0.36, def.trimWidth / def.hoist) : 0;
  const sleeveU = def.sleeve ? Math.min(0.28, Math.max(0.05, (def.trimWidth * 1.6) / def.fly)) : 0;
  const uEdges = axisEdges(sleeveU, trimU, columns - (sleeveU > 0 ? 1 : 0) - (trimU > 0 ? 1 : 0));
  const vEdges = axisEdges(trimV, trimV, bands - (trimV > 0 ? 2 : 0));
  const rand = mulberry32(hashString(`${def.id}:${def.shape}:${def.shapeSeed}`) ^ def.buildSeed);
  const inner = new FlagBuilder(rand);
  const surface = clothSurface(def.fly, def.hoist, wave, def.wind);
  const fieldCol = hexToRgb(def.palette.field, [176, 58, 46]);
  const trimCol = hexToRgb(def.palette.trim ?? def.palette.field, [214, 178, 74]);
  const chargeSlot = def.charge.slot;
  const chargeCol = hexToRgb(def.palette[chargeSlot] ?? def.palette.trim ?? def.palette.field, [230, 222, 208]);
  const chargeMat: FlagMaterial = chargeSlot === 'emblem' ? 'emblem' : 'charge';
  const uEnd = (t: number): number => clamp(shape.profile(clamp01(t)), 0.04, 1);
  for (let j = 0; j + 1 < vEdges.length; j++) {
    const v0 = vEdges[j];
    const v1 = vEdges[j + 1];
    const e0 = uEnd(v0);
    const e1 = uEnd(v1);
    for (let i = 0; i + 1 < uEdges.length; i++) {
      const a0 = uEdges[i] * e0;
      const a1 = uEdges[i + 1] * e0;
      const b0 = uEdges[i] * e1;
      const b1 = uEdges[i + 1] * e1;
      if (Math.max(a1, b1) - Math.min(a0, b0) < 1e-4) continue;
      const trim = (sleeveU > 0 && i === 0)
        || (trimU > 0 && i + 2 === uEdges.length)
        || (trimV > 0 && (j === 0 || j + 2 === vEdges.length));
      const col = trim ? trimCol : fieldCol;
      const mat: FlagMaterial = trim ? 'trim' : 'cloth';
      inner.quad(
        surface.pointAt(a0, v0), surface.pointAt(a1, v0),
        surface.pointAt(b1, v1), surface.pointAt(b0, v1),
        col, mat,
      );
    }
  }
  if (def.charge.kind !== 'none') {
    const painter = chargePainterOf(def.charge.kind);
    painter({
      sink: inner,
      surface,
      spec: def.charge,
      color: chargeCol,
      mat: chargeMat,
      fly: def.fly,
      hoist: def.hoist,
      shapeSeed: def.shapeSeed,
    });
  }
  const geo = inner.build();
  if (!wave.doubleSided) return geo;
  const out = new FlagBuilder(rand);
  out.bake(geo);
  out.bake(geo, { flip: true });
  return out.build();
}

function buildBase(b: FlagBuilder, params: FlagpoleBuildDef['params'], color: RGB): void {
  const spec = params.base;
  if (spec.style === 'none' || !(FLAGPOLE_BASE_STYLES as readonly string[]).includes(spec.style)) return;
  const r = spec.radius > 0 ? spec.radius : params.radius * 3.2;
  const h = Math.max(0.06, spec.height);
  switch (spec.style) {
    case 'slab':
      b.box([0, h / 2, 0], [r * 2.4, h, r * 2.4], color, { mat: 'stone' });
      break;
    case 'step':
      b.box([0, h * 0.28, 0], [r * 3, h * 0.56, r * 3], color, { mat: 'stone' });
      b.box([0, h * 0.75, 0], [r * 2.05, h * 0.5, r * 2.05], lighten(color, 0.06), { mat: 'stone' });
      break;
    case 'stone':
      b.rod([0, 0, 0], [0, h, 0], r, color, {
        radius2: r * 0.84, sides: Math.max(4, Math.round(spec.sides)), mat: 'stone',
      });
      break;
    case 'cross':
      b.box([0, h / 2, 0], [r * 3.4, h, r], color, { mat: 'stone' });
      b.box([0, h / 2, 0], [r, h, r * 3.4], color, { mat: 'stone' });
      break;
    default:
      break;
  }
}

function buildFinial(b: FlagBuilder, params: FlagpoleBuildDef['params'], metal: RGB, wood: RGB): void {
  const spec = params.finial;
  if (spec.style === 'none') return;
  const size = clamp(spec.size, 0.2, 4);
  const r = params.radius;
  const top = params.height;
  const style = (FLAGPOLE_FINIAL_STYLES as readonly string[]).includes(spec.style) ? spec.style : 'none';
  const sides = Math.max(4, params.sides - 1);
  switch (style) {
    case 'knob':
      b.rod([0, top, 0], [0, top + r * 1.2, 0], r * 0.9, wood, { radius2: r * 0.7, sides: params.sides, mat: 'pole' });
      b.ball([0, top + r * 1.2 + r * 1.5 * size, 0], r * 1.7 * size, metal, { sides, rings: 1, mat: 'metal' });
      break;
    case 'ball': {
      const ball = r * 2.6 * size;
      b.rod([0, top, 0], [0, top + ball * 0.5, 0], r * 0.62, wood, { radius2: r * 0.5, sides: params.sides, mat: 'pole' });
      b.ball([0, top + ball * 0.5 + ball * 0.85, 0], ball, metal, { sides, rings: 1, mat: 'metal' });
      break;
    }
    case 'spear': {
      const spike = r * 9 * size;
      b.rod([0, top, 0], [0, top + spike * 0.18, 0], r * 1.15, metal, { radius2: r * 0.85, sides: params.sides, mat: 'metal' });
      b.rod([0, top + spike * 0.18, 0], [0, top + spike * 0.18 + spike, 0], r * 1.5, metal, {
        radius2: 0, sides, mat: 'metal',
      });
      break;
    }
    case 'flame': {
      const flame = r * 6.2 * size;
      b.rod([0, top, 0], [0, top + flame * 0.2, 0], r * 1.5, metal, { radius2: r * 1.1, sides: params.sides, mat: 'metal' });
      b.rod([0, top + flame * 0.2, 0], [0, top + flame * 0.2 + flame, 0], r * 1.9, metal, {
        radius2: 0, sides: 4, rotY: Math.PI / 4, mat: 'metal',
      });
      break;
    }
    case 'cross': {
      const h = r * 7 * size;
      b.rod([0, top, 0], [0, top + h, 0], r * 0.5, metal, { sides: 4, mat: 'metal' });
      b.rod([-r * 2.6, top + h * 0.68, 0], [r * 2.6, top + h * 0.68, 0], r * 0.42, metal, { sides: 4, mat: 'metal' });
      break;
    }
    default:
      break;
  }
}

function buildArm(b: FlagBuilder, params: FlagpoleBuildDef['params'], wood: RGB, metal: RGB): void {
  const arm = params.arm;
  if (!arm) return;
  const x0 = params.plate.style === 'none' ? 0 : params.plate.depth;
  const start: V3 = [x0, arm.at, 0];
  const tip: V3 = [x0 + arm.length, arm.at + arm.rise, 0];
  b.rod(start, tip, arm.radius, wood, {
    radius2: arm.radius * 0.82, sides: arm.sides, color2: lighten(wood, 0.1), mat: 'pole',
  });
  if (arm.tipKnob > 0) {
    b.ball(tip, arm.radius * arm.tipKnob, metal, { sides: Math.max(4, arm.sides - 1), rings: 1, mat: 'metal' });
  }
  if (arm.brace > 0) {
    b.rod([0, arm.at - arm.brace, 0], [x0 + arm.length * 0.45, arm.at + arm.rise * 0.45, 0], arm.radius * 0.55, wood, {
      sides: 5, mat: 'pole',
    });
  }
}

function buildPlate(b: FlagBuilder, params: FlagpoleBuildDef['params'], metal: RGB): void {
  const spec = params.plate;
  if (spec.style === 'none' || !params.arm) return;
  const at = params.arm.at;
  b.box([spec.depth / 2, at, 0], [spec.depth, spec.height, spec.width], metal, { mat: 'metal' });
  if (spec.style === 'bracket') {
    b.rod([0, at - spec.height * 0.95, 0], [spec.depth + params.arm.length * 0.42, at, 0], params.arm.radius * 0.5, metal, {
      sides: 4, mat: 'metal',
    });
  }
}

export function buildFlagpole(def: FlagpoleBuildDef): FlagGeometry {
  const params = def.params;
  const rand = mulberry32(hashString(`${def.id}:${def.kind}:${def.shapeSeed}`) ^ def.buildSeed);
  const inner = new FlagBuilder(rand);
  const wood = hexToRgb(def.palette.pole, [138, 106, 69]);
  const metal = hexToRgb(def.palette.metal, [165, 132, 63]);
  const stone = hexToRgb(def.palette.stone, [139, 139, 139]);
  const cord = hexToRgb(def.palette.cord ?? def.palette.metal, [165, 132, 63]);
  buildBase(inner, params, stone);
  const segments = Math.max(1, Math.round(params.segments));
  let y = 0;
  for (let s = 0; s < segments; s++) {
    const y1 = params.height * ((s + 1) / segments);
    const r0 = radiusAt(params, y);
    const r1 = radiusAt(params, y1);
    const c0 = mix(wood, lighten(wood, 0.16), s / segments);
    const c1 = mix(wood, lighten(wood, 0.16), (s + 1) / segments);
    inner.rod([0, y, 0], [0, y1, 0], r0, c0, {
      radius2: r1, sides: params.sides, color2: c1, mat: 'pole',
    });
    if (params.joints > 0 && s < segments - 1) {
      const band = Math.min(0.035, (y1 - y) * 0.08);
      inner.rod([0, y1 - band, 0], [0, y1 + band, 0], r1 * 1.32, metal, {
        sides: params.sides, mat: 'metal', caps: false,
      });
    }
    y = y1;
  }
  buildPlate(inner, params, metal);
  buildArm(inner, params, wood, metal);
  buildFinial(inner, params, metal, wood);
  if (params.collar.style !== 'none') {
    const size = clamp(params.collar.size, 0.2, 4) * (params.collar.style === 'band' ? 0.16 : 0.06);
    for (const cy of def.collars) {
      const r = radiusAt(params, cy) * (params.collar.style === 'band' ? 1.75 : 1.5);
      inner.rod([0, cy - size / 2, 0], [0, cy + size / 2, 0], r, metal, {
        sides: params.sides, mat: 'metal', caps: false,
      });
    }
  }
  if (params.cord > 0) {
    const r = radiusAt(params, params.height) * 1.5;
    inner.rod([r, params.height * 0.985, 0], [r, params.height * clamp(params.cord, 0.1, 0.98), 0], r * 0.28, cord, {
      sides: 4, mat: 'trim',
    });
  }
  const built = inner.build();
  if (params.tiltX !== 0 || params.tiltZ !== 0) {
    const tilted = new FlagBuilder(rand);
    tilted.bake(built, { rotX: params.tiltX, rotZ: params.tiltZ });
    return tilted.build();
  }
  return built;
}

export interface MountInput {
  kind: FlagpoleKind;
  mount: FlagMount;
  params: FlagpoleBuildDef['params'];
  fly: number;
  hoist: number;
}

export function mountsFor(kind: FlagpoleKind, orientation: FlagOrientation): readonly FlagMount[] {
  return FLAG_MOUNTS.filter((id) => {
    const def = FLAG_MOUNT_TABLE[id];
    return def.kinds.includes(kind) && def.orientation === orientation;
  });
}

export function defaultMountFor(kind: FlagpoleKind, orientation: FlagOrientation): FlagMount {
  if (orientation === 'hang') return 'armHang';
  return kind === 'horizontal' ? 'armFly' : 'masthead';
}

export function assertMountable(kind: FlagpoleKind, mount: FlagMount, params: FlagpoleBuildDef['params']): void {
  const def = FLAG_MOUNT_TABLE[mount];
  if (!def) throw new Error(`[Flags] 未知挂载方式: ${mount}（可用: ${FLAG_MOUNTS.join(', ')}）`);
  if (!def.kinds.includes(kind)) {
    throw new Error(`[Flags] ${kind} 型旗杆不支持挂载 "${mount}"（可用: ${FLAG_MOUNTS.filter((m) => FLAG_MOUNT_TABLE[m].kinds.includes(kind)).join(', ')}）`);
  }
  if (def.needsArm && !params.arm) {
    throw new Error(`[Flags] 挂载 "${mount}" 需要横臂，当前旗杆没有 arm 参数（改用 masthead / midmast，或给旗杆加横臂）`);
  }
  if (def.forbidsArm && params.arm) {
    throw new Error(`[Flags] 挂载 "${mount}" 要求杆上没有横臂：旗面会与横臂和斜撑相交（改用 armFly / armHang，或去掉该杆的 arm）`);
  }
}

export function resolveFlagMount(input: MountInput): FlagMountSolution {
  const { params, mount, hoist } = input;
  assertMountable(input.kind, mount, params);
  const arm = params.arm;
  const top = params.height - finialClearance(params);
  switch (mount) {
    case 'masthead': {
      const clearance = 0.05;
      const bottom = top - clearance - hoist;
      return {
        origin: [radiusAt(params, top) + GAP, bottom, 0],
        rotZ: 0, yaw: 0,
        collars: [bottom, bottom + hoist],
        clearance,
      };
    }
    case 'midmast': {
      const mastTop = params.height * 0.62;
      const bottom = mastTop - hoist;
      return {
        origin: [radiusAt(params, mastTop) + GAP, bottom, 0],
        rotZ: 0, yaw: 0,
        collars: [bottom, bottom + hoist],
        clearance: 0,
      };
    }
    case 'armHang': {
      const x0 = params.plate.style === 'none' ? 0 : params.plate.depth;
      const origin: V3 = [x0 + HANG_GAP, arm!.at + HANG_GAP, 0];
      return { origin, rotZ: -Math.PI / 2, yaw: 0, collars: [], clearance: HANG_GAP };
    }
    case 'armFly': {
      const x0 = params.plate.style === 'none' ? 0 : params.plate.depth;
      const tipX = x0 + arm!.length;
      const tipY = arm!.at + arm!.rise;
      return {
        origin: [tipX + GAP, tipY - hoist / 2, 0],
        rotZ: 0, yaw: 0, collars: [], clearance: 0,
      };
    }
    default:
      throw new Error(`[Flags] 未实现的挂载方式: ${mount}`);
  }
}

export function mountTable(): readonly FlagMountDef[] {
  return FLAG_MOUNTS.map((m) => FLAG_MOUNT_TABLE[m]);
}
