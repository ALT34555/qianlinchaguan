export interface RainSample {
  intensity:number;cloudCover:number;precipitation:'none'|'rain'|'snow'|'mixed';
  id:number;rainMm:number;cloudWaterMm:number;temperature:number;
}
export function clamp(value:number,min?:number,max?:number):number;
export function smooth(a:number,b:number,x:number):number;
export function gameDay(ms:number,offset?:number):number;
export function hash(seed:number,x:number,y:number):number;
export function encodeRain(snapshot:import('./model.mjs').RainSnapshot):Uint8Array;
export function decodeRain(input:Uint8Array|ArrayBuffer):import('./model.mjs').RainSnapshot;
export class CloudNoise {
  constructor(seed:number);
  at(x:number,y:number,period?:number):number;
  cloud(wx:number,wz:number,days:number,circumference:number,columns:number):number;
}
export class RainField {
  version:number;
  noise:{cloud(wx:number,wz:number,days:number,circumference:number,columns:number):number};
  snapshot?:import('./model.mjs').RainSnapshot;
  constructor(world:{columns:number;rows:number;circumference:number;options:{seed:number};temperature:Float32Array});
  adopt(snapshot:import('./model.mjs').RainSnapshot):void;
  read(lon:number,lat:number,level?:number):{rain:number;water:number;temperature:number};
  sample(lon:number,lat:number,ms:number,footprint?:number,temperature?:number):RainSample;
}
