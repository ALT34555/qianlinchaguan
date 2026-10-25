import type {MacroWorld,MacroSurface} from './backend.mjs';
export interface RiverPoint {x:number;z:number;level:number;sink?:number;bedHeight?:number;halfWidth?:number;depth?:number}
export interface RiverEdge {id:string;sourceType:'macro'|'source';cell:number;receiver:number;halfWidth:number;dischargeM3s:number;points:RiverPoint[];outletKind?:'river'|'wetland'}
export interface RiverNetwork {cell:number;world:MacroWorld;edges:RiverEdge[];mainEdges:RiverEdge[];elapsed:number;sources:unknown[];wetlands:{id:string;x:number;z:number;level:number;radius:number;depth:number}[];frame:{size:number;coordinates(x:number,z:number):{wx:number;wz:number};local(wx:number,wz:number):{x:number;z:number}}}
export interface RiverChunkLabel {id:number;code:string;name:string;landClass:number;material:number;terrain:number;isRiver:boolean;isWaterfall:boolean;isStaticWater:boolean;isWetland:boolean;waterfallDropM?:number;salinity?:'salt'|'fresh'|null;hasDecorativeStream?:boolean}
export function tributaryCandidate(world:MacroWorld,cell:number):{cell:number;lake:boolean}|null;
export function getMainRiverNetwork(world:MacroWorld,cell:number):RiverNetwork;
export function getTributaries(world:MacroWorld,cell:number):RiverNetwork;
export function getTributariesAsync(world:MacroWorld,cell:number,options?:{cancelled?:()=>boolean;budgetMs?:number;yieldControl?:()=>Promise<void>;now?:()=>number}):Promise<RiverNetwork|null>;
export function macroCellHasWater(world:MacroWorld,cell:number):boolean;
export interface SpringRockSite {x:number;z:number;id:string;scale:number;yaw:number;form:'buried'|'outcrop'}
export function springRockSites(world:MacroWorld,cx:number,cz:number):SpringRockSite[];
export function applyTributaries(world:MacroWorld,wx:number,wz:number,s:MacroSurface,network:RiverNetwork,chunkLabels?:boolean|'rivers',includeDecorative?:boolean):MacroSurface;
export function riverChunkLabel(network:RiverNetwork,cx:number,cz:number):RiverChunkLabel;
export function macroCurveGeometry(world:MacroWorld,cell:number):{cell:number;receiver:number;halfWidth:number;points:{wx:number;wz:number;level:number}[]}|null;
