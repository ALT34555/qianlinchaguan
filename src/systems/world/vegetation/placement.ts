/** 植被散布 */
import { hash2 } from '../../../core/math/Random';
import { CHUNK_SIZE, SEA_LEVEL } from '../../../core/config';
import { Block, blockBase } from '../Blocks';
import { getPlantVariant, scatterablePlants } from './Plants';
import type { ClimateZone } from './types';

/** 每个方块都重复调用的相邻查询函数 */
export interface SurfaceQuery {
  /** 地表顶面高度；未加载区域返回 NaN */
  height(x: number, z: number): number;
  /** 水面高度；未加载区域返回 NaN */
  waterLevel(x: number, z: number): number;
  /** 表层方块 id；未加载区域返回 0（空气） */
  surface(x: number, z: number): number;
}

export interface PlantPlacement {
  /** 世界坐标（方块） */
  x: number;
  z: number;
  /** 地表顶面高度，植物基点在此 */
  y: number;
  /** 植物 id */
  plant: string;
  /** 0~1 的方位随机 */
  yaw: number;
  /** 0.85~1.2 的尺寸抖动 */
  size: number;
}

/** 生长带 -> 候选物种缓存 */
const candidatesByClimate = new Map<ClimateZone, string[]>();
function candidatesFor(climate: ClimateZone): string[] {
  let list = candidatesByClimate.get(climate);
  if (!list) {
    list = scatterablePlants(climate).map((v) => v.id);
    candidatesByClimate.set(climate, list);
  }
  return list;
}

/** 判断某方块能否长植物 */
export function canHostPlant(q: SurfaceQuery, x: number, z: number, maxSlope = 3): boolean {
  const h = q.height(x, z);
  if (!Number.isFinite(h)) return false;
  const wl = q.waterLevel(x, z);
  if (Number.isFinite(wl) && h < wl) return false;
  const s = blockBase(q.surface(x, z));
  if (s !== Block.GRASS && s !== Block.FOREST_SOIL && s !== Block.DRY_DIRT && s !== Block.SAND) return false;
  let maxDelta = 0;
  for (const [dx, dz] of [[1, 0], [-1, 0], [0, 1], [0, -1]] as const) {
    const nh = q.height(x + dx, z + dz);
    if (!Number.isFinite(nh)) return false; // 邻居未加载：先不长，等区块齐了再说
    const d = Math.abs(nh - h);
    if (d > maxDelta) maxDelta = d;
  }
  return maxDelta <= maxSlope;
}

/** 单位区块的植被散布 */
export function scatterPlants(
  cx: number,
  cz: number,
  climate: ClimateZone,
  seed: number,
  q: SurfaceQuery,
  density: number,
): PlantPlacement[] {
  const out: PlantPlacement[] = [];
  const candidates = candidatesFor(climate);
  if (candidates.length === 0) return out;
  const k = Math.max(0, Math.min(1, density));
  const ox = cx * CHUNK_SIZE;
  const oz = cz * CHUNK_SIZE;
  for (let lz = 0; lz < CHUNK_SIZE; lz++) {
    for (let lx = 0; lx < CHUNK_SIZE; lx++) {
      const x = ox + lx;
      const z = oz + lz;
      if (hash2(x, z, seed ^ 0x5bf03635) >= k) continue;
      if (!canHostPlant(q, x, z)) continue;
      // 物种挑选：同一地块的三种独立哈希，保证同种子同结果
      const pick = hash2(x, z, seed ^ 0x1b873593);
      const idx = Math.min(candidates.length - 1, Math.floor(pick * candidates.length));
      const id = candidates[idx];
      const variant = getPlantVariant(id);
      // 沙地只长带 'arid' 标签的物种，其余地块不限
      if (blockBase(q.surface(x, z)) === Block.SAND && !(variant?.tags ?? []).includes('arid')) continue;
      out.push({
        x,
        z,
        y: q.height(x, z),
        plant: id,
        yaw: hash2(x, z, seed ^ 0x27d4eb2d) * Math.PI * 2,
        size: 0.85 + hash2(x, z, seed ^ 0x165667b1) * 0.35,
      });
    }
  }
  return out;
}

/** 便捷构造 */
export function chunkQuery(
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
