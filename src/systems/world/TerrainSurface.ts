/** 连续低模地表的共同规则：Worker 网格与主线程碰撞必须使用相同顶点和对角线。 */
import { hash2 } from '../../core/math/Random';

export type HeightSampler = (x: number, z: number) => number;
/** 每两格一个低模采样点；水面仍保留逐格细度，兼顾窄河道。 */
export const TERRAIN_GRID = 2;

/** 原高度样本在格中心，四个相邻样本的均值定义格角；区块边界也使用世界坐标采样。 */
export function terrainVertexHeight(x: number, z: number, height: HeightSampler): number {
  return Math.fround((height(x - 1, z - 1) + height(x, z - 1) + height(x - 1, z) + height(x, z)) * .25);
}

export function terrainDiagonal(x: number, z: number, seed: number): boolean {
  return hash2(Math.floor(x / TERRAIN_GRID), Math.floor(z / TERRAIN_GRID), seed) > .5;
}

export function triangleHeight(u: number, v: number, a: number, b: number, c: number, d: number, flip: boolean): number {
  if (flip) return u + v <= 1 ? a + (d - a) * u + (b - a) * v
    : c + (b - c) * (1 - u) + (d - c) * (1 - v);
  return v >= u ? a + (c - b) * u + (b - a) * v : a + (d - a) * u + (c - d) * v;
}

/** 三角形上的重心插值，不用双线性曲面，以免脚底与可见平面不一致。 */
export function terrainHeightAt(x: number, z: number, seed: number, height: HeightSampler): number {
  const ix = Math.floor(x / TERRAIN_GRID) * TERRAIN_GRID, iz = Math.floor(z / TERRAIN_GRID) * TERRAIN_GRID;
  const u = (x - ix) / TERRAIN_GRID, v = (z - iz) / TERRAIN_GRID;
  const a = terrainVertexHeight(ix, iz, height);
  // 格线只需两端，区块最外侧不要读取外扩范围之外的无关格角。
  if (u === 0 && v === 0) return a;
  if (u === 0) return a + (terrainVertexHeight(ix, iz + TERRAIN_GRID, height) - a) * v;
  if (v === 0) return a + (terrainVertexHeight(ix + TERRAIN_GRID, iz, height) - a) * u;
  const b = terrainVertexHeight(ix, iz + TERRAIN_GRID, height);
  const c = terrainVertexHeight(ix + TERRAIN_GRID, iz + TERRAIN_GRID, height);
  const d = terrainVertexHeight(ix + TERRAIN_GRID, iz, height);
  return triangleHeight(u, v, a, b, c, d, terrainDiagonal(ix, iz, seed));
}

/** 矩形脚底与各三角形的交集极值：矩形角、格点以及对角线/矩形边的交点。 */
export function terrainGroundUnder(x: number, z: number, halfWidth: number, seed: number, sample: HeightSampler): number {
  const x0 = x - halfWidth, x1 = x + halfWidth, z0 = z - halfWidth, z1 = z + halfWidth;
  let result = -Infinity;
  const probe = (px: number, pz: number) => {
    if (px >= x0 && px <= x1 && pz >= z0 && pz <= z1) result = Math.max(result, sample(px, pz));
  };
  for (let iz = Math.floor(z0 / TERRAIN_GRID) * TERRAIN_GRID; iz <= z1; iz += TERRAIN_GRID) {
    for (let ix = Math.floor(x0 / TERRAIN_GRID) * TERRAIN_GRID; ix <= x1; ix += TERRAIN_GRID) {
      const left = Math.max(ix, x0), right = Math.min(ix + TERRAIN_GRID, x1);
      const top = Math.max(iz, z0), bottom = Math.min(iz + TERRAIN_GRID, z1);
      probe(left, top); probe(left, bottom); probe(right, top); probe(right, bottom);
      const flipped = terrainDiagonal(ix, iz, seed);
      const zOnDiagonal = (px: number) => iz + (flipped ? TERRAIN_GRID - (px - ix) : px - ix);
      const xOnDiagonal = (pz: number) => ix + (flipped ? TERRAIN_GRID - (pz - iz) : pz - iz);
      for (const px of [left, right]) {
        const pz = zOnDiagonal(px);
        if (pz >= top && pz <= bottom) probe(px, pz);
      }
      for (const pz of [top, bottom]) {
        const px = xOnDiagonal(pz);
        if (px >= left && px <= right) probe(px, pz);
      }
    }
  }
  return result;
}
