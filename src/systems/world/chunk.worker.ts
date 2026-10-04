/**
 * 区块生成 Worker：生成高度图 + 构建网格，结果以 Transferable 方式回传主线程。
 */
import { buildChunkMeshes, type MeshData } from './ChunkMesher';
import type { ChunkResultMessage, WorkerRequest } from './ChunkProtocol';
import { WorldGenerator } from './WorldGenerator';

const ctx = self as unknown as Worker;
let generator: WorldGenerator | null = null;

function pushBuffers(mesh: MeshData | null, out: Transferable[]): void {
  if (!mesh) return;
  out.push(mesh.positions.buffer, mesh.colors.buffer, mesh.indices.buffer);
}

ctx.onmessage = (e: MessageEvent<WorkerRequest>) => {
  const msg = e.data;
  if (msg.kind === 'init') {
    generator = new WorldGenerator(msg.seed, msg.climateWeights);
    return;
  }
  if (msg.kind === 'generate') {
    if (!generator) throw new Error('[chunk.worker] 未初始化');
    const t0 = performance.now();
    const chunk = generator.generateChunk(msg.cx, msg.cz);
    const meshes = buildChunkMeshes(msg.cx, msg.cz, chunk.heights, chunk.surfaces, generator.seed, chunk.waterLevels, chunk.surfaceColors);

    // 主线程只需要区块内部的高度（去掉外扩一圈）
    const S = Math.sqrt(chunk.surfaces.length);
    const P = S + 2;
    const inner = new Int16Array(S * S);
    const waterLevels = new Float32Array(S * S);
    for (let z = 0; z < S; z++) {
      inner.set(chunk.heights.subarray((z + 1) * P + 1, (z + 1) * P + 1 + S), z * S);
      waterLevels.set(chunk.waterLevels.subarray((z + 1) * P + 1, (z + 1) * P + 1 + S), z * S);
    }

    const result: ChunkResultMessage = {
      kind: 'chunk',
      cx: msg.cx,
      cz: msg.cz,
      type: chunk.type,
      heights: inner,
      surfaces: chunk.surfaces,
      waterLevels,
      terrain: meshes.terrain,
      water: meshes.water,
      minimap: meshes.minimap,
      elapsed: performance.now() - t0,
    };
    const transfer: Transferable[] = [inner.buffer, waterLevels.buffer, chunk.surfaces.buffer, meshes.minimap.buffer];
    pushBuffers(meshes.terrain, transfer);
    pushBuffers(meshes.water, transfer);
    ctx.postMessage(result, transfer);
  }
};
