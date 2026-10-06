import type { RiverNetwork } from './RiverNetwork';
import type { WatershedLake } from './Watershed';

export type BasinLake = WatershedLake;

/** 湖泊与河流使用同一张排水树 */
export class BasinLakes {
  constructor(private readonly network: RiverNetwork) {}
  clear(): void {}
  get(cx: number, cz: number): BasinLake | null { return this.network.lake(cx, cz); }
  contains(lake: BasinLake, cx: number, cz: number): boolean { return this.network.contains(lake, cx, cz); }
}
