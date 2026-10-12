import { WorldGenerator } from '../WorldGenerator';
import { ArtificialWorld } from './ArtificialWorld';

const ctx=self as unknown as Worker;
let world:ArtificialWorld|undefined;
let revision=-1,tileCount=-1,lastDay=NaN;
let region:Generator<void>|undefined;
ctx.onmessage=event=>{
  try{
    const message=event.data;
    if(message.kind==='init'){
      const generator=new WorldGenerator(message.seed,message.climateWeights,message.generation);
      generator.cacheLimit=message.entries??8192;
      generator.adoptHydrology(message.hydrology);
      world=new ArtificialWorld(generator,message.ms,message.mode,message.offset,message.state);return;
    }
    if(!world)throw new Error('领地 Worker 未初始化');
    if(message.kind==='budget'){world.terrain.cacheLimit=message.entries;return;}
    if(message.region)region=world.discoverRegion(...message.region as [number,number,number,number]);
    world.discover(message.cx,message.cz,32);
    world.update(message.ms,message.cx,message.cz,8);
    const deadline=performance.now()+40;
    while(region&&performance.now()<deadline)if(region.next().done)region=undefined;
    const state=world.snapshot()!,count=state.discoveredTiles?.length??0;
    const changed=revision!==world.revision||tileCount!==count||lastDay!==state.lastDay;
    revision=world.revision;tileCount=count;lastDay=state.lastDay;
    ctx.postMessage({state:changed?state:undefined,moreWork:!!region});
  }catch(error){ctx.postMessage({error:String(error)});}
};
