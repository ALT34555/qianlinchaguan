import {macroDecorationType} from './planet-macro/DecorationType';
import { createWorldGenerator } from './GeneratorFactory';
/** 区块生成 Worker */
import { buildChunkMeshes, type MeshData } from './ChunkMesher';
import {buildPlanetChunkMeshes} from './planet-macro/PlanetChunkMesher';
import type { ChunkResultMessage, WorkerRequest } from './ChunkProtocol';
import type { WorldGeneratorBase as WorldGenerator } from './WorldContract';
import { decorationPlacements } from './WorldDecoration';
import { CHUNK_SIZE } from '../../core/config';
import * as THREE from 'three';
import { ChunkCache, chunkSignature, minimapSummary } from './ChunkCache';
import { decorationInstances } from './DecorationInstances';
import { DerivedChunkCache } from './DerivedChunkCache';

const ctx = self as unknown as Worker;
let generator: WorldGenerator | null = null;
let cache:ChunkCache|null=null;
let queue=Promise.resolve();
const derived=new DerivedChunkCache();

function transferResult(result:ChunkResultMessage):Transferable[] {
  const buffers=new Set<ArrayBuffer>();
  const visit=(item:unknown):void=>{
    if(ArrayBuffer.isView(item)){if(item.buffer instanceof ArrayBuffer)buffers.add(item.buffer);return;}
    if(item&&typeof item==='object')for(const child of Object.values(item))visit(child);
  };
  visit(result);return [...buffers];
}

function pushBuffers(mesh: MeshData | null, out: Transferable[]): void {
  if (!mesh) return;
  if (!mesh.normals) {
    const geometry=new THREE.BufferGeometry();
    geometry.setAttribute('position',new THREE.BufferAttribute(mesh.positions,3));
    geometry.setIndex(new THREE.BufferAttribute(mesh.indices,1));
    geometry.computeVertexNormals();
    mesh.normals=geometry.getAttribute('normal').array as Float32Array;
    geometry.dispose();
  }
  out.push(mesh.normals.buffer);
  out.push(mesh.positions.buffer, mesh.colors.buffer, mesh.indices.buffer);
  if (mesh.plantParts) out.push(mesh.plantParts.buffer);
  if (mesh.waterDepths) out.push(mesh.waterDepths.buffer);
  if (mesh.waterFlows) out.push(mesh.waterFlows.buffer);
  if (mesh.waterFoams) out.push(mesh.waterFoams.buffer);
  if (mesh.waterFalls) out.push(mesh.waterFalls.buffer);
}

async function handle(e: MessageEvent<WorkerRequest>):Promise<void> {
  const msg = e.data;
  if(msg.kind==='budget'){if(generator)generator.cacheLimit=msg.entries;derived.setLimit(msg.meshBytes);return;}
  if (msg.kind === 'flush') {
    await cache?.flush();
    ctx.postMessage({kind:'flushed',id:msg.id});
    return;
  }
  if (msg.kind === 'init') {
    generator = createWorldGenerator(msg.seed, msg.climateWeights, msg.generation,{state:msg.hydrology});
    cache=msg.worldKey?new ChunkCache(msg.worldKey):null;
    return;
  }
  if (msg.kind === 'generate') {
    if (!generator) throw new Error('[chunk.worker] 未初始化');
    if (msg.waterFreezeRules) generator.setWaterFreezeRules(msg.waterFreezeRules);
    generator.replaceWaterAppearanceOverrides(msg.waterOverrides ?? []);
    generator.setWaterSeason(msg.waterSeason??null);
    const t0 = performance.now();
    const signature=chunkSignature(generator);
    const meshKey=JSON.stringify([signature,msg.cx,msg.cz,msg.artificialType,msg.vegetationMultiplier ?? 1]);
    const reused=derived.get(meshKey);
    if(reused){reused.waterRevision=msg.waterRevision;reused.elapsed=performance.now()-t0;reused.cacheHit=true;reused.meshCacheHit=true;ctx.postMessage(reused,transferResult(reused));return;}
    const stored=await cache?.chunk(msg.cx,msg.cz,signature);
    const chunk = stored??generator.generateChunk(msg.cx, msg.cz);
    const waterGenerator = generator;
    const meshes = generator.generation.terrainRule==='planet-macro'?buildPlanetChunkMeshes(chunk,generator.seed):buildChunkMeshes(msg.cx, msg.cz, chunk.heights, chunk.surfaces, generator.seed, chunk.waterLevels, chunk.surfaceColors,chunk.waterField,
      (x, z) => waterGenerator.getWaterState(msg.cx * CHUNK_SIZE + x, msg.cz * CHUNK_SIZE + z));
    const decorationInput = { ...chunk, type:generator.generation.terrainRule==='planet-macro'?macroDecorationType(chunk.type):chunk.type, cx: msg.cx, cz: msg.cz,
      artificialType: msg.artificialType,
      vegetationMultiplier: msg.vegetationMultiplier,
      seed: generator.seed, temperature: generator.getClimate(msg.cx, msg.cz).temperature };
    const placements = decorationPlacements(decorationInput);
    const collisionData:number[]=[];
    const instances=decorationInstances(decorationInput,placements,collisionData);
    const colliders=new Float32Array(collisionData);
    const decoration=null,rocks=null;
    const summary=minimapSummary(generator,msg.cx,msg.cz,meshes.minimap);
    if(cache){
      cache.queueWrite(`preview_${msg.cx}_${msg.cz}`,signature,summary);
      if(!stored)cache.queueWrite(`chunk_${msg.cx}_${msg.cz}`,signature,chunk,8);
    }

    // 主线程只需要区块内部的高度（去掉外扩一圈）
    const S = Math.sqrt(chunk.surfaces.length);
    const P = S + 2;
    const inner = new Float32Array(S * S);
    const waterLevels = new Float32Array(S * S);
    const bedLevels=new Float32Array(S*S),iceLevels=new Float32Array(S*S),iceBottomLevels=new Float32Array(S*S),iceCoverages=new Float32Array(S*S);
    const kinds=new Uint8Array(S*S),velocities=new Float32Array(S*S*2),discharge=new Float32Array(S*S);
    for (let z = 0; z < S; z++) {
      inner.set(chunk.heights.subarray((z + 1) * P + 1, (z + 1) * P + 1 + S), z * S);
      waterLevels.set(chunk.waterLevels.subarray((z + 1) * P + 1, (z + 1) * P + 1 + S), z * S);
      for (const [target,source] of [[iceCoverages,chunk.waterField.iceCoverages!],[bedLevels,chunk.waterField.bedLevels!],[iceLevels,chunk.waterField.iceLevels!],[iceBottomLevels,chunk.waterField.iceBottomLevels!]]) {
        target.set(source.subarray((z+1)*P+1,(z+1)*P+1+S),z*S);
      }
      kinds.set(chunk.waterField.kinds.subarray((z+1)*P+1,(z+1)*P+1+S),z*S);
      discharge.set(chunk.waterField.discharge.subarray((z+1)*P+1,(z+1)*P+1+S),z*S);
      velocities.set(chunk.waterField.velocities.subarray(((z+1)*P+1)*2,((z+1)*P+1+S)*2),z*S*2);
    }

    const result: ChunkResultMessage = {
      kind: 'chunk',
      waterRevision: msg.waterRevision,
      cx: msg.cx,
      cz: msg.cz,
      type: chunk.type,
      heights: inner,
      surfaces: chunk.surfaces,
      waterLevels,
      waterField:{appearance:chunk.waterField.appearance,levels:waterLevels,bedLevels,iceLevels,iceBottomLevels,iceCoverages,kinds,velocities,discharge},
      terrain: meshes.terrain,
      water: meshes.water,
      decoration,
      rocks,
      decorations: placements,
      minimap: meshes.minimap,
      ...(meshes.mist ? { mist: meshes.mist } : {}),
      elapsed: 0,
      cacheHit:!!stored,
      instances,
      colliders,
    };
    const transfer: Transferable[] = [inner.buffer, bedLevels.buffer,iceLevels.buffer,iceBottomLevels.buffer,iceCoverages.buffer,waterLevels.buffer, kinds.buffer,velocities.buffer,discharge.buffer,chunk.surfaces.buffer, meshes.minimap.buffer];
    if (meshes.mist) transfer.push(meshes.mist.buffer);
    transfer.push(colliders.buffer);
    pushBuffers(meshes.terrain, transfer);
    pushBuffers(meshes.water, transfer);
    pushBuffers(decoration, transfer);
    pushBuffers(rocks, transfer);
    for(const batch of instances){pushBuffers(batch.geometry,transfer);transfer.push(batch.transforms.buffer);}
    result.elapsed = performance.now() - t0;
    derived.put(meshKey,result);
    ctx.postMessage(result, transfer);
  }
}
ctx.onmessage=(e:MessageEvent<WorkerRequest>)=>{
  queue=queue.then(()=>handle(e)).catch(error=>{
    console.error('[Chunk worker]',error);
    if(e.data.kind==='generate')ctx.postMessage({kind:'error',cx:e.data.cx,cz:e.data.cz,message:String(error)});
  });
};
