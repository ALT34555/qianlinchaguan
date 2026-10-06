import { clamp, smooth } from './TerrainLayers';

/** width 为半宽 */
export const MAX_RIVER_RADIUS = 192;
export const MAX_BRAID_RADIUS = 40;
export const MAX_BRAID_OFFSET = 14 + MAX_BRAID_RADIUS * .65;
export const MAX_MOUTH_LENGTH = 48;
export function riverWidth(discharge:number,blocks=discharge>=2500?2:1):number{
  const q=Math.max(0,discharge);
  if(blocks<=1)return Math.min(32,2+126*(1-Math.exp(-Math.sqrt(q)*.016)));
  if(blocks===2)return 48+16*smooth((q-2500)/7500);
  if(blocks===3)return 80+16*smooth((q-5000)/15000);
  if(blocks===4)return 96+32*smooth((q-20000)/40000);
  if(blocks===5)return 144+16*smooth((q-60000)/100000);
  return 160+32*smooth((q-160000)/200000);
}
/** 分级深度上限（格，自水面起算）：1~6 区块河宽 */
export const RIVER_TIER_DEPTH = [16, 32, 64, 128, 256, 256];
export const riverDepthLimit = (blocks: number): number =>
  RIVER_TIER_DEPTH[Math.min(RIVER_TIER_DEPTH.length, Math.max(1, Math.round(blocks))) - 1];
/** 随流量成熟度逼近本级上限，源头保留最小下切 */
export const riverDepth = (discharge: number,blocks=discharge>=2500?2:1): number => {
  const cap=riverDepthLimit(blocks),shallow=cap*.2;
  return Math.min(cap,shallow+(cap-shallow)*(1-Math.exp(-Math.cbrt(Math.max(0,discharge))*.052)));
};
/** 深槽外缘半径比例：此处河床抬回水面下 .85 */
export const RIVER_BED_CORE = .65;
/** 深槽平底半宽比例：越宽的河越平，1 级仍是 V 形 */
export const riverBedFlat = (width: number): number => clamp((width / 32 - 1) * .11, 0, .5);
export const riverShore = (width: number): number => Math.min(22, 5 + width * .45);
export const riverFloodplain = (width: number): number => Math.min(48, 18 + width * .4);
/** 水边半径扰动幅度（湖面另用更大噪声） */
export const RIVER_EDGE_WOBBLE = .16;
export const RIVER_EDGE_NOISE = .07;
export const RIVER_LAKE_EDGE_NOISE = .18;
/** 岸坡重映射位移上限 */
export const RIVER_BEACH_WOBBLE = .34;
/** 岸带 / 滩地宽度倍率幅度与上限 */
export const RIVER_SHORE_SCALE = .38;
export const RIVER_FLOOD_SCALE = .22;
export const RIVER_SHORE_BUDGET = 1 + RIVER_SHORE_SCALE;
export const RIVER_FLOOD_BUDGET = 1 + RIVER_FLOOD_SCALE;
/** 岸顶抬升倍率幅度 */
export const RIVER_CREST_SCALE = .42;
export const riverInfluence = (width: number): number => width + riverShore(width) + riverFloodplain(width);
/** 岸形扰动多伸出的半径，逐段查询范围按此收紧 */
export const riverInfluenceMargin = (width: number, lake = false): number => {
  const noise = lake ? RIVER_LAKE_EDGE_NOISE : RIVER_EDGE_NOISE;
  const narrow = width * (1 - noise - RIVER_EDGE_WOBBLE), wide = width * (1 + noise + RIVER_EDGE_WOBBLE);
  const steep = narrow + riverShore(narrow) * RIVER_SHORE_BUDGET + riverFloodplain(narrow) * RIVER_FLOOD_BUDGET;
  const shoal = wide + riverShore(wide) * (2 - RIVER_SHORE_BUDGET) + riverFloodplain(wide) * (2 - RIVER_FLOOD_BUDGET);
  return Math.max(0, Math.max(steep, shoal) - riverInfluence(width * (lake ? 1.18 : 1.07)));
};

/** 横断面外侧的岸形扰动参数 */
export interface RiverBankShape {
  /** 水边半径倍率偏移：正=水边外移 */
  edgeShift: number;
  /** 岸坡重映射位移：正=贴水先抬升 */
  beachShift: number;
  shoreScale: number;
  floodScale: number;
  crestScale: number;
}
/** 正=凹岸陡窄高，负=凸岸缓宽低；岸坡端点位移为零。 */
export function riverBankShape(character: number): RiverBankShape {
  const c = clamp(character, -1, 1);
  return {edgeShift: RIVER_EDGE_WOBBLE * c, beachShift: -RIVER_BEACH_WOBBLE * c,
    shoreScale: clamp(1 - RIVER_SHORE_SCALE * c, 2 - RIVER_SHORE_BUDGET, RIVER_SHORE_BUDGET),
    floodScale: clamp(1 - RIVER_FLOOD_SCALE * c, 2 - RIVER_FLOOD_BUDGET, RIVER_FLOOD_BUDGET),
    crestScale: clamp(1 + RIVER_CREST_SCALE * c, .5, 1.7)};
}

export interface RiverPoint {
  x: number; z: number; level: number; width: number; depth: number; discharge: number;
  /** 低缓静水河口的浅滩/沉积权重 */
  mouth?: number;
}

/** 分汊共用首尾、切线和水位 */
export function braidRiver(path: readonly RiverPoint[], side: number): readonly (readonly RiverPoint[])[] {
  if (path.length < 2) return [path];
  const first = path[0], last = path[path.length - 1];
  const length = Math.hypot(last.x - first.x, last.z - first.z);
  if (!length) return [path];
  const nx = -(last.z - first.z) / length, nz = (last.x - first.x) / length;
  return [.62, .38].map((share, branch) => path.map((point, i) => {
    const spread = i === 0 || i === path.length - 1 ? 0 : Math.sin(Math.PI * i / (path.length - 1)) ** 2;
    const offset = (branch ? -1 : 1) * side * (14 + Math.min(point.width, MAX_BRAID_RADIUS) * .65) * spread;
    const discharge = point.discharge * share;
    const blend = smooth(spread);
    return {...point, x: point.x + nx * offset, z: point.z + nz * offset, discharge,
      width: point.width + (riverWidth(discharge) - point.width) * blend,
      depth: point.depth + (riverDepth(discharge) - point.depth) * blend};
  }));
}
