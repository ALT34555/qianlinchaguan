import type {MacroWorld} from './backend.mjs';
export function satelliteMap(world:MacroWorld):{pixels:Uint8ClampedArray;codes:Uint16Array;elapsed:number;scope:string};
export function satelliteColor(code:number,rainfall:number):number[];
