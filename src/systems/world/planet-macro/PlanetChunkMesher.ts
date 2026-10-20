import {CHUNK_SIZE} from '../../../core/config';
import {buildChunkMeshes,type ChunkMeshes,type MeshData} from '../ChunkMesher';
import {PADDED_SIZE,type ChunkGenResult} from '../WorldContract';
import {terrainHeightAt} from '../TerrainSurface';
import {gridBuffers,finishGrid} from './patch.mjs';
import {patchWaterVertex} from './PlanetPatchWater';

export function buildPlanetChunkMeshes(chunk:ChunkGenResult,seed:number):ChunkMeshes {
  const {cx,cz,heights,surfaces,surfaceColors,waterField}=chunk,S=CHUNK_SIZE,P=PADDED_SIZE;
  const dry=new Float32Array(P*P).fill(-Infinity);
  const meshes=buildChunkMeshes(cx,cz,heights,surfaces,seed,dry,surfaceColors,{...waterField,levels:dry});
  const ox=cx*S,oz=cz*S,bed=(x:number,z:number)=>heights[(Math.floor(z)-oz+1)*P+Math.floor(x)-ox+1];
  const liquid=(x:number,z:number)=>waterField.levels[(Math.floor(z)-oz+1)*P+Math.floor(x)-ox+1];
  const buffers=gridBuffers(S+1);
  for(let z=0;z<=S;z++)for(let x=0;x<=S;x++){
    const i=z*(S+1)+x,j=(z+1)*P+x+1;
    buffers.positions.set([x,terrainHeightAt(ox+x,oz+z,seed,bed),z],i*3);
    buffers.levels[i]=patchWaterVertex(ox+x,oz+z,seed,bed,liquid);
    buffers.kinds[i]=waterField.kinds[j];
  }
  const patch=finishGrid(cx,cz,S,1,buffers,0,0,performance.now());
  if(!patch.waterIndices.length){meshes.water=null;return meshes;}
  const count=patch.waterPositions.length/3,colors=new Uint8Array(count*3),depths=new Float32Array(count),flows=new Float32Array(count*2),foams=new Uint8Array(count*3);
  const palette=waterField.appearance!.colors;
  for(let i=0;i<count;i++){
    const x=patch.waterPositions[i*3],z=patch.waterPositions[i*3+2],j=(Math.min(S-1,Math.floor(z))+1)*P+Math.min(S-1,Math.floor(x))+1;
    const depth=patch.waterPositions[i*3+1]-terrainHeightAt(ox+x,oz+z,seed,bed),t=Math.min(1,Math.max(0,depth)/14);
    depths[i]=depth;flows[i*2]=waterField.velocities[j*2];flows[i*2+1]=waterField.velocities[j*2+1];
    for(let c=0;c<3;c++){colors[i*3+c]=palette.shallow[c]+(palette.deep[c]-palette.shallow[c])*t;foams[i*3+c]=palette.foam[c];}
  }
  const water:MeshData={positions:patch.waterPositions,indices:patch.waterIndices,colors,waterDepths:depths,waterFlows:flows,waterFoams:foams,waterFalls:new Float32Array(count)};
  meshes.water=water;
  return meshes;
}
