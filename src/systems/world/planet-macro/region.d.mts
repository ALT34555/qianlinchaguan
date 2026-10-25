import type {MacroWorld,MacroSurface} from './backend.mjs';
import type {PlanetPatch,PatchGrid} from './patch.mjs';
import type {RiverNetwork} from './source-rivers.mjs';
export interface PlanetRegion {
  cell:number;size:number;lon:number;lat:number;
  coordinates(x:number,z:number):{wx:number;wz:number};
  sample(x:number,z:number,details?:boolean):MacroSurface;
}
export function createRegion(world:MacroWorld,cell:number,tributaries?:boolean,options?:{chunkLabels?:boolean}):PlanetRegion;
export function buildRegion(world:MacroWorld,cell:number,step?:number,tributaries?:boolean):PlanetPatch;
export function buildRegionNeighborhood(world:MacroWorld,cell:number,step?:number):PlanetPatch&{cell:number;macroCells:number[];network:RiverNetwork;borders:{cell:number;positions:Float32Array;selected:boolean}[];contains(x:number,z:number):boolean;timings:Record<string,number>};
export function buildRegionChunk(world:MacroWorld,cell:number,cx:number,cz:number,size?:number,step?:number,tributaries?:boolean,options?:{cache?:boolean;reuse?:PatchGrid}):PlanetPatch;
export interface PreviewOptions {signal?:AbortSignal;progress?:(data:{stage:string;completed?:number;total?:number;visited?:number;sampled?:number})=>void;pause?:()=>Promise<void>}
export function buildRegionNeighborhoodAsync(world:MacroWorld,cell:number,step?:number,options?:PreviewOptions):Promise<ReturnType<typeof buildRegionNeighborhood>>;
export function buildRegionChunkAsync(world:MacroWorld,cell:number,cx:number,cz:number,size?:number,step?:number,tributaries?:boolean,options?:PreviewOptions&{cache?:boolean;reuse?:PatchGrid}):Promise<PlanetPatch>;
export function clearChunkCache(world:MacroWorld):void;
export function chunkCacheStats(world:MacroWorld):{entries:number;bytes:number;hits:number;misses:number;evictions:number;limit:number;budget:number};
