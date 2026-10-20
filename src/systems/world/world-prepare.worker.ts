import {CalendarSystem} from '../calendar/CalendarSystem';
import { createWorldGenerator,prepareGeneratorHydrology } from './GeneratorFactory';
import { WorldDataStore } from './WorldDataStore';
import { encodeWorldData, decodeWorldData } from './WorldDataCodec';
import { ArtificialWorld } from './artificial/ArtificialWorld';
import { playerFlagSpawn } from './artificial/PlayerFaction';
import type { GameOptions } from '../../core/Game';
import type { WatershedState } from './Watershed';
import { worldSignature } from './WorldSignature';
import { transferBuffers } from './planet-macro/backend.mjs';

const ctx=self as unknown as Worker;
ctx.onmessage=async(event:MessageEvent<GameOptions & {worldKey:string}>)=>{
  try{
    const options=event.data,store=new WorldDataStore(options.worldKey);
    let generator=createWorldGenerator(options.seed,options.climateWeights,options.generation,{defer:true});
    const generation=generator.generation,signature=worldSignature({...options,generation});
    // 骨架快照复用：签名含规则身份，完全一致才命中
    let hit=false;
    try{
      const bytes=await store.get('hydrology');
      if(bytes){
        const record=decodeWorldData<{signature:string;state:WatershedState}>(bytes);
        if(record.signature===signature){generator.adoptHydrology(record.state);hit=true;}
      }
    }catch(error){console.warn('[Skeleton cache]',error);generator=createWorldGenerator(options.seed,options.climateWeights,generation,{defer:true});}
    ctx.postMessage({kind:'progress',message:hit?'加载已保存全球骨架':'构建全球骨架'});
    prepareGeneratorHydrology(generator);
    generator.setWaterSeason(new CalendarSystem({mode:options.calendarType,utcOffsetMinutes:options.utcOffsetMinutes}).snapshotFromUnixMs(options.unixMs??Date.now()).season?.phase??0);
    const artificial=new ArtificialWorld(generator,options.unixMs,options.calendarType,options.utcOffsetMinutes,options.save?.artificialState);
    const flag=options.initialPlayer?null:options.playerFaction&&options.generation.artificial?.enabled?playerFlagSpawn(artificial,options.playerFaction,message=>ctx.postMessage({kind:'progress',message})):null;
    const spawn=options.initialPlayer?{cx:Math.floor(options.initialPlayer.x/64),cz:Math.floor(options.initialPlayer.z/64)}:flag??generator.findSpawnChunk();
    generator.getWaterState(spawn.cx*64+32.5,spawn.cz*64+32.5);
    const state=generator.exposeHydrology();
    if(!hit){try{await store.put('hydrology',encodeWorldData({signature,state}));}catch(error){console.warn('[Skeleton write]',error);}}
    ctx.postMessage({kind:'ready',generation,state,spawn,artificialState:artificial.snapshot(),cacheHit:hit},state.planetMacro?transferBuffers(state):[]);
  }catch(error){ctx.postMessage({kind:'error',message:error instanceof Error?error.message:String(error)});}
};
