export function waterSeasonPhase(phase:number|null):number|null {
  if(phase===null)return null;
  if(!Number.isFinite(phase))throw Error('季相须为有限数值');
  return Math.floor(((phase%4+4)%4)*16)/16;
}
const warmth=(phase:number)=>Math.sin((phase-.5)*Math.PI/2);
export function seasonTemperature(phase:number|null,latitude:number):number {
  return phase===null?0:16*Math.sin(latitude)*warmth(phase);
}
export function seasonDryness(phase:number|null,latitude:number):number {
  return phase===null?0:.5-.5*Math.max(-1,Math.min(1,latitude/(Math.PI/12)))*warmth(phase);
}
