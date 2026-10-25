import type {MacroWorld} from '../../systems/world/planet-macro/backend.mjs';
export const CHUNK:number;
export interface ReliefIndex {
  readonly world:MacroWorld;
  readonly columns:number;readonly rows:number;readonly chunks:number;readonly span:number;
  readonly land:Float32Array;readonly snow:Uint8Array;readonly wet:Uint8Array;readonly owner:Uint8Array;
  readonly level:Uint8Array;readonly cos:Float32Array;readonly maxRain:number;
  readonly version:number;readonly done:number;readonly ready:boolean;
  advance(cancel:()=>boolean):boolean;
  finish():void;
  index(x:number,y:number):number;
}
export function indexFor(world:MacroWorld):ReliefIndex;
