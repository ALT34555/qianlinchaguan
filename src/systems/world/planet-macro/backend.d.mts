export interface MacroOptions {
  seed:number;columns:number;plateCount:number;activity:number;driftSteps:number;landRatio:number;
  recipe:'balanced'|'archipelago'|'mountains'|'rift';equatorChunks:number;equatorTemperature:number;
  poleTemperature:number;precipitation:number;flagCount:number;
  climateProfile?:'prograde-0'|'prograde-23'|'prograde-60'|'retrograde-0'|'retrograde-23'|'retrograde-60';
}
export interface MacroRiver {
  cell:number;receiver:number;dischargeM3s:number;widthM:number;directionEast:number;directionNorth:number;
  id?:string;visualWet?:number;visualDry?:number;source?:'template'|'macro'|'source';waterfallDropM?:number;confluenceBed?:number;
  dischargeRole?:'macro'|'decorative-estimate';hasDecorativeStream?:boolean;
}
export interface MacroSurface {height:number;water:number;kind:number;cell:number;temperature:number;river:MacroRiver|null;wetland:number;wetlandId?:string;chunkCode?:string;salinity?:'salt'|'fresh'|null;saltLakeId?:number|null;surfaceBlock?:number;surfaceColor?:number}
export interface MacroWorld {
  rule:string;ruleVersion:string;options:MacroOptions;columns:number;rows:number;circumference:number;radius:number;
  macroIslands?:unknown[];poles:Record<string,number[]>;lakeSalinityEnabled?:boolean;
  plate:Uint16Array;boundary:Uint8Array;uplift:Float32Array;rift:Float32Array;shear:Float32Array;height:Float32Array;
  areas:Float32Array|Float64Array;temperature:Float32Array;rainfall:Float32Array;evaporation:Float32Array;moisture:Float32Array;
  receiver:Int32Array;rank:Uint32Array;spill:Float32Array;flow:Float64Array;catchment:Float64Array;
  riverWidth:Float32Array;lakeId:Int32Array;water:Uint8Array;biome:Uint8Array;
  flags:{cell:number;cx:number;cz:number;height:number;relief:number}[];
  continents:{id:number;start:number[];center:number[];owner:number;bias:number}[];
  plates:unknown[];rotationHistory:unknown[];operations:unknown[];lakes:unknown[];
  stats:Record<string,number>;timings:Record<string,number>;audit:MacroAudit;fingerprint:string;
}
export interface MacroAudit {ok:boolean;invalid:number;cycles:number;uphill:number;conservation:number;areaError:number;riverCells:number;maxLakeDepth:number}
export const MACRO_RULE:string;
export function setRuleVersion(value:string):void;
export function getRuleVersion():string;
export function normalizeOptions(input?:Partial<MacroOptions>):MacroOptions;
export function generateWorld(input?:Partial<MacroOptions>,progress?:(stage:string)=>void):MacroWorld;
export function sampleSurface(world:MacroWorld,wx:number,wz:number,details?:boolean,carveRivers?:boolean,shapeLakes?:boolean):MacroSurface;
export function macroAt(world:MacroWorld,wx:number,wz:number,field?:string):number;
export function fingerprint(world:MacroWorld):string;
export function validateWorld(world:MacroWorld):MacroAudit;
export function dataBytes(value:unknown):number;
export function transferBuffers(value:unknown):ArrayBuffer[];
