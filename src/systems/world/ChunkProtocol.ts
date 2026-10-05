/**
 * 主线程 <-> 区块 Worker 消息协议。
 */
import type { ClimateWeights, WorldGeneration } from './WorldSettings';
import type { MeshData } from './ChunkMesher';

export type WorkerRequest =
  | { kind: 'init'; seed: number; climateWeights: ClimateWeights; generation: WorldGeneration }
  | { kind: 'generate'; cx: number; cz: number };

export interface ChunkResultMessage {
  kind: 'chunk';
  cx: number;
  cz: number;
  type: number;
  heights: Float32Array;
  surfaces: Uint8Array;
  waterLevels: Float32Array;
  terrain: MeshData | null;
  water: MeshData | null;
  minimap: Uint8ClampedArray<ArrayBuffer>;
  /** 生成 + 构建网格耗时（毫秒） */
  elapsed: number;
}

export type WorkerResponse = ChunkResultMessage;
