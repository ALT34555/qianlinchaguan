import { terrainDiagonal, terrainHeightAt, triangleHeight, type HeightSampler } from './TerrainSurface';
import { SEA_LEVEL } from '../../core/config';

export const WATER_OFFSET = .12;
export function waterVertexLevel(x: number, z: number, height: HeightSampler, water: HeightSampler, seed: number): number {
  let total = 0, count = 0;
  for (const [dx, dz] of [[-1, -1], [0, -1], [-1, 0], [0, 0]]) {
    const level = water(x + dx, z + dz);
    if (Number.isFinite(level) && height(x + dx, z + dz) < level) { total += level; count++; }
  }
  if (count) return Math.fround(total / count - WATER_OFFSET);
  // 低模坡面可能在原始干格中穿过海平面，补齐这段真实岸线，不能留下方形水边或接缝。
  return terrainHeightAt(x, z, seed, height) < SEA_LEVEL - WATER_OFFSET ? Math.fround(SEA_LEVEL - WATER_OFFSET) : -Infinity;
}

export function isWaterCascade(x: number, z: number, height: HeightSampler, water: HeightSampler): boolean {
  const level = water(x, z);
  return Number.isFinite(level) && height(x, z) < level && [[1, 0], [-1, 0], [0, 1], [0, -1]].some(([dx, dz]) =>
    height(x + dx, z + dz) < water(x + dx, z + dz) && Math.abs(level - water(x + dx, z + dz)) > 2);
}

/** 与水面网格相同的三角采样；没有覆盖到该位置的水面时返回 -Infinity。 */
function cellWaterHeight(x: number, z: number, ix: number, iz: number, seed: number, height: HeightSampler, water: HeightSampler): number {
  const level = water(ix, iz);
  let result: number;
  if (isWaterCascade(ix, iz, height, water)) result = level - WATER_OFFSET;
  else {
    const vertices = [[ix, iz], [ix, iz + 1], [ix + 1, iz + 1], [ix + 1, iz]]
      .map(([px, pz]) => waterVertexLevel(px, pz, height, water, seed));
    const finite = vertices.filter(Number.isFinite);
    if (!finite.length) return -Infinity;
    const fallback = Number.isFinite(level) && height(ix, iz) < level ? level - WATER_OFFSET : Math.min(...finite);
    const [a, b, c, d] = vertices.map(value => Number.isFinite(value) ? value : fallback);
    result = triangleHeight(x - ix, z - iz, a, b, c, d, terrainDiagonal(ix, iz, seed));
  }
  return terrainHeightAt(x, z, seed, height) < result ? result : -Infinity;
}

export function waterHeightAt(x: number, z: number, seed: number, height: HeightSampler, water: HeightSampler): number {
  const ix = Math.floor(x), iz = Math.floor(z);
  let result = cellWaterHeight(x, z, ix, iz, seed, height, water);
  // 格线上的点也属于左/上侧的面，岸线与瀑布边界不能因 floor 选到干格而漏判。
  const xs = x === ix ? [ix, ix - 1] : [ix], zs = z === iz ? [iz, iz - 1] : [iz];
  for (const px of xs) for (const pz of zs) {
    if (px === ix && pz === iz) continue;
    result = Math.max(result, cellWaterHeight(x, z, px, pz, seed, height, water));
  }
  return result;
}
