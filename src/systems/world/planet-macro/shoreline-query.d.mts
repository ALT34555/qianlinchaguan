export const SHORELINE_MODEL_REVISION:string;
export type WaterBodyKind='sea'|'lake'|'river'|'unknown';
export interface ShorelineColumn {bed:number;water:number;waterBodyId:string|null;waterBodyKind:WaterBodyKind;iceLevel?:number;iceBottom?:number;iceCoverage?:number;temperature?:number;color?:number;chunkCode?:string|number}
export interface ShorelineSource {worldRevision:string;hydrologyRevision:string;seed:number;circumference?:number;waterOffset?:number;column:(x:number,z:number)=>ShorelineColumn;mayCross?:(bounds:ShorelineRequest['bounds'])=>boolean;hintStats?:()=>{bytes:number;entries:number};clearHints?:()=>void}
export interface ShorelineRequest {
  worldRevision:string;hydrologyRevision:string;
  bounds:{minX:number;minZ:number;maxX:number;maxZ:number};coordinateSystem:'world-xz-metres';
  targetErrorM:number;maxVertices:number;maxCells?:number;maxStepM?:number;rasterSide?:number;phase?:'liquid'|'surface';disableHints?:boolean;cancelled?:()=>boolean;
}
export interface ShorelineResult {
  modelRevision:string;worldRevision:string;hydrologyRevision:string;coordinateSystem:'world-xz-metres';
  origin:{x:number;z:number};targetErrorM:number;
  boundaries:{phase:'liquid'|'ice';waterBodyId:string|null;waterBodyKind:WaterBodyKind;closed:boolean;coordinates:Float64Array}[];
  error:{reference:'fixed-mountain-triangles';maxErrorM:null;status:'unknown';continuousSurfaceMaxErrorM:null};
  budgetDegraded:boolean;modelDegraded:boolean;complete:boolean;waterMask?:Uint8Array;iceMask?:Uint8Array;rasterSide?:number;
  stats:{hintCacheBytes:number;columnProbes:number;sourceCacheHits:number;sourceCacheBytes:number;samples:number;cells:number;vertices:number;elapsedMs:number;cacheHit:boolean;cacheBytes:number};
}
export function createPlanetShorelineSource(world:import('./backend.mjs').MacroWorld,options?:{worldRevision?:string;hydrologyRevision?:string;column?:ShorelineSource['column']}):ShorelineSource;
export function createShorelineQuery(source:ShorelineSource,options?:{maxCacheBytes?:number;maxSourceBytes?:number}):{
  query(request:ShorelineRequest):ShorelineResult;
  queryAsync(request:ShorelineRequest,options?:{yieldControl?:()=>Promise<unknown>}):Promise<ShorelineResult>;
  steps(request:ShorelineRequest):Generator<void,ShorelineResult>;
  clear():void;stats():{sourceCalls:number;sourceCacheBytes:number;sourceCacheEntries:number;maxSourceBytes:number;cacheBytes:number;cacheEntries:number;hits:number;misses:number;maxCacheBytes:number};
};
