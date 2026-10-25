import {TileRenderer} from './tile-renderer.mjs';

export function installTileWorker(port,createSource) {
  let renderer,generation=0,revision=0,epoch=0,waterKey='',sourceState;
  let networkRevision=0;
  let queue=Promise.resolve();
  const cancelled=m=>port.postMessage({...m,kind:'cancelled'});
  const postNetwork=(m,vectors)=>port.postMessage({...m,kind:'tributaries',generation,vectors},vectors.map(v=>v.coordinates.buffer));
  return m=>{
    if(m.kind==='world') {
      const version=++epoch;revision++;networkRevision++;generation=m.generation;
      queue=queue.then(()=>{
        if(version!==epoch)return;
        renderer?.dispose();sourceState=createSource?.(m);
        renderer=new TileRenderer(m.world,32*2**20,{source:sourceState?.source});waterKey=m.waterKey??'';
      });return;
    }
    if(m.generation!==generation)return;
    if(m.kind==='cancel'){revision++;return;}
    if(m.kind==='tributary-cancel'){networkRevision++;return;}
    if(m.kind==='water') {
      revision++;networkRevision++;const version=epoch;
      queue=queue.then(()=>{if(version!==epoch)return;waterKey=m.waterKey;
        if(sourceState){sourceState.update(m.water);renderer.setSource(sourceState.source);}
      });return;
    }
    // 支流任务独立响应取消
    if(m.kind==='tributaries') {
      const job=++networkRevision,version=epoch;
      queue=queue.then(async()=>{
        if(job!==networkRevision||version!==epoch){cancelled(m);return;}
        try {
          const vectors=await renderer.tributaryCell(m.cell,m.tier,()=>job!==networkRevision||version!==epoch);
          if(vectors&&job===networkRevision&&version===epoch)postNetwork(m,vectors);
          else cancelled(m);
        }catch(error){port.postMessage({...m,kind:'error',message:error.message});}
      }).catch(error=>port.postMessage({...m,kind:'error',message:error.message}));
      return;
    }
    if(m.kind!=='tile')return;
    const job=++revision,version=epoch;
    queue=queue.then(async()=>{
      if(job!==revision||version!==epoch||m.waterKey!==undefined&&m.waterKey!==waterKey){cancelled(m);return;}
      try {
        let reported=0;
        const result=await renderer.render(m.tile,m.layer,()=>job!==revision||version!==epoch,p=>{
          if(job===revision&&performance.now()-reported>150){reported=performance.now();port.postMessage({kind:'progress',id:m.id,...p});}
        },{focusId:m.focusId??null,palette:m.palette,sunHours:m.sunHours,contourUnit:m.contourUnit,sunWorld:m.sunWorld,logicalLevel:m.logicalLevel});
        if(result&&job===revision&&version===epoch)port.postMessage({...m,kind:'tile',...result},[result.pixels.buffer,...(result.waterPixels?[result.waterPixels.buffer]:[])]);
        else cancelled(m);
      }catch(error){port.postMessage({...m,kind:'error',message:error.message});}
    }).catch(error=>port.postMessage({...m,kind:'error',message:error.message}));
  };
}
