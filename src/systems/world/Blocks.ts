/** 方块注册表 */
import blockDefs from '../../../content/data/world/blocks.json';

export const Block = {
  AIR: 0,
  STONE: 1,
  DIRT: 2,
  GRASS: 3,
  SAND: 4,
  GRAVEL: 5,
  DRY_DIRT: 6,
  SNOW: 7,
  WATER: 8,
  BEDROCK: 9,
  MUD: 10,
  FOREST_SOIL: 11,
  GRANITE: 12, BASALT: 13, LIMESTONE: 14, SANDSTONE: 15, SHALE: 16, SLATE: 17,
  QUARTZITE: 18, CONGLOMERATE: 19, CLAY: 20, SILT: 21, LOESS: 22, PEAT: 23,
  LATERITE: 24, SCREE: 25, CHALK: 26, TRAVERTINE: 27, RED_SAND: 28, DARK_SAND: 29,
  GLACIAL_TILL: 30, ICE: 31, MOSS_SOIL: 32, PODZOL: 33, ALLUVIUM: 34, BRECCIA: 35,
} as const;

/** 包括五位专用ID；所有传输数组必须保留32位精度。 */
export type BlockId = number;

export interface BlockDef {
  id: number;
  key: string;
  name: string;
  /** 纯色面颜色（#rrggbb），空气为 null */
  color: string | null;
  /** 表层以下的填充方块 key（如草方块下面是泥土） */
  filler: string;
  base: string;
  category: 'air' | 'rock' | 'soil' | 'sediment' | 'ice' | 'static_fluid';
  erodibility: number;
  permeability: number;
  code?: string;
  chunkType?: number;
  slot?: number;
}

const MAX_BLOCKS = 100000;
export const BLOCK_DEFS: readonly BlockDef[] = blockDefs as BlockDef[];
const defs = BLOCK_DEFS;
const byKey = new Map<string, BlockDef>(defs.map((d) => [d.key, d]));
if (byKey.size !== defs.length) throw new Error('方块 key 重复');
const byId = new Map<number, BlockDef>();
export const SPECIAL_BLOCKS = new Map<number, readonly BlockDef[]>();

/** 方块颜色表 */
export const BLOCK_RGB = new Uint8Array(MAX_BLOCKS * 3);
/** 填充方块表 */
export const BLOCK_FILLER = new Uint32Array(MAX_BLOCKS);
export const BLOCK_BASE = new Uint32Array(MAX_BLOCKS);

function parseHex(hex: string): [number, number, number] {
  const v = parseInt(hex.slice(1), 16);
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
}

for (const [constName, id] of Object.entries(Block)) {
  const def = defs.find((d) => d.id === id);
  if (!def || def.key !== constName.toLowerCase()) {
    throw new Error(`[Blocks] blocks.json 与 Block 常量不一致: ${constName}=${id}`);
  }
}

for (const def of defs) {
  if (!Number.isInteger(def.id) || def.id < 0 || def.id >= MAX_BLOCKS || byId.has(def.id)) throw new Error(`方块ID非法或重复: ${def.id}`);
  if (!byKey.has(def.filler) || !byKey.has(def.base)) throw new Error(`方块母材/填充层不存在: ${def.key}`);
  if (def.chunkType !== undefined) {
    if (!def.slot || def.slot > 3 || def.id !== def.chunkType * 100 + def.slot || def.code !== String(def.id).padStart(5,'0')) throw new Error(`专用方块编码无效: ${def.key}`);
    const group = [...(SPECIAL_BLOCKS.get(def.chunkType) ?? []), def];
    if (group.length > 3) throw new Error(`地质区块专用方块超过3种: ${def.chunkType}`);
    SPECIAL_BLOCKS.set(def.chunkType, group);
  }
  byId.set(def.id, def);
  if (def.color) {
    const [r, g, b] = parseHex(def.color);
    BLOCK_RGB[def.id * 3] = r;
    BLOCK_RGB[def.id * 3 + 1] = g;
    BLOCK_RGB[def.id * 3 + 2] = b;
  }
  BLOCK_FILLER[def.id] = byKey.get(def.filler)?.id ?? def.id;
  BLOCK_BASE[def.id] = byKey.get(def.base)!.id;
}

export function getBlockDef(id: number): BlockDef | undefined {
  return byId.get(id);
}

export const formatBlockId = (id: number): string => byId.get(id)?.code ?? String(id);
export const blockBase = (id: number): number => BLOCK_BASE[id] ?? id;
