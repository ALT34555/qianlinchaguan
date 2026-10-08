/** 岩石形态原型（9 个岩石单位） */
import { LowPolyBuilder } from '../vegetation/geometry';
import { mulberry32 } from '../../../core/math/Random';
import {
  RockBuilder, clamp01, deepen, hexToRgb, lighten, mix,
  type RGB, type RockGeometry, type V3,
} from './geometry';
import type { RockBuildDef, RockColorSlot, RockMaterial, RockParams, RockPalette } from './types';

const TAU = Math.PI * 2;

/** 与植被同样的标量兜底 */
function scalar(value: number | undefined, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

/** 按语义槽取色 */
function slotColor(def: RockBuildDef, slot: RockColorSlot): RGB {
  const hex = def.palette[slot];
  if (!hex) return [127, 127, 127];
  return [
    parseInt(hex.slice(1, 3), 16),
    parseInt(hex.slice(3, 5), 16),
    parseInt(hex.slice(5, 7), 16),
  ];
}

/** 语义槽颜色解析 */
function tones(def: RockBuildDef): { body: RGB; band: RGB; cap: RGB; base: RGB } {
  return {
    body: slotColor(def, 'body'),
    band: slotColor(def, 'band'),
    cap: slotColor(def, 'cap'),
    base: slotColor(def, 'base'),
  };
}

/** 覆被方案 */
interface CrustPlan {
  /** 覆被颜色，无则为 null */
  color: RGB | null;
  /** 覆被材质槽 */
  mat: RockMaterial;
  /** 覆盖率 0~1（0 = 完全不覆） */
  amount: number;
  /** 只覆盖法线 y 大于该值的面 */
  slope: number;
  /** 覆被色混合比例 */
  blend: number;
}

function crustPlan(def: RockBuildDef): CrustPlan {
  const amount = clamp01(scalar(def.params.crust, def.crustMaterial ? 0.75 : 0.16));
  const slope = scalar(def.params.crustSlope, 0.42);
  // 覆被色十六进制解析
  const color = def.crustMaterial && def.crustRgb ? hexToRgb(def.crustRgb) : null;
  if (!color) return { color: null, mat: 'solid', amount: 0, slope, blend: 0 };
  // 苔藓压在本体上（混一点本体色才不会像贴纸）
  const blend = def.crustMaterial === 'moss' ? 0.2 : 0;
  return { color, mat: def.crustMaterial ?? 'moss', amount, slope, blend };
}

/** 展开覆被参数 */
function crustArgs(plan: CrustPlan): {
  crust?: { mat: RockMaterial; amount: number; slope: number };
  crustColor?: RGB;
} {
  if (!plan.color || plan.amount <= 0) return {};
  return { crust: { mat: plan.mat, amount: plan.amount, slope: plan.slope }, crustColor: plan.color };
}

/** 该形态档是否叠置第二块小石 */
function wantsStack(def: RockBuildDef): boolean {
  return def.form === 'stacked';
}

/** 原型的统一入口参数 */
interface Resolved {
  diameter: number;
  height: number;
  sides: number;
  jitter: number;
  rand: () => number;
}

function resolve(def: RockBuildDef): Resolved {
  const p = def.params;
  const diameter = scalar(p.diameter, 2);
  const aspect = scalar(p.aspect, 0.62);
  const height = scalar(p.height, diameter * aspect);
  const sizeScale = scalar(p.sizeScale, 1);
  const spreadScale = scalar(p.spreadScale, 1);
  return {
    diameter: diameter * sizeScale * spreadScale,
    height: height * sizeScale,
    sides: Math.max(3, Math.round(scalar(p.sides, 6))),
    jitter: clamp01(scalar(p.jitter, 0.18)),
    rand: mulberry32((def.buildSeed ^ def.shapeSeed) >>> 0),
  };
}

// 叠置档共用小石

/** 叠置档的小石 */
function stackCore(
  b: RockBuilder,
  def: RockBuildDef,
  t: { body: RGB; band: RGB; cap: RGB; base: RGB },
  plan: CrustPlan,
  rand: () => number,
): void {
  const p = def.params;
  const ratio = scalar(p.stackRatio, 0.42);
  const r = (scalar(p.diameter, 2) * scalar(p.sizeScale, 1) * scalar(p.spreadScale, 1) * ratio) / 2;
  const off = scalar(p.stackOffset, 0.3) * scalar(p.diameter, 2);
  const y = mainSpan(def);
  b.shell({
    center: [off, y - r * 0.15, off * 0.5],
    rx: r,
    rz: r * (0.8 + rand() * 0.35),
    thickness: r * 1.5,
    sides: Math.max(4, Math.round(scalar(p.sides, 6)) - 1),
    normal: [Math.sin(rand() * 0.5 - 0.25), 1, Math.sin(rand() * 0.5 - 0.25)],
    color: mix(t.body, t.band, 0.25),
    capColor: mix(t.cap, plan.color ?? t.cap, plan.blend),
    bottomColor: deepen(t.base, 0.5),
    jitter: scalar(p.jitter, 0.18) * 1.2,
    rotY: rand() * TAU,
    ...crustArgs(plan),
  });
}

/** 试算主石顶面高度 */
function mainSpan(def: RockBuildDef): number {
  try {
    const fn = ROCK_ARCHETYPES[def.archetype];
    if (!fn) return 0.5;
    const probe = new RockBuilder(mulberry32((def.buildSeed ^ def.shapeSeed ^ 0x9e3779b9) >>> 0));
    fn(probe, { ...def, form: 'outcrop' });
    return Math.max(probe.build().height, 0.05);
  } catch {
    return 0.5;
  }
}

// 1. 巨砾 boulder

/** 环半径抖动离散表 */
function ringRipple(sides: number, layers: number, rand: () => number): number[][] {
  const table: number[][] = [];
  for (let l = 0; l < layers; l++) {
    const row: number[] = [];
    for (let i = 0; i < sides; i++) row.push(0.78 + rand() * 0.38);
    table.push(row);
  }
  return table;
}

/** 环沿法线位移表 */
function ringWobble(sides: number, layers: number, rand: () => number): number[][] {
  const table: number[][] = [];
  for (let l = 0; l < layers; l++) {
    const row: number[] = [];
    for (let i = 0; i < sides; i++) row.push((rand() * 2 - 1) * 0.9);
    table.push(row);
  }
  return table;
}

/** 取某一层的表 */
function rowOf(table: number[][], layer: number): readonly number[] {
  return table[layer % table.length];
}

/** 巨砾 */
function boulder(b: RockBuilder, def: RockBuildDef): void {
  const { diameter, height, sides, jitter, rand } = resolve(def);
  const p = def.params;
  const t = tones(def);
  const plan = crustPlan(def);
  const shells = Math.max(1, Math.round(scalar(p.shells, 5)));
  const taper = scalar(p.shellTaper, 0.85);
  const skew = scalar(p.shellSkew, 0.11);
  const twist = scalar(p.shellTwist, 0.5);
  const flatten = scalar(p.baseFlatten, 0.95);
  const tiltX = scalar(p.tiltX, 0);
  const tiltZ = scalar(p.tiltZ, 0);
  const normal: V3 = [Math.sin(tiltZ), Math.cos(tiltZ) * Math.cos(tiltX), Math.sin(tiltX)];
  const sink = scalar(p.sink, 0.16) * height;
  const span = Math.max(height * flatten - sink, height * 0.12);
  const steps: number[] = [];
  for (let s = 0; s < shells; s++) steps.push(0.6 + rand() * 0.8);
  const stepSum = steps.reduce((a, b) => a + b, 0);
  const ripple = ringRipple(sides, shells + 2, rand);
  const wobble = ringWobble(sides, shells + 2, rand);
  const basePhase = rand() * TAU;
  let rx = diameter / 2;
  let rz = rx * (0.82 + rand() * 0.3);
  let y = -sink;
  for (let s = 0; s < shells; s++) {
    const k = Math.pow(taper, s);
    const h = (span * steps[s]) / stepSum;
    b.shell({
      center: [
        (rand() * 2 - 1) * skew * rx,
        y + h / 2,
        (rand() * 2 - 1) * skew * rz,
      ],
      rx: rx * k,
      rz: rz * k,
      thickness: h * 1.08,
      sides,
      normal,
      color: mix(t.body, t.band, Math.min(0.5, s * 0.08)),
      capColor: t.cap,
      bottomColor: deepen(t.base, 0.42),
      jitter,
      radiusScale: rowOf(ripple, s),
      wobble: rowOf(wobble, s),
      wobbleAmount: 0.5,
      rotY: basePhase + s * twist,
      ...crustArgs(plan),
    });
    y += h * 0.93;
    rx *= taper;
    rz *= taper;
  }
  if (wantsStack(def) && shells <= 3) stackCore(b, def, t, plan, rand);
}

// ---- 2. block 岩块 ----

/** 岩块 */
function block(b: RockBuilder, def: RockBuildDef): void {
  const { diameter, height, sides, jitter, rand } = resolve(def);
  const p = def.params;
  const t = tones(def);
  const plan = crustPlan(def);
  const shells = Math.max(1, Math.round(scalar(p.shells, 4)));
  const taper = scalar(p.shellTaper, 0.9);
  const skew = scalar(p.shellSkew, 0.12);
  const twist = scalar(p.shellTwist, 0.7);
  const tiltX = scalar(p.tiltX, 0.06);
  const tiltZ = scalar(p.tiltZ, -0.05);
  const normal: V3 = [Math.sin(tiltZ), Math.cos(tiltZ) * Math.cos(tiltX), Math.sin(tiltX)];
  const sink = scalar(p.sink, 0.1) * height;
  const layerH = height / (shells * 0.86);
  const ripple = ringRipple(sides, shells + 2, rand);
  const wobble = ringWobble(sides, shells + 2, rand);
  let y = -sink;
  let rx = diameter / 2;
  let rz = rx * (0.72 + rand() * 0.3);
  for (let s = 0; s < shells; s++) {
    const k = Math.pow(taper, s);
    b.shell({
      center: [
        (rand() * 2 - 1) * skew * rx,
        y + layerH / 2,
        (rand() * 2 - 1) * skew * rz,
      ],
      rx: rx * k,
      rz: rz * k,
      thickness: layerH * 1.02,
      sides,
      normal,
      color: s % 2 === 1 ? mix(t.body, t.band, 0.35) : t.body,
      capColor: t.cap,
      bottomColor: deepen(t.base, 0.45),
      jitter: jitter * 0.55,
      radiusScale: rowOf(ripple, s),
      wobble: rowOf(wobble, s),
      wobbleAmount: 0.45,
      rotY: s * twist + scalar(p.rotY, 0),
      ...crustArgs(plan),
    });
    y += layerH * 0.82;
    rx *= taper;
    rz *= taper;
  }
  // 一道外凸的棱：横嵌薄壳，打断"一整块"的单调
  b.shell({
    center: [0, height * 0.5, 0],
    rx: diameter * 0.44,
    rz: diameter * 0.3,
    thickness: height * 0.1,
    sides: Math.max(4, sides - 1),
    normal: [0.24, 0.96, -0.14],
    color: deepen(t.band, 0.1),
    capColor: t.cap,
    bottomColor: deepen(t.band, 0.4),
    jitter: jitter * 0.4,
    rotY: rand() * TAU,
    ...crustArgs(plan),
  });
  if (wantsStack(def)) stackCore(b, def, t, plan, rand);
}

// 3. 碎石堆 rubble

/** 碎石堆 */
function rubble(b: RockBuilder, def: RockBuildDef): void {
  const { diameter, height, sides, jitter, rand } = resolve(def);
  const p = def.params;
  const t = tones(def);
  const plan = crustPlan(def);
  const chunks = Math.max(2, Math.round(scalar(p.chunks, 6)));
  const spread = scalar(p.spread, 0.8) * diameter;
  for (let i = 0; i < chunks; i++) {
    // 第一块是主体：大且居中，其余是散落小块
    const main = i === 0;
    const k = main ? 0.52 : 0.2 + rand() * 0.26;
    const r = (diameter * k) / 2;
    const a = rand() * TAU;
    const d = main ? 0 : spread * (0.35 + rand() * 0.75);
    const h = r * (1.1 + rand() * 0.9);
    b.shell({
      center: [Math.cos(a) * d, -h * 0.34 + h * 0.5, Math.sin(a) * d],
      rx: r,
      rz: r * (0.7 + rand() * 0.6),
      thickness: h * 0.82,
      sides: Math.max(4, sides - (main ? 1 : 2)),
      normal: [(rand() * 2 - 1) * 0.36, 1, (rand() * 2 - 1) * 0.36],
      color: mix(t.body, t.band, rand() * 0.45),
      capColor: mix(t.cap, t.body, rand() * 0.3),
      bottomColor: deepen(t.base, 0.5),
      jitter: jitter * 1.3,
      rotY: rand() * TAU,
      ...crustArgs(plan),
    });
  }
  // 贴地的一层散屑
  const litter = Math.max(1, Math.round(chunks / 3));
  for (let i = 0; i < litter; i++) {
    const a = rand() * TAU;
    const d = spread * (0.6 + rand() * 0.6);
    b.shell({
      center: [Math.cos(a) * d, height * 0.06, Math.sin(a) * d],
      rx: diameter * (0.08 + rand() * 0.1),
      rz: diameter * (0.06 + rand() * 0.1),
      thickness: height * 0.16,
      sides: 4,
      normal: [(rand() * 2 - 1) * 0.5, 1, (rand() * 2 - 1) * 0.5],
      color: deepen(t.base, 0.12),
      capColor: mix(t.cap, t.base, 0.4),
      bottomColor: deepen(t.base, 0.55),
      jitter,
      rotY: rand() * TAU,
    });
  }
  if (wantsStack(def)) stackCore(b, def, t, plan, rand);
}

// ---- 4. slab 板岩席 ----

/** 板岩席 */
function slab(b: RockBuilder, def: RockBuildDef): void {
  const { diameter, height, sides, jitter, rand } = resolve(def);
  const p = def.params;
  const t = tones(def);
  const plan = crustPlan(def);
  const plates = Math.max(1, Math.round(scalar(p.plates, 3)));
  const thickness = scalar(p.plateThickness, 0.12) * diameter;
  const skew = scalar(p.plateSkew, 0.14) * diameter;
  const tilt = scalar(p.plateTilt, 0.14);
  // 每块板自己的边缘起伏：页理被敲开的凹凸感
  const wobble = ringWobble(sides, plates + 1, rand);
  let y = -height * 0.08;
  for (let i = 0; i < plates; i++) {
    const k = 1 - (i / Math.max(1, plates)) * 0.32;
    b.slab({
      center: [
        (rand() * 2 - 1) * skew,
        y + thickness / 2,
        (rand() * 2 - 1) * skew * 0.7,
      ],
      rx: (diameter / 2) * k,
      rz: (diameter / 2) * k * (0.66 + rand() * 0.4),
      thickness: thickness * (0.8 + rand() * 0.5),
      sides,
      tilt: tilt * (0.6 + rand() * 0.9),
      bearing: rand() * TAU,
      color: i % 2 === 1 ? mix(t.body, t.band, 0.4) : t.body,
      jitter: jitter * 0.8,
      wobble: rowOf(wobble, i),
      wobbleAmount: 0.55,
      rotY: rand() * TAU,
      ...crustArgs(plan),
    });
    y += thickness * (0.86 + rand() * 0.3);
  }
  if (wantsStack(def)) stackCore(b, def, t, plan, rand);
}

// ---- 5. ledge 石台 ----

/** 石台 */
function ledge(b: RockBuilder, def: RockBuildDef): void {
  const { diameter, height, sides, jitter, rand } = resolve(def);
  const p = def.params;
  const t = tones(def);
  const plan = crustPlan(def);
  const tiltX = scalar(p.tiltX, 0.05 + (rand() * 2 - 1) * 0.05);
  const tiltZ = scalar(p.tiltZ, (rand() * 2 - 1) * 0.05);
  const normal: V3 = [Math.sin(tiltZ), Math.cos(tiltZ) * Math.cos(tiltX), Math.sin(tiltX)];
  // 主台底边下沉贴地
  const sink = scalar(p.sink, 0.34) * height;
  const mainTop = scalar(p.baseFlatten, 0.86) * height;
  const mainThick = mainTop + sink;
  // 主台：宽、低、顶面明显
  b.shell({
    center: [0, mainTop - mainThick / 2, 0],
    rx: diameter / 2,
    rz: (diameter / 2) * (0.7 + rand() * 0.35),
    thickness: mainThick,
    sides: Math.max(5, sides),
    normal,
    color: t.body,
    capColor: mix(t.cap, t.body, 0.25),
    bottomColor: deepen(t.base, 0.45),
    jitter: jitter * 0.6,
    radiusScale: ringRipple(sides, 2, rand)[0],
    rotY: rand() * TAU,
    ...crustArgs(plan),
  });
  // 一级台阶
  const stepH = height * 0.4;
  const stepTop = mainTop + stepH * 0.92;
  b.shell({
    center: [
      (rand() * 2 - 1) * diameter * 0.16,
      stepTop - stepH / 2,
      (rand() * 2 - 1) * diameter * 0.12,
    ],
    rx: diameter * 0.3,
    rz: diameter * 0.24 * (0.8 + rand() * 0.4),
    thickness: stepH,
    sides: Math.max(4, sides - 1),
    normal: [Math.sin(tiltZ * 0.6), Math.cos(tiltZ * 0.6) * Math.cos(tiltX * 0.6), Math.sin(tiltX * 0.6)],
    color: mix(t.body, t.band, 0.2),
    capColor: t.cap,
    bottomColor: deepen(t.base, 0.5),
    jitter: jitter * 0.7,
    radiusScale: ringRipple(sides, 2, rand)[1],
    rotY: rand() * TAU,
    ...crustArgs(plan),
  });
  // 裂纹抬高微距避免共面
  const capY = mainTop + height * 0.015;
  for (let i = 0; i < 2; i++) {
    b.shell({
      center: [
        (rand() * 2 - 1) * diameter * 0.2,
        capY,
        (rand() * 2 - 1) * diameter * 0.2,
      ],
      rx: diameter * (0.3 + rand() * 0.2),
      rz: diameter * (0.05 + rand() * 0.04),
      thickness: height * 0.1,
      sides: 4,
      normal: [0, 1, 0],
      color: deepen(t.band, 0.28),
      capColor: deepen(t.band, 0.2),
      bottomColor: deepen(t.band, 0.5),
      jitter: jitter * 0.5,
      rotY: rand() * TAU,
    });
  }
  if (wantsStack(def)) stackCore(b, def, t, plan, rand);
}

// ---- 6. shard 碎岩 ----

/** 碎岩 */
function shard(b: RockBuilder, def: RockBuildDef): void {
  const { diameter, height, sides, jitter, rand } = resolve(def);
  const p = def.params;
  const t = tones(def);
  const plan = crustPlan(def);
  // 底板
  b.shell({
    center: [0, -height * 0.1, 0],
    rx: diameter / 2,
    rz: (diameter / 2) * (0.72 + rand() * 0.3),
    thickness: height * 0.3,
    sides: Math.max(5, sides),
    normal: [0, 1, 0],
    color: t.body,
    capColor: mix(t.cap, t.body, 0.35),
    bottomColor: deepen(t.base, 0.45),
    jitter: jitter * 0.7,
    rotY: rand() * TAU,
    ...crustArgs(plan),
  });
  // 翘起的碎片：绕各自方位向外翻
  const plates = Math.max(2, Math.round(scalar(p.plates, 3)));
  for (let i = 0; i < plates; i++) {
    const bearing = (i / plates) * TAU + rand() * 0.8;
    const tilt = 0.55 + rand() * 0.55;
    const len = (diameter / 2) * (0.5 + rand() * 0.42);
    const h = height * (0.9 + rand() * 0.7);
    b.shell({
      center: [
        Math.cos(bearing) * len * 0.45,
        height * (0.16 + rand() * 0.2),
        Math.sin(bearing) * len * 0.45,
      ],
      rx: len,
      rz: len * (0.34 + rand() * 0.24),
      thickness: h * 0.42,
      sides: 4 + Math.round(rand()),
      // 法线朝外侧上方翻出去，这就是碎片的翘角
      normal: [
        Math.sin(tilt) * Math.cos(bearing),
        Math.cos(tilt),
        Math.sin(tilt) * Math.sin(bearing),
      ],
      color: mix(t.body, t.band, 0.18 + rand() * 0.3),
      capColor: t.cap,
      bottomColor: deepen(t.base, 0.5),
      jitter: jitter * 0.8,
      rotY: bearing + Math.PI / 2,
      ...crustArgs(plan),
    });
  }
  if (wantsStack(def)) stackCore(b, def, t, plan, rand);
}

// ---- 7. spire 尖石 ----

/** 尖石 */
function spire(b: RockBuilder, def: RockBuildDef): void {
  const { diameter, height, sides, jitter, rand } = resolve(def);
  const p = def.params;
  const t = tones(def);
  const plan = crustPlan(def);
  const sink = scalar(p.sink, 0.06) * height;
  // 基座：免得尖石像插进去的
  b.shell({
    center: [0, -sink + height * 0.1, 0],
    rx: (diameter / 2) * (0.8 + rand() * 0.2),
    rz: (diameter / 2) * (0.66 + rand() * 0.24),
    thickness: height * 0.22,
    sides: Math.max(5, sides),
    normal: [0, 1, 0],
    color: t.body,
    capColor: mix(t.cap, t.body, 0.3),
    bottomColor: deepen(t.base, 0.45),
    jitter: jitter * 0.7,
    rotY: rand() * TAU,
    ...crustArgs(plan),
  });
  b.column({
    base: [0, -sink + height * 0.18, 0],
    radius: (diameter / 2) * scalar(p.baseRatio, 0.55),
    height: height * 0.86,
    segments: Math.max(1, Math.round(scalar(p.segments, 3))),
    tipRatio: scalar(p.tipRatio, 0),
    sides: Math.max(4, sides - 1),
    color: t.body,
    skew: scalar(p.nodeSkew, 0.16),
    twist: scalar(p.nodeTwist, 0.4),
    tipColor: mix(t.cap, lighten(t.body, 0.12), 0.5),
    jitter: jitter * 1.2,
    ...crustArgs(plan),
  });
  // 侧面贴合的小块：让尖石根部有"崩下来的"体量
  const chips = Math.max(1, Math.round(scalar(p.chunks, 2)));
  for (let i = 0; i < chips; i++) {
    const a = rand() * TAU;
    const d = (diameter / 2) * (0.5 + rand() * 0.4);
    b.shell({
      center: [Math.cos(a) * d, height * (0.06 + rand() * 0.16), Math.sin(a) * d],
      rx: diameter * (0.12 + rand() * 0.1),
      rz: diameter * (0.1 + rand() * 0.08),
      thickness: height * 0.2,
      sides: 4 + Math.round(rand()),
      normal: [Math.cos(a) * 0.4, 1, Math.sin(a) * 0.4],
      color: mix(t.body, t.band, 0.3),
      capColor: mix(t.cap, t.body, 0.3),
      bottomColor: deepen(t.base, 0.5),
      jitter,
      rotY: rand() * TAU,
      ...crustArgs(plan),
    });
  }
  if (wantsStack(def)) stackCore(b, def, t, plan, rand);
}

// ---- 8. stub 石墩 ----

/** 石墩 */
function stub(b: RockBuilder, def: RockBuildDef): void {
  const { diameter, height, sides, jitter, rand } = resolve(def);
  const p = def.params;
  const t = tones(def);
  const plan = crustPlan(def);
  const sink = scalar(p.sink, 0.14) * height;
  // 底裙遮盖地面接缝
  b.shell({
    center: [0, -sink + height * 0.14, 0],
    rx: (diameter / 2) * (0.9 + rand() * 0.16),
    rz: (diameter / 2) * (0.78 + rand() * 0.2),
    thickness: height * 0.3,
    sides: Math.max(5, sides),
    normal: [0, 1, 0],
    color: mix(t.body, t.base, 0.22),
    capColor: mix(t.cap, t.body, 0.3),
    bottomColor: deepen(t.base, 0.5),
    jitter: jitter * 0.8,
    rotY: rand() * TAU,
    ...crustArgs(plan),
  });
  b.column({
    base: [0, -sink + height * 0.24, 0],
    radius: (diameter / 2) * scalar(p.baseRatio, 0.72),
    height: height * 0.72,
    segments: Math.max(1, Math.round(scalar(p.segments, 2))),
    tipRatio: scalar(p.tipRatio, 0.72),
    sides: Math.max(4, sides - 1),
    color: t.body,
    skew: scalar(p.nodeSkew, 0.1),
    twist: scalar(p.nodeTwist, 0.26),
    tipColor: mix(t.cap, lighten(t.body, 0.06), 0.6),
    jitter: jitter * 0.9,
    ...crustArgs(plan),
  });
  if (wantsStack(def)) stackCore(b, def, t, plan, rand);
}

// ---- 9. floe 冰岩 ----

/** 冰岩 */
function floe(b: RockBuilder, def: RockBuildDef): void {
  const { diameter, height, sides, jitter, rand } = resolve(def);
  const p = def.params;
  const t = tones(def);
  const plan = crustPlan(def);
  const sink = scalar(p.sink, 0.22) * height;
  const shells = Math.max(2, Math.round(scalar(p.shells, 3)));
  const taper = scalar(p.shellTaper, 0.84);
  const span = Math.max(height - sink, height * 0.15);
  const steps: number[] = [];
  for (let s = 0; s < shells; s++) steps.push(0.7 + rand() * 0.6);
  const stepSum = steps.reduce((a, b) => a + b, 0);
  const ripple = ringRipple(Math.max(5, sides), shells + 2, rand);
  const wobble = ringWobble(Math.max(5, sides), shells + 2, rand);
  let rx = diameter / 2;
  let rz = rx * (0.8 + rand() * 0.3);
  let y = -sink;
  for (let s = 0; s < shells; s++) {
    const k = Math.pow(taper, s);
    const h = (span * steps[s]) / stepSum;
    b.shell({
      center: [
        (rand() * 2 - 1) * 0.09 * rx,
        y + h / 2,
        (rand() * 2 - 1) * 0.09 * rz,
      ],
      rx: rx * k,
      rz: rz * k,
      thickness: h * 1.04,
      sides: Math.max(5, sides),
      normal: [0, 1, 0],
      // 上层冰比下层亮（受光更足、更干净）
      color: mix(deepen(t.body, 0.18), t.body, s / Math.max(1, shells - 1)),
      capColor: t.cap,
      bottomColor: deepen(t.base, 0.4),
      jitter: jitter * 0.6,
      radiusScale: rowOf(ripple, s),
      wobble: rowOf(wobble, s),
      wobbleAmount: 0.45,
      rotY: rand() * TAU,
      ...crustArgs(plan),
    });
    y += h * 0.95;
    rx *= taper;
    rz *= taper;
  }
  // 顶盖薄冰壳断裂表现
  const plates = Math.max(1, Math.round(scalar(p.plates, 2)));
  const r = diameter / 2;
  for (let i = 0; i < plates; i++) {
    const bearing = rand() * TAU;
    const tilt = 0.12 + rand() * 0.3;
    b.shell({
      center: [
        (rand() * 2 - 1) * r * 0.32,
        height * (0.46 + rand() * 0.16),
        (rand() * 2 - 1) * r * 0.32,
      ],
      rx: r * (0.42 + rand() * 0.3),
      rz: r * (0.3 + rand() * 0.26),
      thickness: height * (0.12 + rand() * 0.1),
      sides: 4 + Math.round(rand() * 2),
      normal: [Math.sin(tilt) * Math.cos(bearing), Math.cos(tilt), Math.sin(tilt) * Math.sin(bearing)],
      color: t.body,
      capColor: t.cap,
      bottomColor: deepen(t.base, 0.35),
      jitter: jitter * 0.4,
      rotY: rand() * TAU,
      mat: 'ice',
    });
  }
  if (wantsStack(def)) stackCore(b, def, t, plan, rand);
}

// ---- 注册表 ----

/** 原型注册表 */
export const ROCK_ARCHETYPES: Record<string, (b: RockBuilder, def: RockBuildDef) => void> = {
  boulder,
  block,
  rubble,
  slab,
  ledge,
  shard,
  spire,
  stub,
  floe,
};

export type RockArchetypeName = keyof typeof ROCK_ARCHETYPES;

/** 原型默认几何参数 */
export const ROCK_ARCHETYPE_DEFAULTS: Record<string, RockParams> = {
  boulder: { diameter: 2.1, aspect: 0.78, sides: 7, shells: 5, shellTaper: 0.87, shellSkew: 0.12, shellTwist: 0.5, baseFlatten: 0.95, jitter: 0.22, sink: 0.14 },
  block: { diameter: 1.9, aspect: 0.9, sides: 6, shells: 4, shellTaper: 0.91, shellSkew: 0.13, shellTwist: 0.7, jitter: 0.15, sink: 0.1 },
  rubble: { diameter: 1.5, aspect: 0.56, sides: 5, chunks: 6, spread: 0.85, jitter: 0.26, sink: 0.02 },
  slab: { diameter: 2.2, aspect: 0.4, sides: 6, plates: 4, plateThickness: 0.1, plateSkew: 0.16, plateTilt: 0.16, jitter: 0.17, sink: 0.08 },
  ledge: { diameter: 2.9, aspect: 0.52, sides: 7, jitter: 0.17, sink: 0.2 },
  shard: { diameter: 1.9, aspect: 0.6, sides: 5, plates: 3, jitter: 0.23, sink: 0.1 },
  spire: { diameter: 1.05, aspect: 2.2, sides: 5, segments: 3, tipRatio: 0, baseRatio: 0.6, nodeSkew: 0.16, nodeTwist: 0.4, jitter: 0.22, sink: 0.06 },
  stub: { diameter: 1.5, aspect: 0.85, sides: 6, segments: 2, tipRatio: 0.72, baseRatio: 0.74, nodeSkew: 0.1, nodeTwist: 0.26, jitter: 0.18, sink: 0.16 },
  floe: { diameter: 2.3, aspect: 0.5, sides: 7, shells: 3, shellTaper: 0.86, plates: 2, jitter: 0.16, sink: 0.22 },
};

/** 岩性默认色板映射 */
export const FAMILY_PALETTE: Record<string, Required<RockPalette>> = {
  igneous: { body: 'granite', band: 'graniteDark', cap: 'granitePale', base: 'soil', crust: 'lichen' },
  sedimentary: { body: 'sandstone', band: 'sandstoneDark', cap: 'sandstonePale', base: 'soil', crust: 'dryGrass' },
  metamorphic: { body: 'slate', band: 'slateDark', cap: 'slatePale', base: 'soil', crust: 'moss' },
  clastic: { body: 'gravel', band: 'gravelDark', cap: 'scree', base: 'soil', crust: 'dryGrass' },
  ice: { body: 'ice', band: 'iceDark', cap: 'snow', base: 'iceDark', crust: 'snowCap' },
};

/** 按原型生成几何（唯一的几何入口） */
export function buildRock(def: RockBuildDef): RockGeometry {
  const fn = ROCK_ARCHETYPES[def.archetype];
  if (!fn) throw new Error(`[Rocks] 未知岩石原型: ${def.archetype}（可用: ${Object.keys(ROCK_ARCHETYPES).join(', ')}）`);
  const b = new RockBuilder(mulberry32((def.buildSeed ^ def.shapeSeed) >>> 0));
  fn(b, def);
  return b.build();
}

/** 复用原语混合几何 */
export function bakeCone(
  b: RockBuilder,
  base: V3,
  radius: number,
  height: number,
  sides: number,
  color: RGB,
  mat: RockMaterial = 'solid',
): void {
  const tmp = new LowPolyBuilder();
  tmp.cone(base, radius, radius, height, sides, color, { radiusRatio: 0, cap: true });
  b.bake(tmp.build(), { mat });
}
