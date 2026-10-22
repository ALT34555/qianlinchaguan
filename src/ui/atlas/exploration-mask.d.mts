import type {Exploration} from '../../systems/world/Exploration';
import type {AtlasCamera} from './tiles.mjs';
export function exploredPolygons(state:AtlasCamera,exploration:Exploration,size:number):{x:number;y:number}[][];
export function clipExplored(ctx:CanvasRenderingContext2D,state:AtlasCamera,exploration:Exploration,size:number):void;
