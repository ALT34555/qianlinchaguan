import {terrainHeightAt,triangleHeight,type HeightSampler} from '../TerrainSurface';

export function patchWaterVertex(x:number,z:number,seed:number,bed:HeightSampler,water:HeightSampler):number {
  let sum=0,count=0;
  for(const [dx,dz] of [[-1,-1],[0,-1],[-1,0],[0,0]]){
    const level=water(x+dx,z+dz);
    if(Number.isFinite(level)&&bed(x+dx,z+dz)<level){sum+=level;count++;}
  }
  const level=count?Math.fround(sum/count):-Infinity;
  return level>terrainHeightAt(x,z,seed,bed)?level:-Infinity;
}

export function patchWaterHeight(x:number,z:number,seed:number,bed:HeightSampler,water:HeightSampler):number {
  const ix=Math.floor(x),iz=Math.floor(z);
  const cell=(cx:number,cz:number)=>{
    const levels=[[cx,cz],[cx,cz+1],[cx+1,cz+1],[cx+1,cz]].map(([px,pz])=>patchWaterVertex(px,pz,seed,bed,water));
    if(!levels.every(Number.isFinite))return -Infinity;
    return triangleHeight(x-cx,z-cz,levels[0],levels[1],levels[2],levels[3],true)+.02;
  };
  let level=cell(ix,iz);
  if(x===ix)level=Math.max(level,cell(ix-1,iz));
  if(z===iz)level=Math.max(level,cell(ix,iz-1));
  if(x===ix&&z===iz)level=Math.max(level,cell(ix-1,iz-1));
  return level>terrainHeightAt(x,z,seed,bed)?level:-Infinity;
}
