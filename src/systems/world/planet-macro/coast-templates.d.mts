import type {MacroWorld} from './backend.mjs';
export interface CoastEntry {cell:number;base:number;variant:number;geology:number;scale:number}
export interface CoastPatch {seed:number;base:number;variant:number;geology:number;length:number}
export interface CoastPath {id:number;length:number;period:number;closed:boolean;patches:CoastPatch[]}
export interface CoastSample {height:number;kind:number;water:number;cell:number;base:number;variant:number;geology:number;u:number;v:number;weight:number;period:number;arc:number;normalMetersPerUnit:number;alongMetersPerUnit:number;strokeWidth:number;macroSlope:number;scale:number;_path:CoastPath}
export const COAST_VARIANT_COUNT:number;
export const PURE_COAST_SHARE:number;
export const COAST_TEMPLATE_META:{name:string;kind:string;family:string}[];
export function coastCatalog(world:MacroWorld):{blocked:Uint8Array;protectedCells:Uint8Array;entries:Map<number,CoastEntry>;bins:Map<number,unknown[]>;paths:CoastPath[];patches:CoastPatch[];pureLength:number;totalLength:number};
export function sampleCoastRaster(base:number,variant:number,u:number,v:number):number;
export function sampleCoast(world:MacroWorld,wx:number,wz:number,natural:number,baseHeight?:number|null):CoastSample|null;
export function coastStrokeWeight(coast:CoastSample|null):number;
export function coastTint(coast:CoastSample|null,rgb:number[]):number[];
