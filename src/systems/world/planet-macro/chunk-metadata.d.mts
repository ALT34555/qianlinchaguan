import type {MacroWorld,MacroSurface} from './backend.mjs';
import type {RiverChunkLabel} from './source-rivers.mjs';
export function chunkMetadata(id:number):RiverChunkLabel;
export function landTypeId(temperature:number,rainfall:number,evaporation:number,relief:number,slope:number,relative:number):number;
export function landChunkMetadata(world:MacroWorld,samples:MacroSurface[],wx:number,wz:number):RiverChunkLabel;
