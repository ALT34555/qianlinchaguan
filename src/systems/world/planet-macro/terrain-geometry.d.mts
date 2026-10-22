export type HeightSampler = (x:number,z:number)=>number;
export const TERRAIN_GRID:2;
export const TERRAIN_ORIGIN:0.5;
export function terrainVertexHeight(x:number,z:number,height:HeightSampler):number;
export function terrainDiagonal(x:number,z:number,seed:number):boolean;
export function triangleHeight(u:number,v:number,a:number,b:number,c:number,d:number,flip:boolean):number;
export function terrainHeightAt(x:number,z:number,seed:number,height:HeightSampler):number;
export const TERRAIN_DETAIL_DROP:0.1;
export function terrainCell(x:number,z:number,seed:number,height:HeightSampler):{refined:boolean;flip:boolean;heights:number[]};
