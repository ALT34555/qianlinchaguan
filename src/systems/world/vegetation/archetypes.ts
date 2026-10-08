/** 植物原型库 */
import { mulberry32 } from '../../../core/math/Random';
import { LowPolyBuilder, parseColor, type PlantGeometry } from './geometry';
import {
  DEFAULT_BLOOM_COLOR, type ClimateZone, type PaletteKey, type PlantDef, type PlantMaterial,
  type PlantParams, type SnowTarget,
} from './types';

type RGB = [number, number, number];
type V3 = [number, number, number];

const TAU = Math.PI * 2;
const GOLDEN = Math.PI * (3 - Math.sqrt(5));

function scalar(value: number | undefined, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

function clamp(value: number, lo: number, hi: number): number {
  return value < lo ? lo : value > hi ? hi : value;
}

/** 枯叶键集合 */
const WITHEBLE_KEYS: ReadonlySet<PaletteKey> = new Set<PaletteKey>([
  'foliage', 'foliageDark', 'foliageDeep', 'canopy', 'conifer', 'coniferDark', 'coniferDeep',
  'larch', 'jungle', 'jungleDark', 'jungleDeep', 'palmFrond', 'bamboo', 'bambooCulm',
  'tea', 'mulberry', 'cropGreen', 'riceGreen', 'cropGold', 'sorghum',
]);

/** 叶色键 -> 干枯色键 */
function witherKey(key: PaletteKey): PaletteKey {
  switch (key) {
    case 'foliageDark':
    case 'foliageDeep':
    case 'coniferDark':
    case 'coniferDeep':
    case 'jungleDark':
    case 'jungleDeep':
      return 'witheredDark';
    case 'conifer':
    case 'larch':
      return 'witheredNeedle';
    case 'cropGold':
    case 'sorghum':
      return 'withered';
    default:
      return 'withered';
  }
}

/** 取色调（已按季节解析） */
function tone(def: PlantDef, key: PaletteKey | undefined, scale = 1): RGB {
  let base: string | undefined;
  if (key === 'bloom') base = def.bloomColor ?? def.palette.bloom ?? DEFAULT_BLOOM_COLOR;
  else if (key && def.state === 'withered' && WITHEBLE_KEYS.has(key)) {
    base = def.palette[witherKey(key)] ?? def.palette[key];
  } else base = key ? def.palette[key] : undefined;
  const rgb = parseColor(base ?? '#ff00ff');
  return [clampByte(rgb[0] * scale), clampByte(rgb[1] * scale), clampByte(rgb[2] * scale)];
}

function clampByte(v: number): number {
  return v < 0 ? 0 : v > 255 ? 255 : v;
}

/** 按半径向心收缩 */
function deepen(c: RGB, depth: number): RGB {
  if (depth <= 0.01) return c;
  const outer = 1;
  const inner = 0.52;
  const k = outer + (inner - outer) * clamp(depth, 0, 1);
  return [clampByte(c[0] * k), clampByte(c[1] * k), clampByte(c[2] * k)];
}

function hasSnow(def: PlantDef): boolean {
  return def.snow && scalar(def.params.snow, 0) > 0.01;
}

function snowTargets(def: PlantDef): readonly SnowTarget[] {
  return def.params.snowOn ?? ['canopy'];
}

function wantsSnowOn(def: PlantDef, target: SnowTarget): boolean {
  return hasSnow(def) && snowTargets(def).includes(target);
}

function snowColor(def: PlantDef): RGB {
  return tone(def, 'snow');
}

/** 花朵数量 */
function bloomCountFor(def: PlantDef, requested: number, fallback: number): number {
  if (!def.bloom) return 0;
  return Math.max(requested, fallback);
}

/** 果实数量 */
function fruitCountFor(def: PlantDef, requested: number, fallback = 0): number {
  if (!def.fruit) return 0;
  const base = requested > 0 ? requested : fallback;
  if (base <= 0) return 0;
  const scale = scalar(def.params.fruitScale, 1);
  return Math.max(1, Math.round(base * (Number.isFinite(scale) && scale > 0 ? scale : 1)));
}

/** 在叶团上方铺一层薄雪盖 */
function snowCap(b: LowPolyBuilder, def: PlantDef, center: V3, r: number, yScale = 1): void {
  const thickness = scalar(def.params.snow, 0.18);
  const col = snowColor(def);
  b.dome([center[0], center[1] + r * 0.42 + thickness * 0.35, center[2]], r * 0.86, r * 0.5 * yScale, r * 0.86, 6, col, {
    floor: false,
    jitter: 0.1,
    rand: mulberry32(def.buildSeed ^ 0x51ed270b),
    mat: 'snow',
  });
}

/** 花与叶的公共积木 */
/** 默认花瓣数 */
const DEFAULT_PETALS = 5;

/** 花瓣数 */
function petalCountOf(def: PlantDef): number {
  return Math.round(clamp(scalar(def.params.petals, DEFAULT_PETALS), 2, 16));
}

/** 单瓣宽度比 */
function petalWidthRatio(petals: number): number {
  return clamp(1.0 - petals * 0.055, 0.24, 0.6);
}

/** 单朵花半径 */
function flowerRadiusFor(def: PlantDef, fallback: number): number {
  const explicit = def.params.bloomRadius;
  if (typeof explicit === 'number' && Number.isFinite(explicit) && explicit > 0) return clamp(explicit, 0.03, 0.6);
  return clamp(fallback, 0.04, 0.4);
}

/** 一朵花 */
function flowerAt(b: LowPolyBuilder, def: PlantDef, center: V3, radius: number, rand: () => number): void {
  const petals = petalCountOf(def);
  const col = tone(def, 'bloom');
  const core = tone(def, 'pollen');
  // 宽短单瓣搭接形成花团
  const w = radius * 2.5 * petalWidthRatio(petals);
  const phase = rand() * TAU;
  for (let i = 0; i < petals; i++) {
    const a = phase + (i / petals) * TAU;
    const len = radius * (0.8 + rand() * 0.25);
    // 花瓣上扬 17°~34° 并兜成浅碗
    b.petal(
      [center[0], center[1] + radius * 0.14, center[2]],
      a,
      len,
      w * (0.85 + rand() * 0.3),
      deepen(col, i % 2 === 0 ? 0.04 + rand() * 0.1 : 0.26 + rand() * 0.16),
      { pitch: 0.3 + rand() * 0.3, cup: 0.32, mat: 'bloom', rand },
    );
  }
  // 花心
  b.dome([center[0], center[1] + radius * 0.3, center[2]], radius * 0.42, radius * 0.5, radius * 0.42, 4, core,
    { jitter: 0.2, rand, mat: 'bloom', floor: false });
}

/** 花簇 */
function bloomCluster(
  b: LowPolyBuilder,
  def: PlantDef,
  center: V3,
  r: number,
  count: number,
  rand: () => number,
  opts: { size?: number; ySpan?: number } = {},
): void {
  const fr = flowerRadiusFor(def, opts.size ?? r * 0.09);
  const ySpan = opts.ySpan ?? r * 0.55;
  for (let i = 0; i < count; i++) {
    const t = (i + 0.5) / count;
    const a = i * GOLDEN + def.buildSeed * 0.0009 + rand() * 0.5;
    // 径向 0.82~1.12 倍
    const rad = r * (0.82 + 0.3 * rand()) * (0.92 + 0.16 * Math.sqrt(t));
    const y = center[1] + (rand() * 2 - 1) * ySpan;
    flowerAt(b, def, [center[0] + Math.cos(a) * rad, y, center[2] + Math.sin(a) * rad], fr, rand);
  }
}

/** 叶簇（**绿色花瓣**） */
function leafSpray(
  b: LowPolyBuilder,
  def: PlantDef,
  center: V3,
  r: number,
  count: number,
  rand: () => number,
  opts: { width?: number; pitch?: number; spreadY?: number; dry?: boolean; len?: number } = {},
): void {
  if (count <= 0) return;
  const fol = tone(def, 'foliage');
  const dark = tone(def, 'foliageDark', 1);
  const dry = tone(def, 'witheredStem');
  for (let i = 0; i < count; i++) {
    const t = (i + 0.5) / count;
    const a = i * GOLDEN + def.buildSeed * 0.0011 + rand() * 0.6;
    const rad = r * (0.5 + 0.5 * Math.sqrt(t)) * (0.85 + rand() * 0.3);
    const y = center[1] + r * (opts.spreadY ?? 0.22) * (1 - t) + (rand() - 0.5) * r * 0.55;
    const len = r * (opts.len ?? 0.5) * (0.8 + rand() * 0.45);
    b.petal(
      [center[0] + Math.cos(a) * rad, y, center[2] + Math.sin(a) * rad],
      a + (rand() - 0.5) * 0.5,
      len,
      len * (opts.width ?? 0.42),
      opts.dry ? dry : (i % 2 ? fol : dark),
      { pitch: opts.pitch ?? 0.1 + rand() * 0.3, cup: 0.4, rand },
    );
  }
}

/** 会弯的窄叶 */
function bentLeaf(
  b: LowPolyBuilder,
  start: V3,
  bearing: number,
  len: number,
  wid: number,
  rise: number,
  drop: number,
  col: RGB,
  rand: () => number,
  mat: PlantMaterial = 'solid',
): void {
  const dx = Math.cos(bearing), dz = Math.sin(bearing);
  const mid: V3 = [start[0] + dx * len * 0.5, start[1] + rise, start[2] + dz * len * 0.5];
  b.petal(start, bearing, len * 0.56, wid, col, { pitch: Math.atan2(rise, len * 0.5) + 0.14, cup: 0.34, mat, rand });
  b.petal(mid, bearing, len * 0.56, wid * 0.72, col, { pitch: Math.atan2(rise - drop, len * 0.5) - 0.04, cup: 0.3, mat, rand });
}

/** 果实 */
function fruitCluster(
  b: LowPolyBuilder,
  def: PlantDef,
  center: V3,
  r: number,
  count: number,
  rand: () => number,
  opts: { size?: number } = {},
): void {
  const col = tone(def, 'fruit');
  const explicit = def.params.fruitRadius;
  const fr = typeof explicit === 'number' && explicit > 0
    ? clamp(explicit, 0.02, 0.5)
    : clamp(opts.size ?? r * 0.08, 0.035, 0.24);
  for (let i = 0; i < count; i++) {
    const t = (i + 0.5) / count;
    const a = i * GOLDEN + def.buildSeed * 0.0013 + rand() * 0.5;
    const rad = r * (0.8 + 0.35 * rand()) * (0.9 + 0.2 * Math.sqrt(t));
    const y = center[1] - (0.15 + rand() * 0.6) * r * 0.5;
    b.ico([center[0] + Math.cos(a) * rad, y, center[2] + Math.sin(a) * rad], fr, 5, col,
      { jitter: 0.15, rand, mat: 'fruit', rings: 1 });
  }
}

/** 单片花瓣的独立几何 */
export interface PetalShapeSpec {
  /** 花瓣颜色十六进制，默认继承。 */
  color?: string;
  /** 花瓣长度（格），默认 0.16 */
  length?: number;
  /** 花瓣宽度，默认长度0.55倍。 */
  width?: number;
  /** 上扬角（弧度），默认 0.35 */
  pitch?: number;
  /** 兜状上凸量（相对宽度），默认 0.4 */
  cup?: number;
  /** 是否自带背面，默认 true */
  doubleSided?: boolean;
}

export function buildPetalGeometry(spec: PetalShapeSpec = {}): PlantGeometry {
  const len = scalar(spec.length, 0.16);
  const b = new LowPolyBuilder();
  b.petal([0, 0, 0], 0, len, scalar(spec.width, len * 0.55), parseColor(spec.color ?? DEFAULT_BLOOM_COLOR), {
    pitch: scalar(spec.pitch, 0.35),
    cup: scalar(spec.cup, 0.4),
    both: spec.doubleSided !== false,
  });
  return b.build();
}

/** 枝干：从根部向上的一段细枝，末端收细 */
function limbTo(b: LowPolyBuilder, start: V3, dir: V3, length: number, radius: number, col: RGB): V3 {
  const len = Math.hypot(dir[0], dir[1], dir[2]) || 1;
  const d: V3 = [dir[0] / len, dir[1] / len, dir[2] / len];
  b.limb(start, d, length, radius, col, { sides: 3, tipRatio: 0.42 });
  return [start[0] + d[0] * length, start[1] + d[1] * length, start[2] + d[2] * length];
}

// 
// 针叶树：伞盖层叠与尖锐树顶
function conifer(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 12);
  const trunk = scalar(p.trunk, height * 0.24);
  const bark = tone(def, 'bark');
  const trunkR = scalar(p.trunkRadius, Math.max(0.16, height * 0.035));
  const layers = Math.max(2, Math.round(scalar(p.layers, 5)));
  const fol = tone(def, 'foliage');
  const dark = tone(def, 'foliageDark', 1);
  const canopy = scalar(p.canopy, height * 0.26);
  const baseRadius = scalar(p.layerRadius, canopy);
  const span = height - trunk;
  const step = span / (layers + 1) / Math.max(0.2, scalar(p.layerStep, 0.82));
  const sides = Math.round(clamp(scalar(p.sides, 5), 3, 12));
  const jitter = scalar(p.jitter, 0.14);
  const lean = scalar(p.lean, 0);

  b.prism([0, 0, 0], trunkR * 1.25, trunkR * 1.25, 0, trunk, sides, bark, { jitter: jitter * 0.4, rand });
  const topY = trunk + span;
  b.prism([lean * span * 0.4, trunk, lean * span * 0.2], trunkR * 0.8, trunkR * 0.8, 0, span * 0.7, sides, bark, { rand });
  // 根盘
  b.cone([0, 0, 0], trunkR * 1.9, trunkR * 1.9, trunk * 0.12, sides, bark, { rotY: 0.4, jitter: jitter * 0.5, rand, cap: true });

  for (let i = 1; i <= layers; i++) {
    const t = i / layers;
    const y = trunk + step * i * 0.86;
    const r = baseRadius * Math.pow(1 - t * 0.82, 0.9) + 0.02;
    const lh = Math.max(step * 2.1, span / layers);
    const c = deepen(i % 2 === 0 ? fol : dark, (1 - t) * 0.45);
    b.cone([lean * y * 0.5, y, 0], r, r, lh, sides, c, {
      rotY: i * 0.7,
      jitter,
      rand,
      cap: true,
      radiusRatio: i === layers ? 0.02 : 0.06,
    });
    if (wantsSnowOn(def, 'canopy') && i >= layers - Math.ceil(layers * 0.5)) {
      snowCap(b, def, [lean * y * 0.5, y + lh * 0.34, 0], r * 1.02, 1.1);
    }
  }
  // 树顶
  const spikeCount = Math.max(1, Math.round(scalar(p.spikeCount, 1)));
  for (let i = 0; i < spikeCount; i++) {
    const a = (i / spikeCount) * TAU + rand();
    const r = baseRadius * 0.08;
    b.cone([Math.cos(a) * r, topY - span * 0.06, Math.sin(a) * r], baseRadius * 0.11, baseRadius * 0.11, span * 0.16, 3, fol, {
      rotY: a,
      jitter: 0.1,
      rand,
    });
  }
  if (wantsSnowOn(def, 'top')) snowCap(b, def, [0, topY - span * 0.04, 0], baseRadius * 0.5, 1.2);

  // 散粉期冠表散布花粉粒子
  const pollenCount = bloomCountFor(def, 0, 20 + Math.round(layers * 8));
  if (pollenCount > 0) pollenGrains(b, def, pollenCount, baseRadius, trunk, topY, rand);
}

/** 花粉颗粒 */
function pollenGrains(b: LowPolyBuilder, def: PlantDef, count: number, baseRadius: number, bottom: number, top: number, rand: () => number): void {
  const col = tone(def, 'pollen');
  const span = Math.max(0.4, top - bottom);
  const grain = clamp(baseRadius * 0.035, 0.05, 0.09);
  for (let i = 0; i < count; i++) {
    const t = Math.pow(rand(), 0.65); // 偏上半部：顶芽与上部枝叶散粉最多
    const y = bottom + span * t;
    // 松塔形冠幅
    const r = baseRadius * (0.92 - t * 0.72) * (0.55 + rand() * 0.45);
    const a = rand() * TAU;
    b.ico([Math.cos(a) * r, y, Math.sin(a) * r], grain * (0.8 + rand() * 0.45), 3, col, { mat: 'bloom' });
  }
}

// 
// 阔叶树：主干、分枝与团簇树冠
// 
function broadleaf(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const s = scalar(def.params.sizeScale, 1);
  const height = scalar(p.height, 10);
  const trunk = scalar(p.trunk, height * 0.46);
  const bark = tone(def, 'bark');
  const barkLight = tone(def, 'barkLight', 1);
  const trunkR = scalar(p.trunkRadius, Math.max(0.2, height * 0.045)) * s;
  const canopy = scalar(p.canopy, height * 0.36);
  const blobs = Math.max(3, Math.round(scalar(p.blobs, 7)));
  const sides = Math.round(clamp(scalar(p.sides, 5), 3, 10));
  const jitter = scalar(p.jitter, 0.18);
  const lean = scalar(p.lean, 0);
  const canopyH = Math.max(0.35, scalar(p.canopyRatio, 0.42)) * height;
  const cy = trunk + canopyH * 0.5;
  const limbs = Math.max(0, Math.round(scalar(p.limbCount, 3)));
  const limbLength = scalar(p.limbLength, canopy * 0.62);

  b.prism([0, 0, 0], trunkR * 1.3, trunkR * 1.3, 0, trunk * 0.16, sides, bark, { jitter: jitter * 0.5, rand });
  b.prism([lean * trunk * 0.25, trunk * 0.12, 0], trunkR, trunkR, 0, trunk - trunk * 0.12, sides, bark, { jitter, rand });
  const crown: V3 = [lean * trunk * 0.25, trunk, 0];

  // 主枝
  const limbTips: V3[] = [];
  for (let i = 0; i < limbs; i++) {
    const a = (i / limbs) * TAU + rand() * 0.6;
    const up = scalar(p.branchAngle, 0.75);
    const dir: V3 = [Math.cos(a) * Math.cos(up), Math.sin(up), Math.sin(a) * Math.cos(up)];
    const y = trunk - trunkR * 1.6 - rand() * Math.min(1.1, trunk * 0.18);
    limbTips.push(limbTo(b, [crown[0], y, 0], dir, limbLength * (0.8 + rand() * 0.3), trunkR * 0.5, barkLight));
  }

  // 树冠主团双纬线，外围单圈
  const mainR = canopy;
  b.dome([crown[0], cy, 0], mainR, canopyH * 0.62, mainR, sides, tone(def, 'foliage'), { jitter, rand, rings: 1 });
  for (let i = 0; i < blobs; i++) {
    const t = (i + 0.5) / blobs;
    const a = i * GOLDEN + def.buildSeed * 0.0007;
    const rad = mainR * (0.45 + 0.52 * Math.sqrt(t));
    const y = cy + canopyH * (0.42 * (1 - t) - 0.1) + (rand() - 0.5) * canopyH * 0.28;
    const r = mainR * (0.32 + rand() * 0.22);
    const c = deepen(tone(def, i % 2 ? 'canopy' : 'foliage'), t);
    b.dome([crown[0] + Math.cos(a) * rad, y, Math.sin(a) * rad], r, r * 0.82, r, sides, c, { jitter: jitter * 1.4, rand });
    if (wantsSnowOn(def, 'canopy') && i < blobs * 0.7) {
      snowCap(b, def, [crown[0] + Math.cos(a) * rad, y + r * 0.3, Math.sin(a) * rad], r);
    }
  }
  // 枝头叶团：补上主枝末端的空缺
  for (const tip of limbTips) {
    const r = mainR * (0.34 + rand() * 0.18);
    b.dome(tip, r, r * 0.8, r, sides, tone(def, 'canopy'), { jitter: jitter * 1.3, rand });
  }
  // 冠面碎叶
  leafSpray(b, def, [crown[0], cy, 0], mainR * 1.02, Math.round(scalar(p.leafPetals, blobs + 4)), rand,
    { width: 0.4, pitch: 0.14, spreadY: 0.5 });

  // 花果挂载于冠面外缘
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 6);
  if (bloomCount > 0) {
    bloomCluster(b, def, [crown[0], cy + canopyH * 0.1, 0], mainR * 1.32, bloomCount, rand,
      { size: mainR * 0.12, ySpan: canopyH * 0.5 });
  }
  const fruitCount = fruitCountFor(def, Math.round(scalar(p.fruitCount, 0)), 3);
  if (fruitCount > 0) fruitCluster(b, def, [crown[0], cy - canopyH * 0.16, 0], mainR * 1.0, fruitCount, rand, { size: mainR * 0.08 });
  if (wantsSnowOn(def, 'ground')) {
    b.dome([0, 0.02, 0], trunkR * 2.6, trunkR * 0.22, trunkR * 2.6, 6, snowColor(def), { floor: false, jitter: 0.3, rand, mat: 'snow' });
  }
}

// 
// 棕榈：微弯树干 + 放射状下垂叶片
// 
function palm(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 11);
  const trunk = scalar(p.trunk, height * 0.86);
  const bark = tone(def, 'bark');
  const barkLight = tone(def, 'barkLight', 1);
  const trunkR = scalar(p.trunkRadius, Math.max(0.16, height * 0.026));
  const lean = scalar(p.lean, 0.16);
  const fronds = Math.max(4, Math.round(scalar(p.fronds, 9)));
  const frondLength = scalar(p.frondLength, height * 0.42);
  const droop = clamp(scalar(p.droop, 0.55), 0, 1);
  const segments = Math.max(1, Math.round(scalar(p.segments, 7)));
  const sides = Math.round(clamp(scalar(p.sides, 5), 3, 10));
  const leaf = tone(def, 'palmFrond');
  const leafDark = tone(def, 'foliageDark', 1);

  // 树干：分节棱柱，逐节微小偏移形成弧度
  const nodes = Math.max(3, Math.round(trunk / 1.1));
  let x = 0;
  const nodeH = trunk / nodes;
  for (let i = 0; i < nodes; i++) {
    const y = i * nodeH;
    const t = i / Math.max(1, nodes - 1);
    const nr = trunkR * (1.05 - 0.28 * t);
    b.prism([x, y, 0], nr * 1.08, nr * 1.08, 0, nodeH * 1.02, sides, i % 2 ? bark : barkLight, { jitter: 0.06, rand });
    x += lean * nodeH * (0.35 + t * 0.9);
  }
  const crown: V3 = [x, trunk, 0];
  // 冠基：一小段青绿色叶鞘
  b.prism(crown, trunkR * 0.72, trunkR * 0.72, -0.02, trunkR * 1.9, sides, leafDark, { rand });

  // 叶片：沿方位放射，逐节下垂并收窄
  for (let f = 0; f < fronds; f++) {
    const a = (f / fronds) * TAU + rand() * 0.25;
    const dx = Math.cos(a);
    const dz = Math.sin(a);
    const segLen = frondLength / segments;
    const startY = crown[1] + trunkR * 1.6 + rand() * 0.18;
    let px = crown[0];
    let py = startY;
    let pz = crown[2];
    const r0 = Math.max(0.06, trunkR * 0.34);
    for (let i = 0; i < segments; i++) {
      const t = (i + 1) / segments;
      // 抬升 -> 转平 -> 下垂
      const lift = Math.sin((i / segments) * Math.PI * 0.55) * 0.55;
      const sag = -droop * Math.pow(t, 1.7) * 2.1;
      const ny = py + (lift + sag) * segLen * 0.62;
      const nx = px + dx * segLen * 0.98;
      const nz = pz + dz * segLen * 0.98;
      // 每节一片"叶瓣"
      const w = r0 * (1 - t * 0.55) * 2.6;
      const col = deepen(i % 2 ? leaf : leafDark, 0.25 * t);
      const sideX = -dz;
      const sideZ = dx;
      const mid: V3 = [(px + nx) / 2, (py + ny) / 2, (pz + nz) / 2];
      const tip: V3 = [nx, ny, nz];
      b.quad(
        [px + sideX * w, py, pz + sideZ * w],
        [px - sideX * w, py, pz - sideZ * w],
        [mid[0] - sideX * w * 0.8, mid[1] + w * 0.35, mid[2] - sideZ * w * 0.8],
        [mid[0] + sideX * w * 0.8, mid[1] + w * 0.35, mid[2] + sideZ * w * 0.8],
        col,
      );
      b.quad(
        [mid[0] + sideX * w * 0.8, mid[1] + w * 0.35, mid[2] + sideZ * w * 0.8],
        [mid[0] - sideX * w * 0.8, mid[1] + w * 0.35, mid[2] - sideZ * w * 0.8],
        [tip[0] - sideX * w * 0.34, tip[1], tip[2] - sideZ * w * 0.34],
        [tip[0] + sideX * w * 0.34, tip[1], tip[2] + sideZ * w * 0.34],
        col,
      );
      px = nx; py = ny; pz = nz;
    }
    // 叶尖分叉
    for (const s of [1, -1]) {
      b.petal([px, py, pz], a + s * 0.34, segLen * 0.8, segLen * 0.24, deepen(leaf, 0.25),
        { pitch: -0.3 - droop * 0.35, cup: 0.3, rand });
    }
    if (wantsSnowOn(def, 'canopy') && f % 2 === 0) {
      snowCap(b, def, [crown[0] + dx * frondLength * 0.25, crown[1] + trunkR * 2.1, crown[2] + dz * frondLength * 0.25], frondLength * 0.2, 0.7);
    }
  }

  // 棕榈花序从叶鞘抽出
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 5);
  if (bloomCount > 0) {
    bloomCluster(b, def, [crown[0], crown[1] + trunkR * 1.2, crown[2]], frondLength * 0.4, bloomCount, rand,
      { size: frondLength * 0.06, ySpan: trunkR * 1.6 });
  }

  const fruitCount = fruitCountFor(def, Math.round(scalar(p.fruitCount, 0)), 3);
  if (fruitCount > 0) {
    const col = tone(def, 'fruit');
    for (let i = 0; i < fruitCount; i++) {
      const a = (i / fruitCount) * TAU + rand();
      b.ico([crown[0] + Math.cos(a) * trunkR * 1.5, crown[1] + trunkR * 0.4, crown[2] + Math.sin(a) * trunkR * 1.5],
        trunkR * 0.55, 5, col, { jitter: 0.12, rand, mat: 'fruit', rings: 1 });
    }
  }
  if (wantsSnowOn(def, 'ground')) {
    b.dome([0, 0.02, 0], trunkR * 2.2, trunkR * 0.2, trunkR * 2.2, 6, snowColor(def), { floor: false, jitter: 0.3, rand, mat: 'snow' });
  }
}

// 
// 金合欢：分叉树干 + 扁平伞状树冠
// 
function acacia(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 8);
  const trunk = scalar(p.trunk, height * 0.52);
  const bark = tone(def, 'bark');
  const barkLight = tone(def, 'barkLight', 1);
  const trunkR = scalar(p.trunkRadius, Math.max(0.18, height * 0.055));
  const canopy = scalar(p.canopy, height * 0.5);
  const sides = Math.round(clamp(scalar(p.sides, 5), 3, 10));
  const jitter = scalar(p.jitter, 0.16);
  const limbCount = Math.max(2, Math.round(scalar(p.limbCount, 4)));
  const limbLength = scalar(p.limbLength, trunk * 0.55);
  const fol = tone(def, 'foliage');
  const folDark = tone(def, 'foliageDark', 1);

  b.prism([0, 0, 0], trunkR * 1.35, trunkR * 1.35, 0, trunk * 0.2, sides, bark, { jitter: jitter * 0.5, rand });
  b.prism([0, trunk * 0.16, 0], trunkR, trunkR, 0, trunk - trunk * 0.16, sides, barkLight, { jitter, rand });

  for (let i = 0; i < limbCount; i++) {
    const a = (i / limbCount) * TAU + rand() * 0.5;
    const up = scalar(p.branchAngle, 0.62);
    const dir: V3 = [Math.cos(a) * Math.cos(up), Math.sin(up), Math.sin(a) * Math.cos(up)];
    const y = trunk - trunkR * 1.4 - rand() * trunk * 0.12;
    const tip = limbTo(b, [0, y, 0], dir, limbLength * (0.85 + rand() * 0.35), trunkR * 0.55, barkLight);
    // 伞盖：扁平叶团，越靠外越低，形成"伞"的剪影
    const r = canopy * (0.42 + rand() * 0.16);
    b.dome([tip[0], tip[1] + r * 0.16, tip[2]], r, r * 0.34, r, sides, i % 2 ? fol : folDark, { jitter: jitter * 1.5, rand });
    if (wantsSnowOn(def, 'canopy')) snowCap(b, def, [tip[0], tip[1] + r * 0.3, tip[2]], r * 0.9, 0.75);
  }
  // 中央伞盖：让顶部而不是"秃心"
  const topR = canopy * 0.6;
  b.dome([0, trunk + topR * 0.2, 0], topR, topR * 0.32, topR, sides, fol, { jitter: jitter * 1.3, rand });
  // 伞面碎叶
  leafSpray(b, def, [0, trunk + topR * 0.35, 0], canopy, Math.round(scalar(p.leafPetals, Math.round(limbCount * 3))), rand,
    { width: 0.34, pitch: 0.06, spreadY: 0.25, len: 0.42 });

  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 5);
  if (bloomCount > 0) {
    bloomCluster(b, def, [0, trunk + topR * 0.7, 0], canopy * 1.15, bloomCount, rand,
      { size: canopy * 0.12, ySpan: canopy * 0.32 });
  }
  const fruitCount = fruitCountFor(def, Math.round(scalar(p.fruitCount, 0)), 3);
  if (fruitCount > 0) fruitCluster(b, def, [0, trunk, 0], canopy * 0.7, fruitCount, rand, { size: canopy * 0.09 });
  if (wantsSnowOn(def, 'ground')) {
    b.dome([0, 0.02, 0], trunkR * 2.4, trunkR * 0.2, trunkR * 2.4, 6, snowColor(def), { floor: false, jitter: 0.3, rand, mat: 'snow' });
  }
}

// 
// 猴面包树：膨大主干 + 稀疏团冠
// 
function baobab(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 9);
  const trunk = scalar(p.trunk, height * 0.56);
  const trunkR = scalar(p.trunkRadius, height * 0.13);
  const bark = tone(def, 'bark');
  const barkLight = tone(def, 'barkLight', 1);
  const sides = Math.round(clamp(scalar(p.sides, 6), 3, 12));
  const jitter = scalar(p.jitter, 0.2);
  const canopy = scalar(p.canopy, height * 0.34);
  const fol = tone(def, 'foliage');
  const folDark = tone(def, 'foliageDark', 1);

  // 主干：下粗上细的两段，制造"瓶状"轮廓
  b.prism([0, 0, 0], trunkR * 1.45, trunkR * 1.35, 0, trunk * 0.14, sides, bark, { jitter: jitter * 0.6, rand });
  b.prism([0, trunk * 0.12, 0], trunkR * 0.95, trunkR * 0.9, 0, trunk * 0.5, sides, bark, { jitter, rand });
  b.prism([0, trunk * 0.6, 0], trunkR * 0.66, trunkR * 0.62, 0, trunk * 0.44, sides, barkLight, { jitter, rand });

  // 稀疏分枝 + 小团冠
  const limbs = Math.max(3, Math.round(scalar(p.limbCount, 6)));
  for (let i = 0; i < limbs; i++) {
    const a = (i / limbs) * TAU + rand() * 0.4;
    const up = scalar(p.branchAngle, 0.85);
    const dir: V3 = [Math.cos(a) * Math.cos(up), Math.sin(up), Math.sin(a) * Math.cos(up)];
    const tip = limbTo(b, [0, trunk - trunkR * 0.8 - rand() * trunk * 0.16, 0], dir, trunkR * (1.1 + rand() * 0.6), trunkR * 0.32, barkLight);
    const r = canopy * (0.36 + rand() * 0.2);
    b.dome(tip, r, r * 0.68, r, sides, i % 2 ? fol : folDark, { jitter: jitter * 1.4, rand });
    if (wantsSnowOn(def, 'canopy')) snowCap(b, def, [tip[0], tip[1] + r * 0.25, tip[2]], r * 0.8, 0.9);
  }
  // 冠面补碎叶丰富层次
  leafSpray(b, def, [0, trunk + canopy * 0.2, 0], canopy, Math.round(scalar(p.leafPetals, limbs + 2)), rand,
    { width: 0.42, pitch: 0.12, spreadY: 0.4, len: 0.55 });
  // 花
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 4);
  if (bloomCount > 0) {
    bloomCluster(b, def, [0, trunk + canopy * 0.35, 0], canopy * 1.15, bloomCount, rand,
      { size: canopy * 0.13, ySpan: canopy * 0.5 });
  }

  const fruitCount = fruitCountFor(def, Math.round(scalar(p.fruitCount, 0)), 3);
  if (fruitCount > 0) fruitCluster(b, def, [0, trunk - trunkR, 0], trunkR, fruitCount, rand, { size: trunkR * 0.5 });
}

// 
// 灌木：多茎丛生 + 圆团叶簇
// 
function shrub(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 1.6);
  const aspect = scalar(p.aspect, 1.15);
  const canopy = scalar(p.canopy, height * 0.5 * aspect);
  const blobs = Math.max(2, Math.round(scalar(p.blobs, 4)));
  const sides = Math.round(clamp(scalar(p.sides, 5), 3, 8));
  const jitter = scalar(p.jitter, 0.22);
  const stems = Math.max(1, Math.round(scalar(p.stems, 3)));
  const spread = scalar(p.spread, canopy * 0.45);
  const bark = tone(def, 'barkLight');
  const fol = tone(def, 'foliage');
  const folDark = tone(def, 'foliageDark', 1);

  for (let i = 0; i < stems; i++) {
    const a = (i / stems) * TAU + rand();
    const sx = Math.cos(a) * spread * (0.35 + rand() * 0.5);
    const sz = Math.sin(a) * spread * (0.35 + rand() * 0.5);
    const h = height * (0.5 + rand() * 0.4);
    b.prism([sx, 0, sz], 0.045 + height * 0.014, 0.045 + height * 0.014, 0, h, 3, bark, { rand, jitter: 0.15 });
  }
  for (let i = 0; i < blobs; i++) {
    const a = i * GOLDEN + def.buildSeed * 0.0013;
    const rad = canopy * (i === 0 ? 0 : 0.42 + rand() * 0.42);
    const y = height * (i === 0 ? 0.62 : 0.34 + rand() * 0.5);
    const r = canopy * (i === 0 ? 0.62 : 0.34 + rand() * 0.24);
    const c = deepen(i % 2 ? fol : folDark, i / blobs);
    // 主叶团加一圈纬线
    b.dome([Math.cos(a) * rad, y, Math.sin(a) * rad], r, r * 0.84, r, sides, c, { jitter, rand, rings: i === 0 ? 1 : 0 });
    if (wantsSnowOn(def, 'canopy') && i < blobs * 0.75) {
      snowCap(b, def, [Math.cos(a) * rad, y + r * 0.3, Math.sin(a) * rad], r * 0.95);
    }
  }
  // 冠面碎叶 + 花
  leafSpray(b, def, [0, height * 0.66, 0], canopy * 1.02, Math.round(scalar(p.leafPetals, blobs + 3)), rand,
    { width: 0.44, pitch: 0.1, spreadY: 0.45, len: 0.5 });
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 5);
  if (bloomCount > 0) {
    // 半径只到冠幅（不要再往外）
    bloomCluster(b, def, [0, height * 0.7, 0], canopy * 1.0, bloomCount, rand,
      { size: canopy * 0.16, ySpan: height * 0.26 });
  }
  const fruitCount = fruitCountFor(def, Math.round(scalar(p.fruitCount, 0)), 3);
  if (fruitCount > 0) fruitCluster(b, def, [0, height * 0.62, 0], canopy * 0.95, fruitCount, rand, { size: canopy * 0.12 });
  if (wantsSnowOn(def, 'ground')) {
    b.dome([0, 0.02, 0], canopy * 1.5, 0.16, canopy * 1.5, 6, snowColor(def), { floor: false, jitter: 0.35, rand, mat: 'snow' });
  }
}

// 
// 多肉灌木：肥厚叶片 + 顶生花
// 
function succulent(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 1.2);
  const spread = scalar(p.spread, height * 0.5);
  const layers = Math.max(2, Math.round(scalar(p.layers, 3)));
  const perLayer = Math.max(3, Math.round(scalar(p.blobs, 5)));
  const leaf = tone(def, 'foliage');
  const leafDark = tone(def, 'foliageDark', 1);
  const thorn = tone(def, 'thorn');

  for (let l = 0; l < layers; l++) {
    const t = l / Math.max(1, layers - 1);
    const y = 0.06 + t * height * 0.62;
    const rad = spread * (1 - t * 0.45);
    const len = height * (0.42 - t * 0.16);
    for (let i = 0; i < perLayer; i++) {
      const a = (i / perLayer) * TAU + l * 0.8 + rand() * 0.2;
      const dx = Math.cos(a), dz = Math.sin(a);
      const tip: V3 = [dx * rad * 1.4, y + len * 0.55, dz * rad * 1.4];
      b.ico([dx * rad * 0.7, y + len * 0.28, dz * rad * 0.7], [spread * 0.3, len * 0.34, spread * 0.3], 5, l % 2 ? leaf : leafDark, {
        rotY: a, jitter: 0.18, rand, rings: 1,
      });
      if (scalar(p.thorns, 0) > 0) {
        b.cone([tip[0], tip[1], tip[2]], spread * 0.05, spread * 0.05, spread * 0.22, 3, thorn, { rotY: a, rand });
      }
    }
  }
  // 中央花茎
  b.prism([0, height * 0.5, 0], spread * 0.08, spread * 0.08, 0, height * 0.36, 3, leafDark, { rand });
  const bloomCount = bloomCountFor(def, Math.max(1, Math.round(scalar(p.bloomCount, 3))), 4);
  // 莲 / 多肉的花就开在叶心顶上
  bloomCluster(b, def, [0, height * 0.9, 0], spread * 0.6, bloomCount, rand, { size: spread * 0.5, ySpan: height * 0.14 });
  // 果：莲蓬 / 多肉蒴果，结实时从叶心顶出
  const fruitCount = fruitCountFor(def, Math.round(scalar(p.fruitCount, 0)), 3);
  if (fruitCount > 0) fruitCluster(b, def, [0, height * 0.72, 0], spread * 0.55, fruitCount, rand, { size: spread * 0.35 });
  if (wantsSnowOn(def, 'canopy')) snowCap(b, def, [0, height * 0.78, 0], spread * 0.7, 0.6);
}

// 
// 荆棘丛：多枝下弯 + 刺 + 稀疏小叶
// 
function bramble(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 1.25);
  const spread = scalar(p.spread, height * 0.85);
  const stems = Math.max(3, Math.round(scalar(p.stems, 6)));
  const blobs = Math.max(2, Math.round(scalar(p.blobs, 4)));
  const bark = tone(def, 'barkLight');
  const thorn = tone(def, 'thorn');
  const fol = tone(def, 'foliage');
  const folDark = tone(def, 'foliageDark', 1);
  const thorny = scalar(p.thorns, 1) > 0;
  const sides = Math.round(clamp(scalar(p.sides, 4), 3, 6));

  for (let i = 0; i < stems; i++) {
    const a = (i / stems) * TAU + rand() * 0.5;
    const up = 0.55 + rand() * 0.5;
    const dir: V3 = [Math.cos(a) * Math.cos(up), Math.sin(up), Math.sin(a) * Math.cos(up)];
    const tip = limbTo(b, [Math.cos(a) * spread * 0.15, 0.02, Math.sin(a) * spread * 0.15], dir,
      height * (0.85 + rand() * 0.5), 0.05 + height * 0.02, bark);
    if (thorny) {
      const ta = rand() * TAU;
      b.cone(tip, spread * 0.045, spread * 0.045, spread * 0.16, 3, thorn, { rotY: ta, rand });
    }
  }
  for (let i = 0; i < blobs; i++) {
    const a = i * GOLDEN + def.buildSeed * 0.0021;
    const rad = spread * (0.25 + rand() * 0.6);
    const y = height * (0.3 + rand() * 0.55);
    const r = spread * (0.18 + rand() * 0.14);
    b.dome([Math.cos(a) * rad, y, Math.sin(a) * rad], r, r * 0.8, r, sides, i % 2 ? fol : folDark, { jitter: 0.3, rand });
  }
  // 悬钩子 / 旱棘丛的叶本来就只有几片
  leafSpray(b, def, [0, height * 0.55, 0], spread * 0.8, Math.round(scalar(p.leafPetals, blobs + 5)), rand,
    { width: 0.4, pitch: 0.12, spreadY: 0.4, len: 0.45 });
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 5);
  if (bloomCount > 0) {
    bloomCluster(b, def, [0, height * 0.6, 0], spread * 0.95, bloomCount, rand,
      { size: spread * 0.18, ySpan: height * 0.3 });
  }
  const fruitCount = fruitCountFor(def, Math.round(scalar(p.fruitCount, 0)), 3);
  if (fruitCount > 0) fruitCluster(b, def, [0, height * 0.5, 0], spread * 0.9, fruitCount, rand, { size: spread * 0.1 });
  if (wantsSnowOn(def, 'canopy')) snowCap(b, def, [0, height * 0.72, 0], spread * 0.75, 0.7);
}

// 
// 草丛 / 蕨类：交叉面片
// 
function grassClump(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 0.72);
  const blades = Math.max(2, Math.round(scalar(p.blades, 7)));
  const spread = scalar(p.spread, height * 0.5);
  const fol = tone(def, 'foliage');
  const folDark = tone(def, 'foliageDark', 1);
  const tips: V3[] = [];

  for (let i = 0; i < blades; i++) {
    const a = (i / blades) * TAU + rand() * 0.8;
    const rad = spread * (0.15 + rand() * 0.8);
    const h = height * (0.55 + rand() * 0.65);
    const bx = Math.cos(a) * rad;
    const bz = Math.sin(a) * rad;
    b.crossQuad([bx, 0, bz], spread * (0.42 + rand() * 0.3), h, i % 2 ? fol : folDark, {
      rotY: a, bend: (rand() - 0.5) * spread * 0.7,
    });
    tips.push([bx + (rand() - 0.5) * spread * 0.7, h, bz + (rand() - 0.5) * spread * 0.7]);
  }
  if (wantsSnowOn(def, 'ground') || wantsSnowOn(def, 'canopy')) {
    b.dome([0, 0.02, 0], spread * 1.1, height * 0.24, spread * 1.1, 5, snowColor(def), { floor: false, jitter: 0.4, rand, mat: 'snow' });
  }
  // 谷类作物的穗
  const grainCount = fruitCountFor(def, Math.round(scalar(p.fruitCount, 0)), 3);
  if (grainCount > 0) {
    const col = tone(def, 'fruit');
    for (let i = 0; i < grainCount; i++) {
      const tip = tips[Math.floor(rand() * tips.length)];
      const r = Math.max(0.02, spread * 0.16);
      b.ico([tip[0], tip[1] + r * 0.6, tip[2]], [r, r * 2.6, r], 4, col, { jitter: 0.16, rand, mat: 'fruit', rings: 2 });
      // 芒
      // 用窄花瓣画
      for (const s of [1, -1]) {
        b.petal([tip[0], tip[1] + r * 2.0, tip[2]], (s > 0 ? 0.5 : Math.PI - 0.5) + (rand() - 0.5) * 0.6,
          r * 3.2, r * 0.26, col, { pitch: 0.95, cup: 0.1, mat: 'fruit', rand });
      }
    }
  }
  // 花期
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 3);
  if (bloomCount > 0) {
    bloomCluster(b, def, [0, height * 0.82, 0], spread * 0.85, bloomCount, rand,
      { size: spread * 0.22, ySpan: height * 0.22 });
  }
}

// 
// 幼苗 / 树苗
// 
function sapling(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 1.1);
  const trunk = scalar(p.trunk, height * 0.5);
  const bark = tone(def, 'barkLight');
  const fol = tone(def, 'foliage');
  const leafR = scalar(p.canopy, height * 0.28);

  b.prism([0, 0, 0], height * 0.035, height * 0.035, 0, trunk, 3, bark, { rand });
  const lobes = Math.max(1, Math.round(scalar(p.blobs, 3)));
  for (let i = 0; i < lobes; i++) {
    const a = i * GOLDEN + rand();
    const rad = leafR * (i === 0 ? 0 : 0.5);
    b.dome([Math.cos(a) * rad, trunk + leafR * (i === 0 ? 0.55 : 0.2 + rand() * 0.5), Math.sin(a) * rad],
      leafR * (i === 0 ? 1 : 0.7), leafR * 0.9, leafR * (i === 0 ? 1 : 0.7), 4, fol, { jitter: 0.22, rand });
  }
  // 细长叶
  leafSpray(b, def, [0, trunk + leafR * 0.5, 0], leafR * 1.15, Math.round(scalar(p.leafPetals, lobes * 3)), rand,
    { width: 0.24, pitch: 0.18, spreadY: 0.5, len: 0.85 });
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 3);
  if (bloomCount > 0) {
    bloomCluster(b, def, [0, trunk + leafR, 0], leafR * 1.0, bloomCount, rand,
      { size: leafR * 0.16, ySpan: leafR * 0.5 });
  }
  if (wantsSnowOn(def, 'canopy')) snowCap(b, def, [0, trunk + leafR * 0.6, 0], leafR * 0.9, 0.8);
}

// 幼苗原型：适配乔灌草与兰花
function sprout(b: LowPolyBuilder, def: PlantDef): void {
  if (def.params.stateForm === 'orchid') {
    orchid(b, def);
    return;
  }
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  // 幼苗必须比成株矮
  // 贴地植物参数缩放保护
  const speciesH = scalar(p.height, 0.5);
  const height = Math.min(scalar(p.height, 0.5), Math.max(0.08, speciesH * 0.42));
  const trunk = Math.min(scalar(p.trunk, height * 0.52), height * 0.62);
  const stem = tone(def, 'barkLight');
  const fol = tone(def, 'foliage');
  const folDark = tone(def, 'foliageDark', 1);
  const leafR = Math.min(scalar(p.canopy, height * 0.3), height * 0.4);

  // 主茎
  const stemR = Math.max(0.012, height * 0.05);
  b.prism([0, 0, 0], stemR * 1.3, stemR * 1.3, 0, trunk * 0.5, 4, stem, { rand, jitter: 0.1 });
  b.prism([0, trunk * 0.46, 0], stemR, stemR, 0, trunk * 0.56, 4, stem, { rand, jitter: 0.12 });
  // 子叶
  for (let i = 0; i < 2; i++) {
    const a = i * Math.PI + rand() * 0.5;
    b.petal([Math.cos(a) * leafR * 0.3, trunk * 0.5, Math.sin(a) * leafR * 0.3], a, leafR * (0.95 + rand() * 0.3),
      leafR * 0.75, i ? fol : folDark, { pitch: -0.06 + rand() * 0.2, cup: 0.35, rand });
  }
  // 真叶
  for (let i = 0; i < 4; i++) {
    const a = i * GOLDEN + def.buildSeed * 0.0017 + rand() * 0.4;
    const len = leafR * (0.7 + rand() * 0.4);
    b.petal([Math.cos(a) * leafR * 0.22, trunk * (0.62 + i * 0.06), Math.sin(a) * leafR * 0.22], a, len, len * 0.52,
      i % 2 ? fol : folDark, { pitch: 0.12 + rand() * 0.3, cup: 0.4, rand });
  }
  // 顶芽包含圆顶与嫩叶
  b.dome([0, trunk + leafR * 0.3, 0], leafR * 0.5, leafR * 0.62, leafR * 0.5, 5, folDark, { jitter: 0.22, rand });
  for (let i = 0; i < 2 + Math.min(2, Math.round(scalar(p.blobs, 2))); i++) {
    const a = i * GOLDEN + rand();
    const len = leafR * (0.55 + rand() * 0.35);
    b.petal([0, trunk + leafR * (0.42 + rand() * 0.2), 0], a, len, len * 0.55, i % 2 ? fol : folDark,
      { pitch: 0.45 + rand() * 0.3, cup: 0.42, rand });
  }
  if (wantsSnowOn(def, 'canopy')) snowCap(b, def, [0, trunk + leafR * 0.5, 0], leafR * 0.8, 0.7);
}

// 
// 干枯
//
// 枯木不生成花果
// 
function withered(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 4);
  const trunk = Math.max(0.25, scalar(p.trunk, height * 0.42) * 0.42);
  const trunkR = Math.max(0.06, scalar(p.trunkRadius, Math.max(0.18, height * 0.035)));
  const wood = tone(def, 'witheredDark');
  const stem = tone(def, 'withered');
  const dryLeaf = tone(def, 'witheredStem');
  const sides = Math.round(clamp(scalar(p.sides, 5), 3, 8));
  const jitter = scalar(p.jitter, 0.2);
  const limbs = Math.max(3, Math.round(scalar(p.limbCount, 4)) + 1);
  const lean = scalar(p.lean, 0.06);

  // 断头主干：两截，顶端收口（不再往上长）
  b.prism([0, 0, 0], trunkR * 1.4, trunkR * 1.4, 0, trunk * 0.3, sides, wood, { jitter: jitter * 0.5, rand });
  b.prism([lean * trunk * 0.2, trunk * 0.22, 0], trunkR * 1.05, trunkR * 1.05, 0, trunk * 0.62, sides, stem, { jitter, rand });

  // 歪枝
  for (let i = 0; i < limbs; i++) {
    const a = (i / limbs) * TAU + rand() * 0.8;
    const up = 0.28 + rand() * 0.75;
    const dir: V3 = [Math.cos(a) * Math.cos(up), Math.sin(up), Math.sin(a) * Math.cos(up)];
    const y = trunk * (0.3 + rand() * 0.5);
    const len = height * (0.2 + rand() * 0.24);
    const tip = limbTo(b, [lean * trunk * 0.2, y, 0], dir, len, trunkR * (0.34 + rand() * 0.24), stem);
    if (rand() < 0.65) {
      const a2 = a + (rand() - 0.5) * 1.4;
      const up2 = up * (0.6 + rand() * 0.7);
      const dir2: V3 = [Math.cos(a2) * Math.cos(up2), Math.sin(up2), Math.sin(a2) * Math.cos(up2)];
      limbTo(b, tip, dir2, len * (0.35 + rand() * 0.3), trunkR * 0.22, wood);
    }
  }
  // 零星枯叶
  // 用花瓣片（叶）而不是小圆团
  const clumps = Math.max(2, Math.min(5, Math.round(scalar(p.blobs, 3) * 0.5)));
  for (let i = 0; i < clumps; i++) {
    const a = i * GOLDEN + def.buildSeed * 0.0017;
    const rad = height * (0.12 + rand() * 0.22);
    const len = height * 0.085 * (0.8 + rand() * 0.5);
    b.petal([Math.cos(a) * rad, trunk * (0.6 + rand() * 0.6), Math.sin(a) * rad],
      a + (rand() - 0.5) * 0.8, len, len * 0.5, deepen(dryLeaf, rand() * 0.3),
      { pitch: -0.25 + rand() * 0.4, cup: 0.5, rand });
  }
}

// 
// 花树：花量由参数控制
function blossomTree(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 3.4);
  const trunk = scalar(p.trunk, height * 0.34);
  const trunkR = Math.max(0.06, scalar(p.trunkRadius, Math.max(0.1, height * 0.05)));
  const wood = tone(def, 'barkDark');
  const barkLight = tone(def, 'barkLight', 1);
  const leaf = tone(def, 'foliage');
  const sides = Math.round(clamp(scalar(p.sides, 5), 3, 8));
  const limbs = Math.max(3, Math.round(scalar(p.limbCount, 5)));

  // 虬曲主干：两段折向，制造"老梅"的折枝感
  const bend = scalar(p.lean, 0.18);
  b.prism([0, 0, 0], trunkR * 1.5, trunkR * 1.5, 0, trunk * 0.55, sides, wood, { jitter: 0.16, rand });
  b.prism([bend * trunk * 0.3, trunk * 0.5, 0], trunkR * 0.9, trunkR * 0.9, 0, trunk * 0.55, sides, barkLight,
    { jitter: 0.2, rand });

  const tips: V3[] = [];
  for (let i = 0; i < limbs; i++) {
    const a = (i / limbs) * TAU + rand() * 0.7;
    const up = 0.35 + rand() * 0.6;
    const dir: V3 = [Math.cos(a) * Math.cos(up), Math.sin(up), Math.sin(a) * Math.cos(up)];
    const y = trunk * (0.45 + rand() * 0.45);
    const tip = limbTo(b, [bend * trunk * 0.3, y, 0], dir, height * (0.3 + rand() * 0.22), trunkR * 0.42, barkLight);
    tips.push(tip);
    // 二级枝：梅花讲究"疏影横斜"
    if (rand() < 0.6) {
      const a2 = a + (rand() - 0.5) * 1.6;
      const dir2: V3 = [Math.cos(a2) * Math.cos(up), Math.sin(up) * 1.1, Math.sin(a2) * Math.cos(up)];
      tips.push(limbTo(b, tip, dir2, height * (0.1 + rand() * 0.1), trunkR * 0.24, wood));
    }
  }
  // 疏叶
  for (const [i, tip] of tips.entries()) {
    const r = height * (i % 2 ? 0.11 : 0.14);
    for (let k = 0; k < 3; k++) {
      const a = rand() * TAU;
      b.petal([tip[0], tip[1] - r * 0.2, tip[2]], a, r * (0.8 + rand() * 0.5), r * 0.45,
        i % 2 ? leaf : tone(def, 'foliageDark', 1), { pitch: -0.05 + rand() * 0.3, cup: 0.4, rand });
    }
  }
  // 团花
  //
  // 尺寸约束
  // 再大就从"花"变成"枝头挂球"
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 5);
  const bloomR = clamp(scalar(p.bloomRadius, height * 0.05), 0.03, 0.4);
  if (bloomCount > 0) {
    const perTip = Math.max(2, Math.round(bloomCount / Math.max(1, tips.length)));
    for (const tip of tips) {
      for (let k = 0; k < perTip; k++) {
        const a = rand() * TAU;
        const rr = bloomR * (0.5 + rand() * 0.9);
        flowerAt(b, def, [tip[0] + Math.cos(a) * rr, tip[1] + bloomR * (0.4 + rand() * 0.8), tip[2] + Math.sin(a) * rr],
          bloomR * (0.85 + rand() * 0.35), rand);
      }
    }
    // 主干上部补充花簇
    bloomCluster(b, def, [bend * trunk * 0.3, trunk + height * 0.1, 0], height * 0.16,
      Math.max(3, Math.round(bloomCount * 0.5)), rand, { size: bloomR, ySpan: height * 0.16 });
  }
  if (wantsSnowOn(def, 'canopy')) snowCap(b, def, [0, trunk + height * 0.3, 0], height * 0.24, 0.8);
}

// 
// 竹：多秆丛生、外凸节与顶窄叶
// 
function bamboo(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 6);
  // 一丛的秆数
  const culms = Math.max(2, Math.round(scalar(p.culms, 4)));
  const culmR = Math.max(0.04, scalar(p.trunkRadius, height * 0.022));
  const spread = scalar(p.spread, height * 0.16);
  const nodeStep = Math.max(0.35, scalar(p.nodeStep, height * 0.18));
  const blades = Math.max(3, Math.round(scalar(p.blades, 12)));
  const leafLen = scalar(p.frondLength, height * 0.16);
  const stem = tone(def, 'bambooCulm');
  const node = deepen(stem, 0.3);
  const leaf = tone(def, 'bamboo');
  const leafDark = tone(def, 'foliageDeep', 1);
  const sides = Math.round(clamp(scalar(p.sides, 5), 3, 6));
  const jitter = scalar(p.jitter, 0.1);

  const tops: V3[] = [];
  for (let c = 0; c < culms; c++) {
    const a = (c / culms) * TAU + rand() * 0.9;
    const rad = spread * (0.25 + rand() * 0.75);
    const x0 = Math.cos(a) * rad;
    const z0 = Math.sin(a) * rad;
    const h = height * (0.62 + rand() * 0.4);
    const lean = scalar(p.lean, 0.08) * (rand() - 0.5) * 2;
    const nodes = Math.max(3, Math.round(h / nodeStep));
    const nodeH = h / nodes;
    const r = culmR * (0.85 + rand() * 0.4);
    for (let i = 0; i < nodes; i++) {
      const t = i / nodes;
      const y = i * nodeH;
      const nx = x0 + lean * y * 0.35 * (0.4 + t);
      const rr = r * (1 - t * 0.2);
      // 节间：比节环细，两段之间留出节的位置
      b.prism([nx, y, z0], rr, rr, 0, nodeH * 0.86, sides, stem, { jitter: jitter * 0.4, rand });
      // 竹节
      // 一节只加一个环
  // 外凸环强化竹节几何特征
      b.cone([nx, y + nodeH * 0.84, z0], rr * 1.36, rr * 1.36, nodeH * 0.24, sides, node,
        { radiusRatio: 0.8, jitter: 0.05, rand });
    }
    const topX = x0 + lean * h * 0.35 * 1.4;
    tops.push([topX, h, z0]);
  }
  // 秆顶错开挂狭长竹叶
  for (let i = 0; i < blades; i++) {
    const top = tops[Math.floor(rand() * tops.length)];
    const a = rand() * TAU;
    const len = leafLen * (0.45 + rand() * 0.35);
    const col = i % 2 ? leaf : leafDark;
    const drop = leafLen * (0.05 + (i % 3) * 0.5) + rand() * leafLen * 0.2;
    bentLeaf(b, [top[0], top[1] - drop, top[2]], a, len, leafLen * 0.15, len * 0.24, len * (0.5 + rand() * 0.4), col, rand);
  }
  // 地下走茎的笋尖：让竹林根部不空
  for (let i = 0; i < 2; i++) {
    const a = rand() * TAU;
    const rad = spread * (0.3 + rand() * 0.5);
    b.cone([Math.cos(a) * rad, 0, Math.sin(a) * rad], culmR * 0.9, culmR * 0.9, height * 0.07, 4, stem,
      { jitter: 0.2, rand, cap: true });
  }
  // 竹花稀疏三鳞被
  if (def.bloom) {
    const sparse = Math.max(1, Math.round(scalar(p.bloomCount, 2)));
    for (let i = 0; i < sparse; i++) {
      const top = tops[Math.floor(rand() * tops.length)];
      const a = rand() * TAU;
      const rr = leafLen * (0.3 + rand() * 0.5);
      flowerAt(b, def, [top[0] + Math.cos(a) * rr, top[1] + leafLen * 0.3, top[2] + Math.sin(a) * rr], leafLen * 0.24, rand);
    }
  }
  if (wantsSnowOn(def, 'canopy')) snowCap(b, def, [0, height * 0.72, 0], spread * 0.9, 0.6);
}

// 
// 兰花：基生叶、花葶与小花
// 
function orchid(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 0.6);
  const blades = Math.max(5, Math.round(scalar(p.blades, 9)));
  const leafLen = scalar(p.frondLength, height * 1.1);
  const spread = scalar(p.spread, height * 0.3);
  const leaf = tone(def, 'foliage');
  const leafDark = tone(def, 'foliageDeep', 1);
  const stem = tone(def, 'bamboo');

  // 基生叶
  for (let i = 0; i < blades; i++) {
    const a = (i / blades) * TAU + rand() * 0.5;
    const len = leafLen * (0.7 + rand() * 0.5);
    const arc = height * (0.3 + rand() * 0.45);
    const col = i % 2 ? leaf : leafDark;
    bentLeaf(b, [0, 0, 0], a, len, leafLen * 0.1, arc, arc * 0.75, col, rand);
  }
  // 花葶：一根细茎从叶心抽出，顶端弓起
  const stemH = height * (0.9 + rand() * 0.3);
  const bendA = rand() * TAU;
  const bx = Math.cos(bendA) * spread * 0.3;
  const bz = Math.sin(bendA) * spread * 0.3;
  b.prism([0, 0, 0], height * 0.026, height * 0.026, 0, stemH * 0.72, 3, stem, { rand, jitter: 0.1 });
  b.limb([0, stemH * 0.7, 0], [Math.cos(bendA) * 0.42, 0.9, Math.sin(bendA) * 0.42], stemH * 0.34, height * 0.022,
    stem, { sides: 3, rand });
  // 花
  // 只在 def.bloom 为真时开
  const bloomCount = bloomCountFor(def, Math.max(1, Math.round(scalar(p.bloomCount, 5))), 4);
  for (let i = 0; i < bloomCount; i++) {
    const t = i / Math.max(1, bloomCount - 1);
    const a = i * GOLDEN + rand();
    const px = bx + Math.cos(a) * spread * (0.35 + t * 0.5);
    const pz = bz + Math.sin(a) * spread * (0.35 + t * 0.5);
    const py = stemH * (0.82 + t * 0.22);
    flowerAt(b, def, [px, py, pz], height * 0.11 * (1 - t * 0.25), rand);
  }
}

/** 原型注册表 */
export const ARCHETYPES: Record<string, (b: LowPolyBuilder, def: PlantDef) => void> = {
  conifer,
  broadleaf,
  palm,
  acacia,
  baobab,
  shrub,
  succulent,
  bramble,
  grassClump,
  sapling,
  blossomTree,
  bamboo,
  orchid,
  sprout,
  withered,
};

export type ArchetypeName = keyof typeof ARCHETYPES;

/** 每个原型的必备默认参数 */
export const ARCHETYPE_DEFAULTS: Record<string, Partial<PlantParams>> = {
  // 棱柱精度提升微增面数
  conifer: { height: 12, trunk: 2.6, canopy: 3.1, layers: 5, sides: 6, jitter: 0.14, layerStep: 0.82, spikeCount: 1 },
  broadleaf: { height: 10, trunk: 4.6, canopy: 3.6, blobs: 7, sides: 6, jitter: 0.18, limbCount: 3, branchAngle: 0.75, canopyRatio: 0.42 },
  palm: { height: 11, trunk: 9.4, canopy: 4.6, fronds: 9, frondLength: 4.6, droop: 0.55, segments: 7, sides: 6, lean: 0.16 },
  acacia: { height: 8, trunk: 4.1, canopy: 4, limbCount: 4, sides: 6, jitter: 0.16, branchAngle: 0.62 },
  baobab: { height: 9, trunk: 5, trunkRadius: 1.17, canopy: 3, limbCount: 6, sides: 6, jitter: 0.2, branchAngle: 0.85 },
  shrub: { height: 1.6, canopy: 0.9, blobs: 4, sides: 6, jitter: 0.22, stems: 3, aspect: 1.15 },
  succulent: { height: 1.2, layers: 3, blobs: 5, spread: 0.6, bloomCount: 3 },
  bramble: { height: 1.25, stems: 6, blobs: 4, spread: 1.1, sides: 5, jitter: 0.3, thorns: 1, bloomCount: 0 },
  // 颖花两枚稃片对应双瓣
  grassClump: { height: 0.72, blades: 7, spread: 0.36, petals: 2 },
  sapling: { height: 1.1, trunk: 0.55, canopy: 0.3, blobs: 3 },
  // 状态形态与专类形态默认尺寸
  sprout: { height: 0.5, trunk: 0.26, canopy: 0.16, blobs: 2, sides: 4, jitter: 0.16 },
  withered: { height: 4.5, trunk: 1.9, trunkRadius: 0.2, limbCount: 5, sides: 4, jitter: 0.24, blobs: 4 },
  blossomTree: { height: 3.4, trunk: 1.2, trunkRadius: 0.16, limbCount: 5, blobs: 4, sides: 5, jitter: 0.22, lean: 0.18, petals: 5 },
  // 一丛 4 秆（原 5）
  bamboo: { height: 6, culms: 4, trunkRadius: 0.12, spread: 1, nodeStep: 1.1, blades: 14, frondLength: 1, sides: 5, jitter: 0.1, lean: 0.08, petals: 3 },
  orchid: { height: 0.6, blades: 9, frondLength: 0.66, spread: 0.2, bloomCount: 5, sides: 5, petals: 6 },
};

/** 生长带默认参数 */
export const CLIMATE_DEFAULTS: Record<ClimateZone, Partial<PlantParams>> = {
  tropical: { height: 13, canopy: 4.4, droop: 0.6, layers: 6, blobs: 9 },
  subtropical: { height: 11.5, canopy: 3.9, layers: 5, blobs: 8 },
  temperate: { height: 10, canopy: 3.6, layers: 5, blobs: 7 },
  cold: { height: 8.5, canopy: 2.7, layers: 4, blobs: 5, droop: 0.7 },
};

/** 通用默认值：任何原型都可能读到的字段 */
export const GLOBAL_DEFAULTS: Partial<PlantParams> = {
  sides: 5,
  jitter: 0.15,
  blobJitter: 0.2,
  sizeScale: 1,
};

/** 生成一株植物的低模几何 */
export function buildPlant(def: PlantDef): PlantGeometry {
  const fn = ARCHETYPES[def.archetype];
  if (!fn) throw new Error(`[Vegetation] 未知原型: ${def.archetype}（植物 ${def.id}）`);
  const b = new LowPolyBuilder();
  fn(b, def);
  const geo = b.build();
  if (geo.triangleCount === 0) throw new Error(`[Vegetation] 植物 ${def.id} 没有产出任何面，请检查参数`);
  return geo;
}
