import { WorldDataStore } from './WorldDataStore';
import { encodeWorldData, decodeWorldData } from './WorldDataCodec';
import { worldSignature } from './WorldSignature';
import type { WorldGenerator, ChunkGenResult, ChunkInfo, RiverPoint } from './WorldGenerator';
import type { SurfacePreview } from './SurfacePreview';

export interface MapSummary extends SurfacePreview { info:ChunkInfo;paths:readonly (readonly RiverPoint[])[] }
export function chunkSignature(generator:WorldGenerator):string {
  return worldSignature(generator)+'|'+JSON.stringify([generator.getWaterFreezeRules(),generator.getWaterAppearanceOverrides()]);
}
export class ChunkCache {
  readonly store:WorldDataStore;
  private writes = Promise.resolve();
  private queued = 0;
  constructor(worldKey:string){this.store=new WorldDataStore(worldKey);}
  async read<T>(key:string,signature:string):Promise<T|null>{
    try{
      const bytes=await this.store.get(key);if(!bytes)return null;
      const record=decodeWorldData<{signature:string;value:T}>(bytes);
      return record.signature===signature?record.value:null;
    }catch(error){console.warn('[Chunk cache read]',error);return null;}
  }
  async write(key:string,signature:string,value:unknown):Promise<void>{
    try{await this.store.put(key,encodeWorldData({signature,value}));}
    catch(error){console.warn('[Chunk cache write]',error);}
  }
  queueWrite(key:string,signature:string,value:unknown):void {
    let bytes:Uint8Array;
    try{bytes=encodeWorldData({signature,value});}
    catch(error){console.warn('[Chunk cache encode]',error);return;}
    this.queued++;
    this.writes=this.writes.then(()=>this.store.put(key,bytes)).catch(error=>{
      console.warn('[Chunk cache write]',error);
    }).finally(()=>{this.queued--;});
  }
  async flush(limit=0):Promise<void>{if(this.queued>limit)await this.writes;}
  chunk(cx:number,cz:number,signature:string):Promise<ChunkGenResult|null>{return this.read(`chunk_${cx}_${cz}`,signature);}
  summary(cx:number,cz:number,signature:string):Promise<MapSummary|null>{return this.read(`preview_${cx}_${cz}`,signature);}
}
export function minimapSummary(generator:WorldGenerator,cx:number,cz:number,rgba:Uint8ClampedArray<ArrayBuffer>):MapSummary {
  const average:[number,number,number]=[0,0,0],pixels=rgba.length/4;
  for(let i=0;i<pixels;i++)for(let c=0;c<3;c++)average[c]+=rgba[i*4+c]/pixels;
  return {cx,cz,size:64,rgba,average,info:generator.getChunkInfo(cx,cz),paths:generator.getRiverPaths(cx,cz)};
}
