/** 区块生成 Worker */
import { buildChunkMeshes, type MeshData } from './ChunkMesher';
import type { ChunkResultMessage, WorkerRequest } from './ChunkProtocol';
import { WorldGenerator } from './WorldGenerator';
import { buildDecorationMesh, decorationPlacements } from './WorldDecoration';

const ctx = self as unknown as Worker;
let generator: WorldGenerator | null = null;

function pushBuffers(mesh: MeshData | null, out: Transferable[]): void {
  if (!mesh) return;
  out.push(mesh.positions.buffer, mesh.colors.buffer, mesh.indices.buffer);
  if (mesh.waterDepths) out.push(mesh.waterDepths.buffer);
  if (mesh.waterFlows) out.push(mesh.waterFlows.buffer);
  if (mesh.waterFalls) out.push(mesh.waterFalls.buffer);
}

ctx.onmessage = (e: MessageEvent<WorkerRequest>) => {
  const msg = e.data;
  if (msg.kind === 'init') {
    generator = new WorldGenerator(msg.seed, msg.climateWeights, msg.generation);
    return;
  }
  if (msg.kind === 'generate') {
    if (!generator) throw new Error('[chunk.worker] 未初始化');
    const t0 = performance.now();
    const chunk = generator.generateChunk(msg.cx, msg.cz);
    const meshes = buildChunkMeshes(msg.cx, msg.cz, chunk.heights, chunk.surfaces, generator.seed, chunk.waterLevels, chunk.surfaceColors,chunk.waterField);
    const decorationInput = { ...chunk, cx: msg.cx, cz: msg.cz,
      artificialType: msg.artificialType,
      seed: generator.seed, temperature: generator.getClimate(msg.cx, msg.cz).temperature };
    const placements = decorationPlacements(decorationInput);
    const decoration = buildDecorationMesh(decorationInput, placements.filter(p => p.kind === 'plant'));
    const rocks = buildDecorationMesh(decorationInput, placements.filter(p => p.kind === 'rock'));

    // 主线程只需要区块内部的高度（去掉外扩一圈）
    const S = Math.sqrt(chunk.surfaces.length);
    const P = S + 2;
    const inner = new Float32Array(S * S);
    const waterLevels = new Float32Array(S * S);
    const kinds=new Uint8Array(S*S),velocities=new Float32Array(S*S*2),discharge=new Float32Array(S*S);
    for (let z = 0; z < S; z++) {
      inner.set(chunk.heights.subarray((z + 1) * P + 1, (z + 1) * P + 1 + S), z * S);
      waterLevels.set(chunk.waterLevels.subarray((z + 1) * P + 1, (z + 1) * P + 1 + S), z * S);
      kinds.set(chunk.waterField.kinds.subarray((z+1)*P+1,(z+1)*P+1+S),z*S);
      discharge.set(chunk.waterField.discharge.subarray((z+1)*P+1,(z+1)*P+1+S),z*S);
      velocities.set(chunk.waterField.velocities.subarray(((z+1)*P+1)*2,((z+1)*P+1+S)*2),z*S*2);
    }

    const result: ChunkResultMessage = {
      kind: 'chunk',
      cx: msg.cx,
      cz: msg.cz,
      type: chunk.type,
      heights: inner,
      surfaces: chunk.surfaces,
      waterLevels,
      waterField:{levels:waterLevels,kinds,velocities,discharge},
      terrain: meshes.terrain,
      water: meshes.water,
      decoration,
      rocks,
      minimap: meshes.minimap,
      ...(meshes.mist ? { mist: meshes.mist } : {}),
      elapsed: performance.now() - t0,
    };
    const transfer: Transferable[] = [inner.buffer, waterLevels.buffer, kinds.buffer,velocities.buffer,discharge.buffer,chunk.surfaces.buffer, meshes.minimap.buffer];
    if (meshes.mist) transfer.push(meshes.mist.buffer);
    pushBuffers(meshes.terrain, transfer);
    pushBuffers(meshes.water, transfer);
    pushBuffers(decoration, transfer);
    pushBuffers(rocks, transfer);
    ctx.postMessage(result, transfer);
  }
};
