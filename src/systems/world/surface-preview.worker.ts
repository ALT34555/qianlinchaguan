import {WorldGenerator} from './WorldGenerator';
import {buildSurfacePreview} from './SurfacePreview';
import type {SurfacePreviewRequest} from './ChunkProtocol';
import {ChunkCache,chunkSignature} from './ChunkCache';
import {overviewColor} from '../../ui/PlanetAtlasLayer';
import type {WatershedState} from './Watershed';
const ctx=self as unknown as Worker;
let generator:WorldGenerator|null=null;
let pendingHydrology:WatershedState|undefined;
let cache:ChunkCache|null=null,queue=Promise.resolve();
async function handle(event:MessageEvent<SurfacePreviewRequest>):Promise<void>{
  const m=event.data;
  if(m.kind==='budget'){if(generator)generator.cacheLimit=m.entries;return;}
  if(m.kind==='init'){
    generator=new WorldGenerator(m.seed,m.climateWeights,m.generation);
    pendingHydrology=m.hydrology;
    cache=m.worldKey?new ChunkCache(m.worldKey):null;return;
  }
  if(!generator)throw new Error('舆图预览Worker未初始化');
  const refined=!!(m.image||m.detail);
  if(!refined&&!m.cachedOnly){
    const info=generator.getOverviewInfo(m.cx,m.cz);
    ctx.postMessage({cx:m.cx,cz:m.cz,size:1,average:overviewColor(info,false,false),refined:false,info,paths:[]});
    return;
  }
  const signature=chunkSignature(generator);
  await cache?.flush(6);
  let preview=await cache?.summary(m.cx,m.cz,signature);
  if(!preview&&m.cachedOnly){ctx.postMessage({cx:m.cx,cz:m.cz,missing:true});return;}
  if(!preview){
    if(pendingHydrology){generator.adoptHydrology(pendingHydrology);pendingHydrology=undefined;}
    const result=buildSurfacePreview(generator.generatePreviewChunk(m.cx,m.cz,refined?16:4),refined);
    preview={...result,refined,info:generator.getChunkInfo(m.cx,m.cz),paths:generator.getRiverPaths(m.cx,m.cz)};
    if(cache&&refined)cache.queueWrite(`preview_${m.cx}_${m.cz}`,signature,preview);
  }
  if(!m.image)preview={...preview,rgba:undefined};
  ctx.postMessage(preview,preview.rgba?[preview.rgba.buffer]:[]);
}
ctx.onmessage=(event:MessageEvent<SurfacePreviewRequest>)=>{queue=queue.then(()=>handle(event)).catch(error=>{console.error(error);ctx.postMessage({error:String(error)});});};
