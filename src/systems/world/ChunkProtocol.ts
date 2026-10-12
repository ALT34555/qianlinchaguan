import type { WaterFreezeRules } from './WaterFreezing';
import type { WaterAppearanceEntry } from './WaterAppearance';
/**
 * 主线程 <-> 区块 Worker 消息协议。
 */
import type { ClimateWeights, WorldGeneration } from './WorldSettings';
import type { MeshData } from './ChunkMesher';
import type {WaterField} from './DynamicWater';
import type { DecorationPlacement } from './WorldDecoration';
import type { WatershedSamples, WatershedState } from './Watershed';
import type { DecorationBatch } from './DecorationInstances';

export type WorkerRequest =
  | { kind:'budget'; entries:number; meshBytes:number }
  | { kind: 'flush'; id: number }
  | { kind: 'init'; seed: number; climateWeights: ClimateWeights; generation: WorldGeneration; worldKey?:string; hydrology?:WatershedState }
  | { kind: 'coarse'; samples: WatershedSamples }
  | { kind: 'generate'; cx: number; cz: number; artificialType?: number; waterOverrides?: WaterAppearanceEntry[]; waterRevision?: number; waterFreezeRules?: WaterFreezeRules };

/** 舆图预览请求：image 为假时只回平均色，省一次像素图分配与回传 */
export type SurfacePreviewRequest =
  | {kind:'budget';entries:number}
  | { kind: 'init'; seed: number; climateWeights: ClimateWeights; generation: WorldGeneration; worldKey?:string; hydrology?:WatershedState }
  | { kind: 'generate'; cx: number; cz: number; image?: boolean; detail?:boolean; cachedOnly?:boolean };

export interface ChunkResultMessage {
  meshCacheHit?:boolean;
  kind: 'chunk';
  waterRevision?: number;
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
  rocks?: MeshData | null;
  /** 装饰落位（世界坐标），供准心检视 */
  decorations: DecorationPlacement[];
  minimap: Uint8ClampedArray<ArrayBuffer>;
  mist?: Float32Array;
  /** 生成 + 构建网格耗时（毫秒） */
  elapsed: number;
  cacheHit?: boolean;
  instances?:DecorationBatch[];
}

export type WorkerResponse = ChunkResultMessage;
