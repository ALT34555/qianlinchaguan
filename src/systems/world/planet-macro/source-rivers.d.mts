import type {MacroWorld,MacroSurface} from './backend.mjs';
export interface RiverPoint {x:number;z:number;level:number;sink?:number;bedHeight?:number}
export interface RiverEdge {id:string;sourceType:'macro'|'source';cell:number;receiver:number;halfWidth:number;dischargeM3s:number;points:RiverPoint[]}
export interface RiverNetwork {cell:number;world:MacroWorld;edges:RiverEdge[];mainEdges:RiverEdge[];elapsed:number;sources:unknown[];frame:{size:number;coordinates(x:number,z:number):{wx:number;wz:number};local(wx:number,wz:number):{x:number;z:number}}}
export interface RiverChunkLabel {id:number;code:string;name:string;landClass:number;material:number;terrain:number;isRiver:boolean;isWaterfall:boolean;isStaticWater:boolean;isWetland:boolean;waterfallDropM?:number;salinity?:'salt'|'fresh'|null;hasDecorativeStream?:boolean}
export function getTributaries(world:MacroWorld,cell:number):RiverNetwork;
export function applyTributaries(world:MacroWorld,wx:number,wz:number,s:MacroSurface,network:RiverNetwork,chunkLabels?:boolean):MacroSurface;
export function riverChunkLabel(network:RiverNetwork,cx:number,cz:number):RiverChunkLabel;
export function macroCurveGeometry(world:MacroWorld,cell:number):{cell:number;receiver:number;halfWidth:number;points:{wx:number;wz:number;level:number}[]}|null;
