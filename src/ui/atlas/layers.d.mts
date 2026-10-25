export type LayerId='civilization'|'chunks'|'baseline'|'daylight'|'rain'|'contour'|'climate'|'temperature'|'height'|'ice'|'depth';
export interface LayerState {id:LayerId;on:boolean;opacity:number}
export const LAYER_IDS:readonly LayerId[];
export const LAYER_DEFS:readonly {id:LayerId;opacity:boolean;slider:string|null}[];
export function defaultLayers():LayerState[];
export function normalizeLayers(value:unknown):LayerState[];
export const DENSITY_RANGE:readonly number[];
export function layersKey(layers:LayerState[],density:number):string;
export function clampDensity(value:unknown):number;
export function contourLevelsFor(density:unknown):number;
