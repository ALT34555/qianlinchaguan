import type {TileStore} from './tile-plan.mjs';

export interface TileJob {
  layer: string;
  tile: any;
  style: string;
}

export interface TileSchedulerOptions {
  owner?: any;
  store?: TileStore;
  budget?: number;
}

export class TileScheduler {
  constructor(world: any, options?: TileSchedulerOptions);
  world: any;
  owner: any;
  store: TileStore;
  failedTiles: Set<string>;
  queue: any[];
  serial: number;
  generation: number;
  version: number;
  baseVersion: number;
  inFlight: any;
  cancelling: boolean;
  paused: boolean;
  progress: any;
  stats: any;
  focusId: any;
  plan: any;
  conditionKey: string;
  context: Record<string, unknown>;
  disposed: boolean;
  tributaryPoolSize: number;

  setWorld(world: any): void;
  waterState():any;
  syncWater():void;
  setPaused(paused: boolean): void;
  cancel(): void;
  updateQueue(jobs: Map<string, TileJob>, protectedIds: Set<string>): void;
  tributaryVectors(): any[];
  pump(context?: any): void;
  dispose(): void;
}
