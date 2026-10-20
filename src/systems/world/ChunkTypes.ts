/** 区块类型注册表 */
import chunkTypeDefs from '../../../content/data/world/chunk_types.json';
import macroDefs from '../../../content/data/world/macro_chunk_types.json';

export interface ChunkTypeDef {
  id: number;
  code: string;
  key: string;
  name: string;
  mapColor: string;
  generate: boolean;
  artificial?: boolean;
  description: string;
  macro?:boolean;
}

export const CHUNK_TYPES: readonly ChunkTypeDef[] = (chunkTypeDefs as ChunkTypeDef[]).sort((a, b) => a.id - b.id);

const chunkTypeMap = new Map<number, ChunkTypeDef>();
CHUNK_TYPES.forEach(def => chunkTypeMap.set(def.id, def));

export const MACRO_CHUNK_TYPES:readonly ChunkTypeDef[]=macroDefs.map(d=>({...d,key:'macro_'+d.key,description:d.name,macro:true}));
const macroTypeMap=new Map(MACRO_CHUNK_TYPES.map(d=>[d.id,d]));
export const getChunkTypes=(macro=false):readonly ChunkTypeDef[]=>macro?[...MACRO_CHUNK_TYPES,...CHUNK_TYPES.filter(d=>d.artificial)]:CHUNK_TYPES;
export function getChunkTypeDef(id: number,macro=false): ChunkTypeDef {
  return (macro&&id<9000?macroTypeMap.get(id):chunkTypeMap.get(id)) ?? chunkTypeMap.get(0)!;
}

/** 将区块类型 ID 格式化为统一的三位数（如 00 */
export function formatChunkId(id: number): string {
  const def = chunkTypeMap.get(id);
  if (def?.code) return def.code;
  return String(id).padStart(3, '0');
}

export const isRiverType = (type: number,macro=false): boolean => macro?type===4||(type>=5&&type<100&&type%10===5):type===5||type===10||(type>=15&&type<100&&type%10===5);
