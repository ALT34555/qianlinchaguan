import { smooth } from './TerrainLayers';

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
export const riverDepth = (discharge: number,blocks=discharge>=2500?2:1): number =>
  Math.min(4+blocks*4,1.1+19*(1-Math.exp(-Math.cbrt(Math.max(0,discharge))*.05))+Math.max(0,blocks-2)*2.2);
export const riverShore = (width: number): number => Math.min(22, 5 + width * .45);
export const riverFloodplain = (width: number): number => Math.min(48, 18 + width * .4);
export const riverInfluence = (width: number): number => width + riverShore(width) + riverFloodplain(width);

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
