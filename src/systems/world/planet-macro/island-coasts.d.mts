import type {MacroWorld} from './backend.mjs';
export interface IslandCoast {height:number;kind:number;water:number;islandCell:number;variant:number}
export interface IslandCoastEntry {cell:number;variant:number;wx:number;wz:number;cos:number;angle:number;sin:number;rotCos:number;span:number;peak:number;delta:number}
export function islandCoastCatalog(world:MacroWorld):{islands:IslandCoastEntry[];cells:Map<number,IslandCoastEntry[]>};
export function islandTemplateHeight(variant:number,u:number,v:number):number|null;
export function sampleIslandCoast(world:MacroWorld,wx:number,wz:number,natural:number):IslandCoast|null;
