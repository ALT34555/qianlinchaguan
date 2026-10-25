export const DAY_MS:number;
export const THRESHOLD:number;
export function clamp(value:number,min?:number,max?:number):number;
export function smooth(a:number,b:number,x:number):number;
export function gameDay(ms:number,offset?:number):number;
export function hash(seed:number,x:number,y:number):number;
export class CloudNoise {
  constructor(seed:number);
  at(x:number,y:number,period?:number):number;
  cloud(wx:number,wz:number,days:number,circumference:number,columns:number):number;
}
export interface RainConfig {columns:number;rows:number;seed:number;fingerprint:string;rainfall:Float32Array}
export interface RainSnapshot {
  version:number;fingerprint:string;columns:number;rows:number;seed:number;day:number;
  water:Float32Array;rain:Float32Array;phase:Uint8Array;
}
export class DailyRainModel {
  readonly config:RainConfig;
  day:number;
  updates:number;
  readonly water:Float32Array;
  readonly rain:Float32Array;
  readonly phase:Uint8Array;
  constructor(config:RainConfig,day:number,saved?:RainSnapshot);
  stepRows(day:number,start:number,end:number):void;
  step(day:number):void;
  snapshot():RainSnapshot;
}
export function validateSnapshot(config:RainConfig,snapshot:RainSnapshot):void;
export function encodeRain(snapshot:RainSnapshot):Uint8Array;
export function decodeRain(input:Uint8Array|ArrayBuffer):RainSnapshot;
