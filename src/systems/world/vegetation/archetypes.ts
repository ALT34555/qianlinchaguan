/**
 * 植物原型库：每个原型是一个"参数 -> 低模几何"的纯函数。
 *
 * 约定：
 *   · 所有尺寸单位为**格**（1 格 = 1 个方块边长），原点在植物根部地面中心，+Y 向上；
 *   · 只使用确定性随机（调用方传入 mulberry32 序列），同种子必然复现同一株；
 *   · 颜色一律来自已解析的色板，原型不写死任何色值；
 *   · 参数表（content/data/world/plants/*.json）里的 params 会覆盖原型默认值。
 */
import { mulberry32 } from '../../../core/math/Random';
import { LowPolyBuilder, parseColor, type PlantGeometry } from './geometry';
import { DEFAULT_BLOOM_COLOR, type ClimateZone, type PaletteKey, type PlantDef, type PlantParams, type SnowTarget } from './types';

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

/**
 * 取色调（已按季节解析）。
 *
 * 花色单独走一条优先级链：**接口传入的花色 > 物种色板 bloom > 默认花色 #a8456b**，
 * 这样"开花版本"既能被外部统一指定颜色，又能保留各物种自带的固有花色。
 * 其余色板键缺失时回退到洋红，以便一眼看出参数表写错。
 */
function tone(def: PlantDef, key: PaletteKey | undefined, scale = 1): RGB {
  let base: string | undefined;
  if (key === 'bloom') base = def.bloomColor ?? def.palette.bloom ?? DEFAULT_BLOOM_COLOR;
  else base = key ? def.palette[key] : undefined;
  const rgb = parseColor(base ?? '#ff00ff');
  return [clampByte(rgb[0] * scale), clampByte(rgb[1] * scale), clampByte(rgb[2] * scale)];
}

function clampByte(v: number): number {
  return v < 0 ? 0 : v > 255 ? 255 : v;
}

/** 按半径向心收缩：让受光的外层叶片更亮、内层更暗，弥补低模没有环境光遮蔽 */
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

/**
 * 花朵数量：开花版本开启时，即使参数表没写 bloomCount 也按 fallback 绽放，
 * 写了更多则以参数表为准（不会减少物种本来就有的花量）。
 */
function bloomCountFor(def: PlantDef, requested: number, fallback: number): number {
  return def.bloom ? Math.max(requested, fallback) : requested;
}

/**
 * 在叶团上方铺一层薄雪盖：半球形薄壳（不封底）。
 * 只在冬季变体里出现，靠 material='snow' 走独立材质槽。
 */
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

/** 花簇：几片薄薄的中心点状叶团，用 bloom 材质槽，春/秋可单独着色 */
function bloomCluster(b: LowPolyBuilder, def: PlantDef, center: V3, r: number, count: number, rand: () => number): void {
  const col = tone(def, 'bloom');
  for (let i = 0; i < count; i++) {
    const a = rand() * TAU;
    const rr = r * (0.2 + rand() * 0.55);
    b.dome([center[0] + Math.cos(a) * rr, center[1] + (rand() - 0.5) * r * 0.4, center[2] + Math.sin(a) * rr],
      r * 0.42, r * 0.34, r * 0.42, 4, col, { jitter: 0.25, rand, mat: 'bloom', floor: false });
  }
}

/** 果实：小球一簇 */
function fruitCluster(b: LowPolyBuilder, def: PlantDef, center: V3, r: number, count: number, rand: () => number): void {
  const col = tone(def, 'fruit');
  for (let i = 0; i < count; i++) {
    const a = rand() * TAU;
    const rr = r * (0.25 + rand() * 0.7);
    const y = center[1] - rand() * r * 0.8;
    b.ico([center[0] + Math.cos(a) * rr, y, center[2] + Math.sin(a) * rr], r * 0.2, 4, col, { jitter: 0.15, rand, mat: 'fruit' });
  }
}

/** 枝干：从根部向上的一段细枝，末端收细 */
function limbTo(b: LowPolyBuilder, start: V3, dir: V3, length: number, radius: number, col: RGB): V3 {
  const len = Math.hypot(dir[0], dir[1], dir[2]) || 1;
  const d: V3 = [dir[0] / len, dir[1] / len, dir[2] / len];
  b.limb(start, d, length, radius, col, { sides: 3, tipRatio: 0.42 });
  return [start[0] + d[0] * length, start[1] + d[1] * length, start[2] + d[2] * length];
}

// ---------------------------------------------------------------------------
// 针叶树：层叠的伞盖 + 尖锐树顶
//
// 针叶树是裸子植物，**不参与开花版本**：本函数不引用 bloom 色板键，
// 也不调用 bloomCluster，因此 def.bloom 对它无效（这是刻意的，不是遗漏）。
// 需要"有花"的针叶树观感时，请用落叶松/阔叶原型，不要在这里加花。
// ---------------------------------------------------------------------------
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
  // 树顶：1~2 根朝天细尖（并让雪线在冬态下顶到树尖）
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
}

// ---------------------------------------------------------------------------
// 阔叶树：直立主干 + 数条上扬主枝 + 团簇树冠
// ---------------------------------------------------------------------------
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

  // 主枝：绕主干均匀分布并向上扬起，末端成为树冠最外层叶团
  const limbTips: V3[] = [];
  for (let i = 0; i < limbs; i++) {
    const a = (i / limbs) * TAU + rand() * 0.6;
    const up = scalar(p.branchAngle, 0.75);
    const dir: V3 = [Math.cos(a) * Math.cos(up), Math.sin(up), Math.sin(a) * Math.cos(up)];
    const y = trunk - trunkR * 1.6 - rand() * Math.min(1.1, trunk * 0.18);
    limbTips.push(limbTo(b, [crown[0], y, 0], dir, limbLength * (0.8 + rand() * 0.3), trunkR * 0.5, barkLight));
  }

  // 树冠叶团：主团 + 沿黄金角散布的外层团（低模"云朵状"轮廓）
  const mainR = canopy;
  b.dome([crown[0], cy, 0], mainR, canopyH * 0.62, mainR, sides, tone(def, 'foliage'), { jitter, rand });
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

  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 6);
  if (bloomCount > 0) bloomCluster(b, def, [crown[0], cy + canopyH * 0.2, 0], mainR * 0.9, bloomCount, rand);
  const fruitCount = Math.round(scalar(p.fruitCount, 0));
  if (fruitCount > 0) fruitCluster(b, def, [crown[0], cy, 0], mainR * 0.85, fruitCount, rand);
  if (wantsSnowOn(def, 'ground')) {
    b.dome([0, 0.02, 0], trunkR * 2.6, trunkR * 0.22, trunkR * 2.6, 6, snowColor(def), { floor: false, jitter: 0.3, rand, mat: 'snow' });
  }
}

// ---------------------------------------------------------------------------
// 棕榈：微弯树干 + 放射状下垂叶片
// ---------------------------------------------------------------------------
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
      // 抬升 -> 转平 -> 下垂：droop 越大，叶尖垂得越低
      const lift = Math.sin((i / segments) * Math.PI * 0.55) * 0.55;
      const sag = -droop * Math.pow(t, 1.7) * 2.1;
      const ny = py + (lift + sag) * segLen * 0.62;
      const nx = px + dx * segLen * 0.98;
      const nz = pz + dz * segLen * 0.98;
      // 每节一片"叶瓣"：两片沿叶轴展开的四边形拼成 V 形截面
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
    if (wantsSnowOn(def, 'canopy') && f % 2 === 0) {
      snowCap(b, def, [crown[0] + dx * frondLength * 0.25, crown[1] + trunkR * 2.1, crown[2] + dz * frondLength * 0.25], frondLength * 0.2, 0.7);
    }
  }

  // 花：棕榈是单子叶开花植物，花序从冠基叶鞘间抽出
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 5);
  if (bloomCount > 0) {
    bloomCluster(b, def, [crown[0], crown[1] + trunkR * 1.2, crown[2]], frondLength * 0.34, bloomCount, rand);
  }

  const fruitCount = Math.round(scalar(p.fruitCount, 0));
  if (fruitCount > 0) {
    const col = tone(def, 'fruit');
    for (let i = 0; i < fruitCount; i++) {
      const a = (i / fruitCount) * TAU + rand();
      b.ico([crown[0] + Math.cos(a) * trunkR * 1.5, crown[1] + trunkR * 0.4, crown[2] + Math.sin(a) * trunkR * 1.5],
        trunkR * 0.55, 5, col, { jitter: 0.12, rand, mat: 'fruit' });
    }
  }
  if (wantsSnowOn(def, 'ground')) {
    b.dome([0, 0.02, 0], trunkR * 2.2, trunkR * 0.2, trunkR * 2.2, 6, snowColor(def), { floor: false, jitter: 0.3, rand, mat: 'snow' });
  }
}

// ---------------------------------------------------------------------------
// 金合欢：分叉树干 + 扁平伞状树冠
// ---------------------------------------------------------------------------
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

  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 5);
  if (bloomCount > 0) bloomCluster(b, def, [0, trunk + topR * 0.7, 0], canopy * 0.8, bloomCount, rand);
  const fruitCount = Math.round(scalar(p.fruitCount, 0));
  if (fruitCount > 0) fruitCluster(b, def, [0, trunk, 0], canopy * 0.7, fruitCount, rand);
  if (wantsSnowOn(def, 'ground')) {
    b.dome([0, 0.02, 0], trunkR * 2.4, trunkR * 0.2, trunkR * 2.4, 6, snowColor(def), { floor: false, jitter: 0.3, rand, mat: 'snow' });
  }
}

// ---------------------------------------------------------------------------
// 猴面包树：膨大主干 + 稀疏团冠
// ---------------------------------------------------------------------------
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
  // 花：猴面包树的花大而多，开在冠层外围
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 4);
  if (bloomCount > 0) bloomCluster(b, def, [0, trunk + canopy * 0.35, 0], canopy * 0.85, bloomCount, rand);

  const fruitCount = Math.round(scalar(p.fruitCount, 0));
  if (fruitCount > 0) fruitCluster(b, def, [0, trunk - trunkR, 0], trunkR, fruitCount, rand);
}

// ---------------------------------------------------------------------------
// 灌木：多茎丛生 + 圆团叶簇
// ---------------------------------------------------------------------------
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
    b.dome([Math.cos(a) * rad, y, Math.sin(a) * rad], r, r * 0.84, r, sides, c, { jitter, rand });
    if (wantsSnowOn(def, 'canopy') && i < blobs * 0.75) {
      snowCap(b, def, [Math.cos(a) * rad, y + r * 0.3, Math.sin(a) * rad], r * 0.95);
    }
  }
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 5);
  if (bloomCount > 0) bloomCluster(b, def, [0, height * 0.72, 0], canopy * 0.95, bloomCount, rand);
  const fruitCount = Math.round(scalar(p.fruitCount, 0));
  if (fruitCount > 0) fruitCluster(b, def, [0, height * 0.62, 0], canopy * 0.85, fruitCount, rand);
  if (wantsSnowOn(def, 'ground')) {
    b.dome([0, 0.02, 0], canopy * 1.5, 0.16, canopy * 1.5, 6, snowColor(def), { floor: false, jitter: 0.35, rand, mat: 'snow' });
  }
}

// ---------------------------------------------------------------------------
// 多肉灌木：肥厚叶片 + 顶生花
// ---------------------------------------------------------------------------
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
  bloomCluster(b, def, [0, height * 0.9, 0], spread * 0.5, bloomCount, rand);
  if (wantsSnowOn(def, 'canopy')) snowCap(b, def, [0, height * 0.78, 0], spread * 0.7, 0.6);
}

// ---------------------------------------------------------------------------
// 荆棘丛：多枝下弯 + 刺 + 稀疏小叶
// ---------------------------------------------------------------------------
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
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 5);
  if (bloomCount > 0) bloomCluster(b, def, [0, height * 0.6, 0], spread * 0.8, bloomCount, rand);
  const fruitCount = Math.round(scalar(p.fruitCount, 0));
  if (fruitCount > 0) fruitCluster(b, def, [0, height * 0.5, 0], spread * 0.8, fruitCount, rand);
  if (wantsSnowOn(def, 'canopy')) snowCap(b, def, [0, height * 0.72, 0], spread * 0.75, 0.7);
}

// ---------------------------------------------------------------------------
// 草丛 / 蕨类：交叉面片
// ---------------------------------------------------------------------------
function grassClump(b: LowPolyBuilder, def: PlantDef): void {
  const p = def.params;
  const rand = mulberry32(def.buildSeed);
  const height = scalar(p.height, 0.72);
  const blades = Math.max(2, Math.round(scalar(p.blades, 7)));
  const spread = scalar(p.spread, height * 0.5);
  const fol = tone(def, 'foliage');
  const folDark = tone(def, 'foliageDark', 1);

  for (let i = 0; i < blades; i++) {
    const a = (i / blades) * TAU + rand() * 0.8;
    const rad = spread * (0.15 + rand() * 0.8);
    const h = height * (0.55 + rand() * 0.65);
    b.crossQuad([Math.cos(a) * rad, 0, Math.sin(a) * rad], spread * (0.42 + rand() * 0.3), h, i % 2 ? fol : folDark, {
      rotY: a, bend: (rand() - 0.5) * spread * 0.7,
    });
  }
  if (wantsSnowOn(def, 'ground') || wantsSnowOn(def, 'canopy')) {
    b.dome([0, 0.02, 0], spread * 1.1, height * 0.24, spread * 1.1, 5, snowColor(def), { floor: false, jitter: 0.4, rand, mat: 'snow' });
  }
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 3);
  if (bloomCount > 0) bloomCluster(b, def, [0, height * 0.8, 0], spread * 0.6, bloomCount, rand);
}

// ---------------------------------------------------------------------------
// 幼苗 / 树苗：细干 + 小团叶（用于补植与近景细节）
// ---------------------------------------------------------------------------
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
  const bloomCount = bloomCountFor(def, Math.round(scalar(p.bloomCount, 0)), 3);
  if (bloomCount > 0) bloomCluster(b, def, [0, trunk + leafR, 0], leafR * 0.7, bloomCount, rand);
  if (wantsSnowOn(def, 'canopy')) snowCap(b, def, [0, trunk + leafR * 0.6, 0], leafR * 0.9, 0.8);
}

/** 原型注册表：JSON 的 archetype 字段必须是这里的键 */
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
};

export type ArchetypeName = keyof typeof ARCHETYPES;

/** 每个原型的必备默认参数：与生长带默认值叠加，参数表只写差异 */
export const ARCHETYPE_DEFAULTS: Record<string, Partial<PlantParams>> = {
  conifer: { height: 12, trunk: 2.6, canopy: 3.1, layers: 5, sides: 5, jitter: 0.14, layerStep: 0.82, spikeCount: 1 },
  broadleaf: { height: 10, trunk: 4.6, canopy: 3.6, blobs: 7, sides: 5, jitter: 0.18, limbCount: 3, branchAngle: 0.75, canopyRatio: 0.42 },
  palm: { height: 11, trunk: 9.4, canopy: 4.6, fronds: 9, frondLength: 4.6, droop: 0.55, segments: 7, sides: 5, lean: 0.16 },
  acacia: { height: 8, trunk: 4.1, canopy: 4, limbCount: 4, sides: 5, jitter: 0.16, branchAngle: 0.62 },
  baobab: { height: 9, trunk: 5, trunkRadius: 1.17, canopy: 3, limbCount: 6, sides: 6, jitter: 0.2, branchAngle: 0.85 },
  shrub: { height: 1.6, canopy: 0.9, blobs: 4, sides: 5, jitter: 0.22, stems: 3, aspect: 1.15 },
  succulent: { height: 1.2, layers: 3, blobs: 5, spread: 0.6, bloomCount: 3 },
  bramble: { height: 1.25, stems: 6, blobs: 4, spread: 1.1, sides: 4, jitter: 0.3, thorns: 1, bloomCount: 0 },
  grassClump: { height: 0.72, blades: 7, spread: 0.36 },
  sapling: { height: 1.1, trunk: 0.55, canopy: 0.3, blobs: 3 },
};

/** 生长带默认参数：同一原型在不同气候下的默认尺寸/形态 */
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

/**
 * 生成一株植物的低模几何。
 * 尺寸由 PlantDef.params 决定（注册表已完成 全局 -> 生长带 -> 原型 -> 物种 的四层合并）。
 */
export function buildPlant(def: PlantDef): PlantGeometry {
  const fn = ARCHETYPES[def.archetype];
  if (!fn) throw new Error(`[Vegetation] 未知原型: ${def.archetype}（植物 ${def.id}）`);
  const b = new LowPolyBuilder();
  fn(b, def);
  const geo = b.build();
  if (geo.triangleCount === 0) throw new Error(`[Vegetation] 植物 ${def.id} 没有产出任何面，请检查参数`);
  return geo;
}
