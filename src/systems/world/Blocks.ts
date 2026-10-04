/**
 * 方块注册表。名称 / 颜色 / 填充层来自 content/data/world/blocks.json，
 * 数字 ID 在代码中固定，加载时校验二者一致。
 */
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
} as const;

export type BlockId = (typeof Block)[keyof typeof Block];

export interface BlockDef {
  id: number;
  key: string;
  name: string;
  /** 纯色面颜色（#rrggbb），空气为 null */
  color: string | null;
  /** 表层以下的填充方块 key（如草方块下面是泥土） */
  filler: string;
}

const MAX_BLOCKS = 256;
const defs: BlockDef[] = blockDefs as BlockDef[];
const byKey = new Map<string, BlockDef>(defs.map((d) => [d.key, d]));

/** 方块颜色表：BLOCK_RGB[id*3 + 0..2] */
export const BLOCK_RGB = new Uint8Array(MAX_BLOCKS * 3);
/** 填充方块表：BLOCK_FILLER[id] = 填充方块 id */
export const BLOCK_FILLER = new Uint8Array(MAX_BLOCKS);

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
  if (def.color) {
    const [r, g, b] = parseHex(def.color);
    BLOCK_RGB[def.id * 3] = r;
    BLOCK_RGB[def.id * 3 + 1] = g;
    BLOCK_RGB[def.id * 3 + 2] = b;
  }
  BLOCK_FILLER[def.id] = byKey.get(def.filler)?.id ?? def.id;
}

export function getBlockDef(id: number): BlockDef | undefined {
  return defs.find((d) => d.id === id);
}
