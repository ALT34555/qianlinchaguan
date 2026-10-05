/**
 * 区块类型注册表，定义来自 content/data/world/chunk_types.json
 */
import chunkTypeDefs from '../../../content/data/world/chunk_types.json';

export interface ChunkTypeDef {
  id: number;
  code: string;
  key: string;
  name: string;
  mapColor: string;
  generate: boolean;
  description: string;
}

export const CHUNK_TYPES: readonly ChunkTypeDef[] = (chunkTypeDefs as ChunkTypeDef[]).sort((a, b) => a.id - b.id);

const chunkTypeMap = new Map<number, ChunkTypeDef>();
CHUNK_TYPES.forEach(def => chunkTypeMap.set(def.id, def));

export function getChunkTypeDef(id: number): ChunkTypeDef {
  return chunkTypeMap.get(id) ?? chunkTypeMap.get(0)!;
}

/** 将区块类型 ID 格式化为统一的三位数（如 001、102、409） */
export function formatChunkId(id: number): string {
  const def = chunkTypeMap.get(id);
  if (def?.code) return def.code;
  return String(id).padStart(3, '0');
}

export const isRiverType = (type: number): boolean => type === 5 || type === 10;
