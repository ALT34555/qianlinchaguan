import {WorldGenerator} from './WorldGenerator';
import {buildSurfacePreview} from './SurfacePreview';
import type {WorkerRequest} from './ChunkProtocol';
const ctx=self as unknown as Worker;
let generator:WorldGenerator|null=null;
ctx.onmessage=(event:MessageEvent<WorkerRequest>)=>{
  const m=event.data;
  if(m.kind==='init'){generator=new WorldGenerator(m.seed,m.climateWeights,m.generation);return;}
  if(!generator)throw new Error('舆图预览Worker未初始化');
  const preview=buildSurfacePreview(generator.generateChunk(m.cx,m.cz));
  ctx.postMessage(preview,[preview.rgba.buffer]);
};
