import {createWorldGenerator} from '../GeneratorFactory';
import {generatorShorelineSource} from './GeneratorShoreline';
import {createShorelineQuery} from './shoreline-query.mjs';
import type {ShorelineWorkerRequest} from './ShorelineProtocol';

export function installShorelineWorker(port:Pick<Worker,'postMessage'>) {
  let generator:ReturnType<typeof createWorldGenerator>|undefined,worldRevision='',epoch=0;
  let query:ReturnType<typeof createShorelineQuery>|undefined,revision='';
  const cancelled=new Set<number>(),pending=new Set<number>();let queue=Promise.resolve(),latestHydrologySequence=0;
  async function run(msg:Extract<ShorelineWorkerRequest,{kind:'query'}>,version:number) {
    try {
      if(!generator||worldRevision!==msg.request.worldRevision||version!==epoch||msg.hydrologySequence<latestHydrologySequence)throw Object.assign(new Error('Old shoreline world'),{name:'StaleShorelineRevision'});
      generator.replaceWaterAppearanceOverrides(msg.waterOverrides);generator.setWaterFreezeRules(msg.waterFreezeRules);generator.setWaterSeason(msg.waterSeason);
      if(!query||revision!==msg.request.hydrologyRevision){query=createShorelineQuery(generatorShorelineSource(generator,worldRevision,msg.request.hydrologyRevision));revision=msg.request.hydrologyRevision;}
      const result=await query.queryAsync({...msg.request,cancelled:()=>cancelled.has(msg.id)||version!==epoch||msg.hydrologySequence<latestHydrologySequence});
      if(version!==epoch)return;
      const transfers:Transferable[]=result.boundaries.map(b=>b.coordinates.buffer);
      if(result.waterMask)transfers.push(result.waterMask.buffer);
      if(result.iceMask)transfers.push(result.iceMask.buffer);
      port.postMessage({kind:'shoreline',id:msg.id,result},transfers);
    }catch(error){const e=error as Error;port.postMessage({kind:'error',id:msg.id,name:e.name,message:e.message});}
    finally{cancelled.delete(msg.id);pending.delete(msg.id);}
  }
  return (msg:ShorelineWorkerRequest)=>{
    if(msg.kind==='cancel'){if(pending.has(msg.id))cancelled.add(msg.id);return;}
    if(msg.kind==='init') {
      epoch++;latestHydrologySequence=0;query?.clear();query=undefined;revision='';worldRevision=msg.worldRevision;
      generator=createWorldGenerator(msg.seed,msg.climateWeights,msg.generation,{state:msg.hydrology});return;
    }
    pending.add(msg.id);latestHydrologySequence=Math.max(latestHydrologySequence,msg.hydrologySequence);
    const version=epoch;queue=queue.then(()=>run(msg,version));
  };
}
