import { createWorldGenerator } from './GeneratorFactory';
import type { WorldGeneratorBase as WorldGenerator } from './WorldContract';
import {buildSurfacePreview} from './SurfacePreview';
import type {SurfacePreviewRequest} from './ChunkProtocol';
import {ChunkCache,chunkSignature,type MapSummary} from './ChunkCache';
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
    generator=createWorldGenerator(m.seed,m.climateWeights,m.generation,{state:m.hydrology,defer:true});
    pendingHydrology=m.hydrology;
    cache=m.worldKey?new ChunkCache(m.worldKey):null;return;
  }
  if(!generator)throw new Error('舆图预览Worker未初始化');
  generator.setWaterSeason(m.waterSeason??null);
  if(m.base){
    const info=generator.cachedChunkInfo(m.cx,m.cz)??generator.getBaseChunkInfo(m.cx,m.cz);
    ctx.postMessage({cx:m.cx,cz:m.cz,size:1,average:overviewColor(info,false,false,true),info,refined:false,schematic:true,paths:[]});return;
  }
  const refined=!!(m.image||m.detail);
  const signature=chunkSignature(generator);
  if(m.nodes&&!m.image){
    const key=`atlas_node_${m.cx}_${m.cz}`,nodeSignature=signature+'|atlas-node-diameter';
    let preview=await cache?.read<MapSummary>(key,nodeSignature);
    if(!preview&&m.cachedOnly){ctx.postMessage({cx:m.cx,cz:m.cz,missing:true});return;}
    if(!preview){
      if(pendingHydrology){generator.adoptHydrology(pendingHydrology);pendingHydrology=undefined;}
      const info=generator.getAtlasNodeInfo(m.cx,m.cz),next=info.downstream;
      const point=(cx:number,cz:number)=>({x:(cx+.5)*64,z:(cz+.5)*64,width:info.riverBlocks,level:0,depth:0,discharge:info.discharge});
      preview={cx:m.cx,cz:m.cz,size:1,average:overviewColor(info,false,false,true),info,refined:true,schematic:true,
        paths:next?[[point(m.cx,m.cz),point(next.cx,next.cz)]]:[]};
      if(cache)await cache.write(key,nodeSignature,preview);
    }
    ctx.postMessage(preview);return;
  }
  await cache?.flush(6);
  let preview=await cache?.summary(m.cx,m.cz,signature);
  if(!preview&&m.cachedOnly){ctx.postMessage({cx:m.cx,cz:m.cz,missing:true});return;}
  if(!preview||refined&&preview.refined===false||m.image&&!preview.rgba){
    if(!refined&&!m.cachedOnly){
      const info=generator.getBaseChunkInfo(m.cx,m.cz);
      preview={cx:m.cx,cz:m.cz,size:1,average:overviewColor(info,false,false),refined:false,info,paths:[]};
    }else{
      if(pendingHydrology){generator.adoptHydrology(pendingHydrology);pendingHydrology=undefined;}
      const info=generator.getChunkInfo(m.cx,m.cz),paths=generator.getRiverPaths(m.cx,m.cz);
      const result=m.image?buildSurfacePreview(generator.generatePreviewChunk(m.cx,m.cz,16),true):
        {cx:m.cx,cz:m.cz,size:1,average:overviewColor(info,false,false)};
      preview={...result,refined:true,info,paths};
    }
    if(cache)await cache.write(`preview_${m.cx}_${m.cz}`,signature,preview);
  }
  if(!m.image)preview={...preview,rgba:undefined};
  ctx.postMessage(preview,preview.rgba?[preview.rgba.buffer]:[]);
}
ctx.onmessage=(event:MessageEvent<SurfacePreviewRequest>)=>{queue=queue.then(()=>handle(event)).catch(error=>{console.error(error);ctx.postMessage({error:String(error)});});};
