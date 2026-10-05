/**
 * 植被散布：纯粹的"某个方块该长什么"判定，不依赖 three、不接触场景图。
 *
 * 之所以单独成文件：世界接入（区块加载时撒树）下一步再做，届时主线程与 Worker
 * 两侧都要用同一套判定；这里保持无副作用，谁都能调用。
 *
 * 输入是相邻两个区块的地表数据（主线程侧的 LoadedChunk 就有），
 * 只需继续向外读 1 格即可覆盖最高 3 格冠幅的跨界遮挡判断。
 */
import { hash2 } from '../../../core/math/Random';
import { CHUNK_SIZE, SEA_LEVEL } from '../../../core/config';
import { Block } from '../Blocks';
import { PLANT_IDS, getPlantVariant } from './Plants';
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
  /** 植物 id，交给 buildPlantById 取几何 */
  plant: string;
  /** 0~1 的方位随机，接入时用于绕 Y 轴旋转与轻微缩放 */
  yaw: number;
  /** 0.85~1.2 的尺寸抖动 */
  size: number;
}

/**
 * 生长带 -> 候选物种。
 * 用 id 前缀做气候筛选，避免手写四份物种清单（参数表里已声明 climate）。
 */
const CLIMATE_OF_PREFIX: readonly [string, ClimateZone][] = [
  ['tree.trop.', 'tropical'],
  ['tree.sub.', 'subtropical'],
  ['tree.temp.', 'temperate'],
  ['tree.cold.', 'cold'],
  ['shrub.trop.', 'tropical'],
  ['shrub.sub.', 'subtropical'],
  ['shrub.temp.', 'temperate'],
  ['shrub.cold.', 'cold'],
  ['sprout.', 'temperate'],
];

function climateOf(id: string): ClimateZone | undefined {
  for (const [prefix, climate] of CLIMATE_OF_PREFIX) if (id.startsWith(prefix)) return climate;
  return undefined;
}

/** 按生长带分组的候选物种缓存 */
const candidatesByClimate = new Map<ClimateZone, string[]>();
function candidatesFor(climate: ClimateZone): string[] {
  let list = candidatesByClimate.get(climate);
  if (!list) {
    list = PLANT_IDS.filter((id) => climateOf(id) === climate);
    candidatesByClimate.set(climate, list);
  }
  return list;
}

/**
 * 判断某方块能否长植物。
 * 规则（与地表的体素语义一致）：
 *   1. 该列必须在水面之上（有水就不是陆地）；
 *   2. 必须是草方块 / 林地腐殖土 / 旱地土 / 沙（沙地只长耐旱物种，由标签控制）；
 *   3. 不能是陡坡（四邻高差超过阈值时视为裸岩）；
 *   4. 邻居必须已加载，避免区块边缘出现"半株树"或悬空树。
 */
export function canHostPlant(q: SurfaceQuery, x: number, z: number, maxSlope = 3): boolean {
  const h = q.height(x, z);
  if (!Number.isFinite(h)) return false;
  const wl = q.waterLevel(x, z);
  if (Number.isFinite(wl) && h < wl) return false;
  const s = q.surface(x, z);
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

/**
 * 单位区块的植被散布。
 *
 * 候选密度（0~1）：0.35 约等于每 3 个方块一株，林地里会自然连成冠层；
 * 单体物种（大乔木）靠区域噪声成簇，避免整片均匀铺满。
 */
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
      if (q.surface(x, z) === Block.SAND && !(variant?.tags ?? []).includes('arid')) continue;
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

/** 便捷构造：从区块高度/表层数组生成查询函数（含四周一圈邻居） */
export function chunkQuery(
  heights: Int16Array | Float32Array,
  surfaces: Uint8Array,
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
