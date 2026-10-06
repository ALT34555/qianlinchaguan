/** 岩石散布 */
import { hash2 } from '../../../core/math/Random';
import { CHUNK_SIZE, SEA_LEVEL } from '../../../core/config';
import { rocksForChunk } from './Rocks';
import { ROCK_FORM_CODES, type RockForm, type Season } from './types';

/** 与植被同名接口 */
export interface SurfaceQuery {
  /** 地表顶面高度；未加载区域返回 NaN */
  height(x: number, z: number): number;
  /** 水面高度；未加载区域返回 NaN */
  waterLevel(x: number, z: number): number;
  /** 表层方块 id；未加载区域返回 0（空气） */
  surface(x: number, z: number): number;
}

/** 一次岩石摆放 */
export interface RockPlacement {
  /** 世界坐标（方块） */
  x: number;
  z: number;
  /** 地表顶面高度 */
  y: number;
  /** 岩石单位 id */
  rock: string;
  /** 形态档（露头 / 半埋 / 叠置 / 覆被） */
  form: RockForm;
  /** 形态档编号（落盘 / 网络用） */
  formCode: number;
  /** 0~1 的方位随机：接入时用于绕 Y 轴旋转 */
  yaw: number;
  /** 0.85~1.2 的尺寸抖动 */
  size: number;
  /** 个体形态种子 */
  shapeSeed: number;
}

/** 判断某处能否摆石头 */
export function canHostRock(q: SurfaceQuery, x: number, z: number, maxSlope = 3): boolean {
  const h = q.height(x, z);
  if (!Number.isFinite(h)) return false;
  const wl = q.waterLevel(x, z);
  if (Number.isFinite(wl) && h < wl) return false;
  // 海平面以下的干地（河床、谷底）允许，只在真的泡在水下时才排除
  let maxDelta = 0;
  for (const [dx, dz] of [[1, 0], [-1, 0], [0, 1], [0, -1]] as const) {
    const nh = q.height(x + dx, z + dz);
    if (!Number.isFinite(nh)) return false; // 邻居未加载：先不摆，等区块齐了再说
    const d = Math.abs(nh - h);
    if (d > maxDelta) maxDelta = d;
  }
  return maxDelta <= maxSlope;
}

/** 形态档选取 */
const FORM_WEIGHTS_WARM: readonly (readonly [RockForm, number])[] = [
  ['outcrop', 0.52],
  ['buried', 0.3],
  ['stacked', 0.1],
  ['crusted', 0.08],
];

const FORM_WEIGHTS_COLD: readonly (readonly [RockForm, number])[] = [
  ['outcrop', 0.36],
  ['buried', 0.26],
  ['stacked', 0.08],
  ['crusted', 0.3],
];

/** 按权重挑一个形态档（`r` 为 0~1） */
function pickForm(r: number, season: Season, snowCrust: boolean): RockForm {
  const cold = season === 'winter' || season === 'autumn';
  const table = cold || snowCrust ? FORM_WEIGHTS_COLD : FORM_WEIGHTS_WARM;
  let acc = 0;
  for (const [form, w] of table) {
    acc += w;
    if (r < acc) return form;
  }
  return 'outcrop';
}

/** 单位区块的岩石散布 */
export function scatterRocks(
  cx: number,
  cz: number,
  chunkType: number,
  seed: number,
  q: SurfaceQuery,
  density: number,
  season: Season = 'summer',
): RockPlacement[] {
  const out: RockPlacement[] = [];
  const units = rocksForChunk(chunkType);
  if (units.length === 0) return out;
  const k = Math.max(0, Math.min(1, density));
  const ox = cx * CHUNK_SIZE;
  const oz = cz * CHUNK_SIZE;
  for (let lz = 0; lz < CHUNK_SIZE; lz++) {
    for (let lx = 0; lx < CHUNK_SIZE; lx++) {
      const x = ox + lx;
      const z = oz + lz;
      // 三处独立的盐值
      if (hash2(x, z, seed ^ 0x51ed270b) >= k) continue;
      if (!canHostRock(q, x, z)) continue;
      const pick = hash2(x, z, seed ^ 0x2545f491);
      const unit = units[Math.min(units.length - 1, Math.floor(pick * units.length))];
      const shapeSeed = Math.floor(hash2(x, z, seed ^ 0x1b873593) * 0xffffffff) >>> 0;
      const snowCrust = season === 'winter' || season === 'autumn';
      const form = pickForm(hash2(x, z, seed ^ 0x27d4eb2d), season, snowCrust);
      out.push({
        x,
        z,
        y: q.height(x, z),
        rock: unit.id,
        form,
        formCode: ROCK_FORM_CODES[form],
        yaw: hash2(x, z, seed ^ 0x165667b1) * Math.PI * 2,
        size: 0.85 + hash2(x, z, seed ^ 0x9e3779b1) * 0.35,
        shapeSeed,
      });
    }
  }
  return out;
}

/** 便捷别名 */
export function rocksAt(
  x: number,
  z: number,
  chunkType: number,
  seed: number,
  q: SurfaceQuery,
  density: number,
  season: Season = 'summer',
): RockPlacement | null {
  const cx = Math.floor(x / CHUNK_SIZE);
  const cz = Math.floor(z / CHUNK_SIZE);
  const list = scatterRocks(cx, cz, chunkType, seed, q, density, season);
  return list.find((p) => p.x === x && p.z === z) ?? null;
}

/** 便捷构造 */
export function rockQuery(
  heights: Int16Array | Float32Array,
  surfaces: Uint32Array | Uint16Array | Uint8Array,
  waterLevels: Float32Array,
  paddedSize: number,
  originX: number,
  originZ: number,
): SurfaceQuery {
  const at = (x: number, z: number): number => {
    const lx = x - originX;
    const lz = z - originZ;
    if (lx < 0 || lz < 0 || lx >= paddedSize || lz >= paddedSize) return -1;
    return (lz * paddedSize + lx);
  };
  const fallback = { h: SEA_LEVEL, w: SEA_LEVEL, s: 0 };
  return {
    height: (x, z) => {
      const i = at(x, z);
      return i < 0 ? Number.NaN : heights[i];
    },
    waterLevel: (x, z) => {
      const i = at(x, z);
      return i < 0 ? Number.NaN : (waterLevels[i] ?? fallback.w);
    },
    surface: (x, z) => {
      const i = at(x, z);
      return i < 0 ? fallback.s : surfaces[i];
    },
  };
}
