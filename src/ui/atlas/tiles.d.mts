export const TAU:number;
export const LIMIT:number;
export const TILE:number;
export interface AtlasCamera {width:number;height:number;scale:number;lon:number;lat:number;view:'globe'|'projection'|'mercator'}
export function atlasCamera(view:unknown,width:number,height:number):AtlasCamera;
export function clamp(value:number,min:number,max:number):number;
export function wrap(value:number):number;
export function merc(lat:number):number;
export function project(view:{width:number;height:number;scale:number;lon:number;lat:number;view:'globe'|'projection'|'mercator'},lon:number,lat:number,clip?:boolean):{x:number;y:number}|null;
export function unproject(view:{width:number;height:number;scale:number;lon:number;lat:number;view:'globe'|'projection'|'mercator'},x:number,y:number):{lat:number;lon:number}|null;
export function tileKey(z:number,x:number,y:number):string;
export function visibleTiles(view:unknown,z:number,limit?:number):unknown[];
export function levelFor(view:{scale:number}):number;
export function detailLevel(meters:number):string;
