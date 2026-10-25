import {iceVertexLevel,iceVertexCoverage} from './planet-macro/water-geometry.mjs';
import {waterVertexLevel} from './planet-macro/water-geometry.mjs';
import { terrainDiagonal, terrainHeightAt, triangleHeight, type HeightSampler } from './TerrainSurface';

export const WATER_OFFSET = .12;
export const SHALLOW_STATIC_DEPTH = 1.2;
export type ShallowSampler = (x: number, z: number) => boolean;
export function shallowVertexLimit(x: number, z: number, height: HeightSampler, water: HeightSampler, shallow: ShallowSampler): number {
  let count = 0;
  for (const [dx, dz] of [[-1, -1], [0, -1], [-1, 0], [0, 0]]) {
    const px = x + dx, pz = z + dz, level = water(px, pz), bed = height(px, pz);
    if (!Number.isFinite(level) || bed >= level) continue;
    if (!shallow(px, pz) || level - bed > SHALLOW_STATIC_DEPTH + 1e-5) return Infinity;
    count++;
  }
  return count ? SHALLOW_STATIC_DEPTH : Infinity;
}
function shallowCellLimit(x: number, z: number, height: HeightSampler, water: HeightSampler, shallow: ShallowSampler): number {
  let count = 0;
  for (let dz = -1; dz <= 1; dz++) for (let dx = -1; dx <= 1; dx++) {
    const px = x + dx, pz = z + dz, level = water(px, pz), bed = height(px, pz);
    if (!Number.isFinite(level) || bed >= level) continue;
    if (!shallow(px, pz) || level - bed > SHALLOW_STATIC_DEPTH + 1e-5) return Infinity;
    count++;
  }
  return count ? SHALLOW_STATIC_DEPTH : Infinity;
}
export {waterVertexLevel} from './planet-macro/water-geometry.mjs';

export function isWaterCascade(x: number, z: number, height: HeightSampler, water: HeightSampler): boolean {
  const level = water(x, z);
  return Number.isFinite(level) && height(x, z) < level && [[1, 0], [-1, 0], [0, 1], [0, -1]].some(([dx, dz]) =>
    height(x + dx, z + dz) < water(x + dx, z + dz) && Math.abs(level - water(x + dx, z + dz)) > 2);
}

/** 与水面网格相同的三角采样 */
function cellWaterHeight(x: number, z: number, ix: number, iz: number, seed: number, height: HeightSampler, water: HeightSampler, seaFallback = true, shallow?: ShallowSampler, offset = WATER_OFFSET): number {
  const level = water(ix - .5, iz - .5);
  let result: number;
  {
    const vertices = [[ix, iz], [ix, iz + 1], [ix + 1, iz + 1], [ix + 1, iz]]
      .map(([px, pz]) => waterVertexLevel(px, pz, height, water, seed, seaFallback, offset));
    const finite = vertices.filter(Number.isFinite);
    if (!finite.length) return -Infinity;
    const fallback = Number.isFinite(level) && height(ix - .5, iz - .5) < level ? level - offset : Math.min(...finite);
    const [a, b, c, d] = vertices.map(value => Number.isFinite(value) ? value : fallback);
    result = triangleHeight(x - ix, z - iz, a, b, c, d, terrainDiagonal(ix, iz, seed));
  }
  const depth = result - terrainHeightAt(x, z, seed, height);
  const limit = shallow ? shallowCellLimit(ix - .5, iz - .5, height, water, shallow) : Infinity;
  return depth > 0 && depth <= limit + 1e-5 ? result : -Infinity;
}

export function waterHeightAt(x: number, z: number, seed: number, height: HeightSampler, water: HeightSampler, seaFallback = true, shallow?: ShallowSampler, offset = WATER_OFFSET): number {
  const ix = Math.floor(x - .5) + .5, iz = Math.floor(z - .5) + .5;
  let result = cellWaterHeight(x, z, ix, iz, seed, height, water, seaFallback, shallow, offset);
  // 格线上的点也属于左/上侧的面
  const xs = x === ix ? [ix, ix - 1] : [ix], zs = z === iz ? [iz, iz - 1] : [iz];
  for (const px of xs) for (const pz of zs) {
    if (px === ix && pz === iz) continue;
    result = Math.max(result, cellWaterHeight(x, z, px, pz, seed, height, water, seaFallback, shallow, offset));
  }
  return result;
}

export {iceVertexLevel,iceVertexCoverage,clipIce} from './planet-macro/water-geometry.mjs';
export type {IceVertex} from './planet-macro/water-geometry.mjs';

export function iceHeightAt(x: number, z: number, seed: number, ice: HeightSampler, coverage?: HeightSampler): number {
  const ix=Math.floor(x-.5)+.5,iz=Math.floor(z-.5)+.5;
  let result=-Infinity;
  for(const px of x===ix ? [ix,ix-1] : [ix]) for(const pz of z===iz ? [iz,iz-1] : [iz]) {
    if(!coverage&&!Number.isFinite(ice(px-.5,pz-.5)))continue;
    const vertices=[[px,pz],[px,pz+1],[px+1,pz+1],[px+1,pz]].map(([vx,vz])=>iceVertexLevel(vx,vz,ice));
    const finite=vertices.filter(Number.isFinite);
    if(!finite.length)continue;
    const fallback=Math.min(...finite);
    const [a,b,c,d]=vertices.map(v=>Number.isFinite(v)?v:fallback);
    const amounts=[[px,pz],[px,pz+1],[px+1,pz+1],[px+1,pz]].map(([vx,vz])=>iceVertexCoverage(vx,vz,coverage??(()=>1)));
    const flip=terrainDiagonal(px,pz,seed);
    if(triangleHeight(x-px,z-pz,...amounts as [number,number,number,number],flip)<.5) continue;
    result=Math.max(result,triangleHeight(x-px,z-pz,a,b,c,d,flip));
  }
  return result;
}

