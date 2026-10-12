import { terrainDiagonal, terrainHeightAt, triangleHeight, type HeightSampler } from './TerrainSurface';
import { SEA_LEVEL } from '../../core/config';

export const WATER_OFFSET = .12;
export function waterVertexLevel(x: number, z: number, height: HeightSampler, water: HeightSampler, seed: number, seaFallback = true): number {
  let total = 0, count = 0;
  for (const [dx, dz] of [[-1, -1], [0, -1], [-1, 0], [0, 0]]) {
    const level = water(x + dx, z + dz);
    if (Number.isFinite(level) && height(x + dx, z + dz) < level) { total += level; count++; }
  }
  if (count) return Math.fround(total / count - WATER_OFFSET);
  // 低模坡面可能在原始干格中穿过海平面
  return seaFallback && terrainHeightAt(x, z, seed, height) < SEA_LEVEL - WATER_OFFSET ? Math.fround(SEA_LEVEL - WATER_OFFSET) : -Infinity;
}

export function isWaterCascade(x: number, z: number, height: HeightSampler, water: HeightSampler): boolean {
  const level = water(x, z);
  return Number.isFinite(level) && height(x, z) < level && [[1, 0], [-1, 0], [0, 1], [0, -1]].some(([dx, dz]) =>
    height(x + dx, z + dz) < water(x + dx, z + dz) && Math.abs(level - water(x + dx, z + dz)) > 2);
}

/** 与水面网格相同的三角采样 */
function cellWaterHeight(x: number, z: number, ix: number, iz: number, seed: number, height: HeightSampler, water: HeightSampler, seaFallback = true): number {
  const level = water(ix, iz);
  let result: number;
  {
    const vertices = [[ix, iz], [ix, iz + 1], [ix + 1, iz + 1], [ix + 1, iz]]
      .map(([px, pz]) => waterVertexLevel(px, pz, height, water, seed, seaFallback));
    const finite = vertices.filter(Number.isFinite);
    if (!finite.length) return -Infinity;
    const fallback = Number.isFinite(level) && height(ix, iz) < level ? level - WATER_OFFSET : Math.min(...finite);
    const [a, b, c, d] = vertices.map(value => Number.isFinite(value) ? value : fallback);
    result = triangleHeight(x - ix, z - iz, a, b, c, d, terrainDiagonal(ix, iz, seed));
  }
  return terrainHeightAt(x, z, seed, height) < result ? result : -Infinity;
}

export function waterHeightAt(x: number, z: number, seed: number, height: HeightSampler, water: HeightSampler, seaFallback = true): number {
  const ix = Math.floor(x), iz = Math.floor(z);
  let result = cellWaterHeight(x, z, ix, iz, seed, height, water, seaFallback);
  // 格线上的点也属于左/上侧的面
  const xs = x === ix ? [ix, ix - 1] : [ix], zs = z === iz ? [iz, iz - 1] : [iz];
  for (const px of xs) for (const pz of zs) {
    if (px === ix && pz === iz) continue;
    result = Math.max(result, cellWaterHeight(x, z, px, pz, seed, height, water, seaFallback));
  }
  return result;
}

export function iceVertexLevel(x: number, z: number, ice: HeightSampler): number {
  let sum=0,count=0;
  for(const [dx,dz] of [[-1,-1],[0,-1],[-1,0],[0,0]]) {
    const value=ice(x+dx,z+dz);
    if(Number.isFinite(value)) {sum+=value;count++;}
  }
  return count ? sum/count : -Infinity;
}

export function iceHeightAt(x: number, z: number, seed: number, ice: HeightSampler, coverage?: HeightSampler): number {
  const ix=Math.floor(x),iz=Math.floor(z);
  let result=-Infinity;
  for(const px of x===ix ? [ix,ix-1] : [ix]) for(const pz of z===iz ? [iz,iz-1] : [iz]) {
    if(!coverage&&!Number.isFinite(ice(px,pz)))continue;
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

export function iceVertexCoverage(x: number, z: number, coverage: HeightSampler): number {
  let sum=0;
  for(const [dx,dz] of [[-1,-1],[0,-1],[-1,0],[0,0]])sum+=coverage(x+dx,z+dz);
  return sum*.25;
}

export interface IceVertex {x:number;y:number;z:number;depth:number;coverage:number;bottom:number}
export function clipIce(vertices: IceVertex[], field: 'coverage' | 'depth'): IceVertex[] {
  const out:IceVertex[]=[],threshold=field==='coverage'?.5:0;
  for(let i=0;i<vertices.length;i++) {
    const a=vertices[i],b=vertices[(i+1)%vertices.length],av=a[field]-threshold,bv=b[field]-threshold;
    if(av>=0)out.push(a);
    if((av>=0)!==(bv>=0)) {
      const t=av/(av-bv),point={} as IceVertex;
      for(const key of ['x','y','z','depth','coverage','bottom'] as const)point[key]=a[key]+(b[key]-a[key])*t;
      out.push(point);
    }
  }
  return out;
}
