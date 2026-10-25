export interface PatchGrid {
  side:number;positions:Float32Array;colors:Float32Array;levels:Float32Array;kinds:Uint8Array;wetlands:Float32Array;
}
export interface PlanetPatch extends PatchGrid {
  cx:number;cz:number;size:number;step:number;indices:Uint32Array;waterPositions:Float32Array;waterIndices:Uint32Array;min:number;max:number;elapsed:number;
}
export function gridBuffers(side:number,reuse?:PatchGrid|null):PatchGrid;
export function finishGrid(cx:number,cz:number,size:number,step:number,buffers:PatchGrid,min:number,max:number,start:number):PlanetPatch;
export function topology(side:number):Uint32Array;
export function buildPatch(world:import('./backend.mjs').MacroWorld,cx:number,cz:number,size?:number,step?:number,sampler?:(x:number,z:number)=>import('./backend.mjs').MacroSurface,edgeRemainder?:boolean,options?:{reuse?:PatchGrid;onSample?:(x:number,z:number,s:import('./backend.mjs').MacroSurface)=>void}):PlanetPatch;
export function buildPatchSteps(...args:Parameters<typeof buildPatch>):Generator<Promise<unknown>|{stage:string;completed:number;total:number},PlanetPatch,unknown>;
