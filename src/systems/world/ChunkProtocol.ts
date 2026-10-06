/**
 * 主线程 <-> 区块 Worker 消息协议。
 */
import type { ClimateWeights, WorldGeneration } from './WorldSettings';
import type { MeshData } from './ChunkMesher';
import type {WaterField} from './DynamicWater';

export type WorkerRequest =
  | { kind: 'init'; seed: number; climateWeights: ClimateWeights; generation: WorldGeneration }
  | { kind: 'generate'; cx: number; cz: number };

export interface ChunkResultMessage {
  kind: 'chunk';
  cx: number;
  cz: number;
  type: number;
  heights: Float32Array;
  surfaces: Uint32Array;
  waterLevels: Float32Array;
  waterField: WaterField;
  terrain: MeshData | null;
  water: MeshData | null;
  decoration: MeshData | null;
  minimap: Uint8ClampedArray<ArrayBuffer>;
  mist?: Float32Array;
  /** 生成 + 构建网格耗时（毫秒） */
  elapsed: number;
}

export type WorkerResponse = ChunkResultMessage;
