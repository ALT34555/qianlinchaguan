import type {MacroWorld,MacroSurface} from './backend.mjs';
export interface LakeOutletPoint {wx:number;wz:number;level:number;halfWidth:number;depth:number}
export interface LakeLandscapeOutlet {
  id:string;cell:number;ownerCell?:number;lakeId:number;salinity:'salt'|'fresh';level:number;drop:number;fall:boolean;cos:number;points:LakeOutletPoint[];
  pond:{wx:number;wz:number;level:number;radiusX:number;radiusZ:number;depth:number};
  bounds:{x0:number;z0:number;span:number;cos:number};
}
export interface LakeLandscape {status:'existing'|'salt-shore'|'local'|'no-safe-bank';outlet:LakeLandscapeOutlet|null}
export function shoreContext(world:MacroWorld,wx:number,wz:number):{a:number;b:number;c:number;d:number;tx:number;ty:number;pole:number;poleId:number};
export function sampleLakeShore(world:MacroWorld,wx:number,wz:number,height:number,base?:number|null,context?:unknown,sampler?:((world:MacroWorld,x:number,z:number)=>number)|null,details?:boolean,landscapes?:boolean):Partial<MacroSurface>|null;
export function lakeLandscapeForCell(world:MacroWorld,cell:number,sampler?:(world:MacroWorld,x:number,z:number)=>number):LakeLandscape|null;
export function lakeShoreStats(world:MacroWorld):{step:number;sampled:number;buildMs:number;visualEntries:number;visualBytes:number;models:{id:number;level:number;originalLevel:number;tileCount:number;bytes:number}[]}|null;
