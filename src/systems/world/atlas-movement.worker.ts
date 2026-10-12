import { WorldGenerator } from './WorldGenerator';
import { AtlasWalker } from './AtlasWalker';
const ctx=self as unknown as Worker;
let walker:AtlasWalker;
let terrain:WorldGenerator;
ctx.onmessage=event=>{
  try{
    const m=event.data;
    if(m.kind==='init'){
      terrain=new WorldGenerator(m.seed,m.climateWeights,m.generation);terrain.cacheLimit=m.entries??8192;terrain.adoptHydrology(m.hydrology);
      const heights=new Map<string,number>();
      walker=new AtlasWalker(m.position,{circumference:terrain.generation.mode==='planet'?terrain.generation.planet.equatorChunks*64:undefined,
        height:(x,z)=>{const key=`${Math.floor(x)},${Math.floor(z)}`;let h=heights.get(key);
          if(h===undefined){h=terrain.getHeight(x,z);if(heights.size>=8192)heights.delete(heights.keys().next().value!);heights.set(key,h);}return h;},
        water:(x,z)=>terrain.getWaterLevel(x,z)});return;
    }
    if(m.kind==='budget'){terrain.cacheLimit=m.entries;return;}
    if(m.kind==='target'){walker.target(m.target);return;}
    ctx.postMessage(walker.tick(m.dt,m.dx,m.dz,m.sprint));
  }catch(error){ctx.postMessage({error:String(error)});}
};
