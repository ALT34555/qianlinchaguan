export const MERCATOR_LIMIT=Math.atan(Math.sinh(Math.PI));
export function mercatorZ(z:number,size:number):number {
  const angle=Math.max(-MERCATOR_LIMIT,Math.min(MERCATOR_LIMIT,z/size*Math.PI*2));
  return Math.asinh(Math.tan(angle))*size/(Math.PI*2);
}
export function inverseMercatorZ(y:number,size:number):number {
  return Math.atan(Math.sinh(Math.max(-Math.PI,Math.min(Math.PI,y/size*Math.PI*2))))*size/(Math.PI*2);
}
