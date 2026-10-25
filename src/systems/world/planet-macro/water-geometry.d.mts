import type {HeightSampler} from './terrain-geometry.mjs';
export interface WaterVertex {x:number;y:number;z:number;depth:number}
export function waterVertexLevel(x:number,z:number,height:HeightSampler,water:HeightSampler,seed:number,seaFallback?:boolean,offset?:number):number;
export function clipShore(vertices:WaterVertex[],upper?:number):WaterVertex[];

export interface IceVertex {x:number;y:number;z:number;depth:number;coverage:number;bottom:number}
export function iceVertexLevel(x:number,z:number,ice:HeightSampler):number;
export function iceVertexCoverage(x:number,z:number,coverage:HeightSampler):number;
export function clipIce(vertices:IceVertex[],field:'coverage'|'depth'):IceVertex[];
