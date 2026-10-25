export interface SurfaceFields {
  heights: Float32Array;
  kinds: Uint8Array;
  snow: Uint8Array;
  depth: Uint8Array;
  rivers: Uint8Array;
  climate: Uint8Array;
  temperature: Int8Array;
  ice: Uint8Array;
  satellite: Uint8ClampedArray;
  biome: Uint8ClampedArray;
  meters: number;
  bytes?: number;
}

export class SurfaceSampler {
  constructor(world: any, budget?: number);
  world: any;
  budget: number;
  bytes: number;
  gridBytes: number;
  cache: Map<string, SurfaceFields>;
  samples: number;
  hits: number;
  colorSampleCount: number;
  shorelineGeometryCount: number;
  dataLayerCounts: Record<string, number>;
  grid4mCache: Map<string, any>;
  riverThreshold: number;
  rivers: number[];

  cacheStats():any;
  clear():void;
  clearFields():void;
  sample4mColor(wx: number, wz: number, palette?: string, cancelled?: () => boolean, waitForShore?: any, step?:number): Promise<any>;
  cacheKeyFor(tile: any, focusId: any, stride: number): string;
  fillContinuousBase(tile:any,data:any,stride:number,cancelled:()=>boolean,options?:any):Promise<any>;
  fillSurfaceColors(tile:any,data:any,stride:number,cancelled:()=>boolean):Promise<any>;
  getFields(
    tile: any,
    focusId: any,
    cancelled?: () => boolean,
    stride?: number,
    waitForShore?: (promise: Promise<any>, cancelled: () => boolean) => Promise<boolean>,
    options?: any
  ): Promise<SurfaceFields | null>;
}

export function macroCodeAt(world:any,cell:number):number;
export function shadeLayerPixels(layer: string, fields: SurfaceFields, options: any): Uint8ClampedArray;
export function slopeAt(fields: SurfaceFields, x: number, y: number, dx: number, dy: number): number;
export function contourShade(fields: SurfaceFields, i: number, unit: number): number;
export function sampleOverview(w: any, wx: number, wz: number): any;
export function overviewRiver(w: any, cell: number): any;
export function riverStrokePixels(flow: number, threshold: number, z: number): number;
