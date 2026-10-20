import {CalendarSystem} from '../calendar/CalendarSystem';
import type { WorldGeneratorBase as WorldGenerator } from './WorldContract';
import { createWorldGenerator } from './GeneratorFactory';
import type { WorldGeneration } from './WorldSettings';
import type { GameOptions } from '../../core/Game';
import type { WatershedState } from './Watershed';
import type { ArtificialState } from './artificial/ArtificialState';
import { WorldDataStore } from './WorldDataStore';
import { decodeWorldData } from './WorldDataCodec';
import type { ExplorationSnapshot } from './Exploration';
import { validateExploration } from './Exploration';

export interface PreparedWorld { generator:WorldGenerator;spawn:{cx:number;cz:number};artificialState:ArtificialState;cacheHit:boolean;exploration?:ExplorationSnapshot }
export type WorldPreparationOptions=Pick<GameOptions,'seed'|'climateWeights'|'generation'|'calendarType'|'unixMs'|'utcOffsetMinutes'|'playerFaction'|'initialPlayer'|'save'>&{worldKey:string};
export async function prepareWorld(options: WorldPreparationOptions, progress:(message:string)=>void): Promise<PreparedWorld> {
  const worker=new Worker(new URL('./world-prepare.worker.ts',import.meta.url),{type:'module'});
  const result=await new Promise<{generation:WorldGeneration;state:WatershedState;spawn:{cx:number;cz:number};artificialState:ArtificialState;cacheHit:boolean}>((resolve,reject)=>{
    worker.onmessage=event=>{
      if(event.data.kind==='progress'){progress(event.data.message);return;}
      if(event.data.kind==='error')reject(new Error(event.data.message));else resolve(event.data);
    };
    worker.onerror=event=>reject(new Error(event.message));
    worker.postMessage({worldKey:options.worldKey,seed:options.seed,climateWeights:options.climateWeights,generation:options.generation,
      calendarType:options.calendarType,unixMs:options.unixMs,utcOffsetMinutes:options.utcOffsetMinutes,
      playerFaction:options.playerFaction,initialPlayer:options.initialPlayer,save:options.save});
  }).finally(()=>worker.terminate());
  const generator=createWorldGenerator(options.seed,options.climateWeights,result.generation,{state:result.state,defer:true});
  generator.setWaterSeason(new CalendarSystem({mode:options.calendarType,utcOffsetMinutes:options.utcOffsetMinutes}).snapshotFromUnixMs(options.unixMs??Date.now()).season?.phase??0);
  let exploration:ExplorationSnapshot|undefined;
  try{const bytes=await new WorldDataStore(options.worldKey).get('exploration');if(bytes)exploration=validateExploration(decodeWorldData(bytes));}catch(error){console.warn('[Exploration read]',error);}
  return {generator,spawn:result.spawn,artificialState:result.artificialState,cacheHit:result.cacheHit,exploration};
}
