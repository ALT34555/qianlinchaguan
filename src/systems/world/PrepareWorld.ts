import {CalendarSystem} from '../calendar/CalendarSystem';
import type { WorldGeneratorBase as WorldGenerator } from './WorldContract';
import { createWorldGenerator } from './GeneratorFactory';
import type { WorldGeneration } from './WorldSettings';
import type { GameOptions } from '../../core/GameOptions';
import type { WatershedState } from './Watershed';
import type { ArtificialState } from './artificial/ArtificialState';
import { WorldDataStore } from './WorldDataStore';
import { decodeWorldData } from './WorldDataCodec';
import type { ExplorationSnapshot } from './Exploration';
import { validateExploration } from './Exploration';

export interface PreparedWorld { generator:WorldGenerator;spawn:{cx:number;cz:number};artificialState:ArtificialState;cacheHit:boolean;exploration?:ExplorationSnapshot }
export type WorldPreparationOptions=Pick<GameOptions,'seed'|'climateWeights'|'generation'|'calendarType'|'unixMs'|'utcOffsetMinutes'|'playerFaction'|'initialPlayer'|'save'>&{worldKey:string};

/** Worker就绪消息定义 */
interface PrepareReady {
  kind:'ready';generation:WorldGeneration;state:WatershedState;spawn:{cx:number;cz:number};
  artificialState:ArtificialState;cacheHit:boolean;
}
type PrepareReply=PrepareReady|{kind:'progress';message:string}|{kind:'error';message:string};

/** Worker接口，便于测试注入 */
export interface PrepareWorker {
  onmessage:((event:MessageEvent)=>void)|null;
  onerror:((event:ErrorEvent)=>void)|null;
  postMessage(message:unknown):void;
  terminate():void;
}

/** 第三个参数保持可选，双参数调用行为不变 */
export interface PrepareWorldControl {
  signal?:AbortSignal;
  /** 测试注入；缺省创建模块 Worker */
  createWorker?:()=>PrepareWorker;
  /** 缺省读取IndexedDB/文件存档 */
  store?:Pick<WorldDataStore,'get'>;
}

export function abortError():DOMException { return new DOMException('世界准备已取消','AbortError'); }
export function isAbortError(error:unknown):boolean {
  return typeof error==='object'&&error!==null&&(error as {name?:unknown}).name==='AbortError';
}

function defaultWorker():PrepareWorker {
  return new Worker(new URL('./world-prepare.worker.ts',import.meta.url),{type:'module'}) as unknown as PrepareWorker;
}
function throwIfAborted(signal?:AbortSignal):void { if(signal?.aborted)throw abortError(); }

/** 存储读取可取消，挂接避免未处理拒绝 */
function raceAbort<T>(promise:Promise<T>,signal?:AbortSignal):Promise<T> {
  if(!signal)return promise;
  if(signal.aborted)return Promise.reject(abortError());
  return new Promise<T>((resolve,reject)=>{
    const cleanup=()=>signal.removeEventListener('abort',onAbort);
    const onAbort=()=>{ cleanup(); reject(abortError()); };
    signal.addEventListener('abort',onAbort,{once:true});
    promise.then(value=>{ cleanup(); resolve(value); },error=>{ cleanup(); reject(error); });
  });
}

export async function prepareWorld(options: WorldPreparationOptions, progress:(message:string)=>void,
  control:PrepareWorldControl = {}): Promise<PreparedWorld> {
  const {signal}=control;
  // 预先取消：不创建 Worker，也不触碰缓存
  throwIfAborted(signal);
  const worker=(control.createWorker??defaultWorker)();
  let removeAbort=()=>{};
  const reply=await new Promise<PrepareReady>((resolve,reject)=>{
    let settled=false;
    const finish=(action:()=>void)=>{
      if(settled)return;
      settled=true;
      worker.onmessage=null;
      worker.onerror=null;
      removeAbort();
      action();
    };
    const onAbort=()=>{
      finish(()=>reject(abortError()));
    };
    removeAbort=()=>signal?.removeEventListener('abort',onAbort);
    worker.onmessage=event=>{
      if(signal?.aborted)return;
      const data=event.data as PrepareReply;
      if(data.kind==='progress'){
        try { progress(data.message); } catch(error) { finish(()=>reject(error)); }
        return;
      }
      if(data.kind==='error') { finish(()=>reject(new Error(data.message))); return; }
      finish(()=>resolve(data));
    };
    worker.onerror=event=>finish(()=>reject(new Error(event.message)));
    signal?.addEventListener('abort',onAbort,{once:true});
    if(signal?.aborted){ onAbort(); return; }
    try {
      worker.postMessage({worldKey:options.worldKey,seed:options.seed,climateWeights:options.climateWeights,generation:options.generation,
        calendarType:options.calendarType,unixMs:options.unixMs,utcOffsetMinutes:options.utcOffsetMinutes,
        playerFaction:options.playerFaction,initialPlayer:options.initialPlayer,save:options.save});
    } catch(error) { finish(()=>reject(error)); }
  }).finally(()=>{ removeAbort(); worker.terminate(); });

  throwIfAborted(signal);
  const generator=createWorldGenerator(options.seed,options.climateWeights,reply.generation,{state:reply.state,defer:true});
  generator.setWaterSeason(new CalendarSystem({mode:options.calendarType,utcOffsetMinutes:options.utcOffsetMinutes}).snapshotFromUnixMs(options.unixMs??Date.now()).season?.phase??0);
  let exploration:ExplorationSnapshot|undefined;
  try {
    const bytes=await raceAbort((control.store??new WorldDataStore(options.worldKey)).get('exploration'),signal);
    throwIfAborted(signal);
    if(bytes)exploration=validateExploration(decodeWorldData(bytes));
  } catch(error) {
    if(isAbortError(error))throw error;
    console.warn('[Exploration read]',error);
  }
  throwIfAborted(signal);
  return {generator,spawn:reply.spawn,artificialState:reply.artificialState,cacheHit:reply.cacheHit,exploration};
}
