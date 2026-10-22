import type {Block} from '../../systems/world/Blocks';
export interface Mask {kind:'land'|'lake';block:number;sun:boolean}
export function classifySurfaceHeight(height:number):number;
export function finalizeMasks(masks:Mask[],where:Masks,filled:Uint8Array,shapeLakes:boolean,cut:number):void;
export const DETAIL_MAX:number;
export type {Block};
