import {TERRAIN_ORIGIN,terrainDiagonal} from './planet-macro/terrain-geometry.mjs';
import type {HeightSampler} from './planet-macro/terrain-geometry.mjs';
export {TERRAIN_GRID,TERRAIN_ORIGIN,terrainVertexHeight,terrainDiagonal,triangleHeight,terrainHeightAt,terrainCell} from './planet-macro/terrain-geometry.mjs';
export type {HeightSampler} from './planet-macro/terrain-geometry.mjs';

/** 矩形脚底与各三角形的交集极值 */
export function terrainGroundUnder(x: number, z: number, halfWidth: number, seed: number, sample: HeightSampler): number {
  const x0 = x - halfWidth, x1 = x + halfWidth, z0 = z - halfWidth, z1 = z + halfWidth;
  let result = -Infinity;
  const probe = (px: number, pz: number) => {
    if (px >= x0 && px <= x1 && pz >= z0 && pz <= z1) result = Math.max(result, sample(px, pz));
  };
  for (let iz = Math.floor(z0 - TERRAIN_ORIGIN) + TERRAIN_ORIGIN; iz <= z1; iz += 1) {
    for (let ix = Math.floor(x0 - TERRAIN_ORIGIN) + TERRAIN_ORIGIN; ix <= x1; ix += 1) {
      const left = Math.max(ix, x0), right = Math.min(ix + 1, x1);
      const top = Math.max(iz, z0), bottom = Math.min(iz + 1, z1);
      probe(left, top); probe(left, bottom); probe(right, top); probe(right, bottom);
      const flipped = terrainDiagonal(ix, iz, seed);
      const zOnDiagonal = (px: number) => iz + (flipped ? 1 - (px - ix) : px - ix);
      const xOnDiagonal = (pz: number) => ix + (flipped ? 1 - (pz - iz) : pz - iz);
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
