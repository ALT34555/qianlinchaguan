export const MAX_CONTOUR_LEVEL:number;
export const LEVEL_UNITS:Readonly<Record<'small'|'medium'|'large',readonly number[]>>;
export function contourSize(columns:number):'small'|'medium'|'large';
export function contourUnit(level:number,columns?:number):number;
export const MIN_SEPARATION_PX:number;
export function metersPerPixel(circumference:number,level:number):number;
export function drawsContour(height:number,next:number,unit:number,metersPerPixelValue:number):boolean;
export function bandPosition(height:number,unit:number):number;
export function terraceShade(band:number):number;
