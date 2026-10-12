import { WorldGenerator } from './WorldGenerator';
import { WorldDataStore } from './WorldDataStore';
import { encodeWorldData, decodeWorldData } from './WorldDataCodec';
import { ArtificialWorld } from './artificial/ArtificialWorld';
import { playerFlagSpawn } from './artificial/PlayerFaction';
import type { GameOptions } from '../../core/Game';
import type { WatershedState } from './Watershed';
import { worldSignature } from './WorldSignature';

const ctx=self as unknown as Worker;
ctx.onmessage=async(event:MessageEvent<GameOptions & {worldKey:string}>)=>{
  try{
    const options=event.data,store=new WorldDataStore(options.worldKey),signature=worldSignature(options);
    let generator=new WorldGenerator(options.seed,options.climateWeights,options.generation);
    let hit=false;
    try{
      const bytes=await store.get('hydrology');
      if(bytes){
        const record=decodeWorldData<{signature:string;state:WatershedState}>(bytes);
        if(record.signature===signature){generator.adoptHydrology(record.state);hit=true;}
      }
    }catch(error){console.warn('[Hydrology cache]',error);generator=new WorldGenerator(options.seed,options.climateWeights,options.generation);}
    ctx.postMessage({kind:'progress',message:hit?'加载已保存水文':'计算世界水文'});
    const previousCells=generator.hydrologyCellCount;
    const artificial=new ArtificialWorld(generator,options.unixMs,options.calendarType,options.utcOffsetMinutes,options.save?.artificialState);
    const flag=options.initialPlayer?null:options.playerFaction&&options.generation.artificial?.enabled?playerFlagSpawn(artificial,options.playerFaction,message=>ctx.postMessage({kind:'progress',message})):null;
    const spawn=options.initialPlayer?{cx:Math.floor(options.initialPlayer.x/64),cz:Math.floor(options.initialPlayer.z/64)}:flag??generator.findSpawnChunk();
    generator.getWaterState(spawn.cx*64+32.5,spawn.cz*64+32.5);
    const changed=generator.hydrologyCellCount!==previousCells;
    const state=generator.exposeHydrology(changed);
    if(!hit||changed){try{await store.put('hydrology',encodeWorldData({signature,state}));}catch(error){console.warn('[Hydrology write]',error);}}
    ctx.postMessage({kind:'ready',state,spawn,artificialState:artificial.snapshot(),cacheHit:hit});
  }catch(error){ctx.postMessage({kind:'error',message:error instanceof Error?error.message:String(error)});}
};
