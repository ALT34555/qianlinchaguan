import type {MacroWorld} from '../../systems/world/planet-macro/backend.mjs';
export type ReliefMode='none'|'sun'|'height'|'contour';
export interface SunWorld {axialTilt?:number;prograde?:boolean}
export interface SunState {height:number;east:number;north:number}
export const RELIEF_VERSION:number;
export const RELIEF_MODES:readonly ReliefMode[];
export function normalizeReliefMode(value:unknown):ReliefMode;
export function normalizeSunHours(value:unknown):number;
export function ramp(stops:readonly (readonly [number,readonly [number,number,number]])[]):Uint8Array;
export const LAND:Uint8Array, ROCK:Uint8Array, WATER:Uint8Array, ICE:Uint8Array, SNOW:Uint8Array, NIGHT:Uint8Array, TINT:Uint8Array;
export const LIGHT_DAY:readonly [number,number,number], LIGHT_NIGHT:readonly [number,number,number];
export const RELIEF_FULL:number;
export function elevationRampValue(height:number):number;
export function snowValue(temperature:number):number;
export function depthValue(level:number,height:number,full:number):number;
export const DEPTH_SEA:number, DEPTH_LAKE:number;
export const CONTOUR_LEVELS:number;
export function contourUnit(heights:ArrayLike<number>):number;
export function flatColor(kind:number,rampValue:number,out:Float64Array|number[],snow:number,depth:number):Float64Array|number[];
export function sunState(hours:number,latitude:number,longitude:number,world:SunWorld|undefined|unknown):SunState;
export interface DayProfile {sinDecl:number;cosDecl:number;sunLon:number}
export function dayProfile(hours:number,world:SunWorld|undefined|unknown):DayProfile;
export function dayFactor(profile:DayProfile,latitude:number,longitude:number):{height:number;day:number;dusk:number};
export function applyDaylight(rgb:Float64Array|number[],day:number,dusk:number):Float64Array|number[];
export function shadeRGB(rgb:Float64Array|number[],sun:SunState|null,slopeEast:number,slopeNorth:number):Float64Array|number[];
export type MacroWorldType=MacroWorld;
