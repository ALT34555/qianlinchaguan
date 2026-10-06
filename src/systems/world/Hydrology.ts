import { SEA_LEVEL } from '../../core/config';
import {freezeState} from './TerrainLayers';

export const FLOW_DIRECTIONS = [
  { dx: 0, dz: -1, arrow: '↑' }, { dx: 1, dz: -1, arrow: '↗' },
  { dx: 1, dz: 0, arrow: '→' }, { dx: 1, dz: 1, arrow: '↘' },
  { dx: 0, dz: 1, arrow: '↓' }, { dx: -1, dz: 1, arrow: '↙' },
  { dx: -1, dz: 0, arrow: '←' }, { dx: -1, dz: -1, arrow: '↖' },
] as const;
/** 降水 0.5 的成河门槛 */
export const RIVER_THRESHOLD = 190;
export interface DrainageNode { height: number; moisture: number; temperature: number }
export const liquidRiverAllowed=(n:DrainageNode):boolean=>n.height>SEA_LEVEL&&freezeState(n.height,n.temperature)==='liquid';
export const waterfallDrop=(drop:number):boolean=>drop>=16&&drop<=256;
export const splashWetlandDrop=(drop:number):boolean=>drop>=64&&drop<=256;
const clamp = (v: number) => Math.max(0, Math.min(1, v));

/** 每平方区块的长期有效径流 */
export function effectiveRunoff(n: DrainageNode, precipitation = .8): number {
  if (!liquidRiverAllowed(n) || precipitation === 0) return 0;
  const wet = clamp((n.moisture + 1) * .5);
  const temperature = n.temperature - Math.max(0, n.height) * .0065;
  const rainfall = .12 + 3.2 * wet ** 1.7;
  const evaporation = (.12 + .65 * clamp((temperature + 5) / 40)) * (1 - wet);
  const melt = .55 + .45 * clamp((temperature + 15) / 20);
  return rainfall * (1 - evaporation) * melt * precipitation / .8;
}
