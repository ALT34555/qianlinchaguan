import {RainField} from './field.mjs';
import {CloudNoise,DAY_MS,gameDay,validateSnapshot} from './model.mjs';
import type {WorldDataStore} from '../WorldDataStore';
import {decodeWorldData,encodeWorldData} from '../WorldDataCodec';
import {WEATHER,type ChunkWeather,type WeatherId} from '../Weather';
import type {RainWorld} from '../WorldContract';

/** 宏观格逐日降水，详见同名文档 */
export interface PrecipitationSnapshot {
  version:1;fingerprint:string;columns:number;rows:number;seed:number;day:number;
  water:Float32Array;rain:Float32Array;phase:Uint8Array;
}
export interface PrecipitationSample {
  id:WeatherId;intensity:number;cloudCover:number;precipitation:'none'|'rain'|'snow'|'mixed';
  temperature:number;rainMm:number;cloudWaterMm:number;
}
export interface PrecipitationOptions {
  world:RainWorld;
  store:WorldDataStore;
  dataKey:string;
  unixMs:number;
  utcOffsetMinutes:number;
  /** 可替换的时钟，缺省读取 unixMs */
  clock?():number;
  isForced():boolean;
  forcedWeather():WeatherId;
  createWorker():Worker;
  onStateChange?():void;
}
interface Job {resolve(value:PrecipitationSnapshot):void;reject(error:Error):void}

/** 世界数据键，每个世界一枚随机编号 */
export function precipitationKey():string {
  let key='weather';
  for(let i=0;i<8;i++)key+=String.fromCharCode(97+Math.floor(Math.random()*26));
  return key;
}
/** 游戏日序：86400 游戏秒为一天，按世界时区切日 */
export function precipitationDay(unixMs:number,utcOffsetMinutes:number):number {
  return gameDay(unixMs,utcOffsetMinutes);
}
/** 存档附件中的降水字节 */
export function findPrecipitationBytes(entries:Record<string,Uint8Array>|undefined):Uint8Array|null {
  if(!entries)return null;
  for(const [key,bytes] of Object.entries(entries))if(key.startsWith('weather'))return bytes??null;
  return null;
}

export class Precipitation {
  readonly world:RainWorld;
  readonly field:RainField;
  /** 世界数据存储键，存档记录同一编号 */
  readonly dataKey:string;
  private readonly noise:CloudNoise;
  private readonly jobs=new Map<number,Job>();
  private worker:Worker|null=null;
  private serial=0;
  private initialized=false;
  private disposed=false;
  private errorText='';
  private lastWritten=-Infinity;
  private chunkStamp='';
  private readonly chunks=new Map<string,ChunkWeather>();
  private now():number {return this.options.clock?this.options.clock():this.options.unixMs;}
  constructor(private readonly options:PrecipitationOptions) {
    this.world=options.world;
    this.dataKey=options.dataKey;
    this.field=new RainField(options.world);
    this.noise=new CloudNoise(options.world.options.seed^0x636c6f75);
  }
  get error():string {return this.errorText;}
  get ready():boolean {return this.initialized&&!this.errorText;}
  get busy():boolean {return this.jobs.size>0;}
  /** 雨场版本，舆图云层缓存键使用 */
  get version():number {return this.field.version;}
  /** 两分钟时刻与手动天气，舆图云层缓存键使用 */
  frameStamp():string {return `${Math.floor(this.now()/120000)}:${this.options.isForced()?this.options.forcedWeather():'natural'}`;}
  /** 当前日序，舆图缓存键使用 */
  day():number {return precipitationDay(this.now(),this.options.utcOffsetMinutes);}
  private fail(message:string):void {
    this.errorText=message;
    for(const job of this.jobs.values())job.reject(Error(message));
    this.jobs.clear();this.notify();
  }
  private notify():void {
    if(this.disposed)return;
    try{this.options.onStateChange?.();}catch(error){console.warn('[Precipitation]',error);}
  }
  private ensureWorker():Worker {
    if(this.worker)return this.worker;
    const worker=this.options.createWorker();
    worker.onmessage=({data}:{data:{kind:string;id:number;snapshot?:PrecipitationSnapshot;message?:string}})=>{
      const job=this.jobs.get(data.id);if(!job)return;
      this.jobs.delete(data.id);
      if(data.kind==='error'||!data.snapshot){job.reject(Error(data.message??'降水计算失败'));this.fail(data.message??'降水计算失败');return;}
      this.field.adopt(data.snapshot as never);
      job.resolve(data.snapshot);this.notify();
    };
    worker.onerror=event=>this.fail(event.message||'降水后台失败');
    this.worker=worker;
    return worker;
  }
  private request(kind:'init'|'advance',day:number,saved?:PrecipitationSnapshot):Promise<PrecipitationSnapshot> {
    const worker=this.ensureWorker(),id=++this.serial;
    return new Promise<PrecipitationSnapshot>((resolve,reject)=>{
      this.jobs.set(id,{resolve,reject});
      worker.postMessage({kind,id,day,...(kind==='init'?{saved,config:this.config()}:{})});
    });
  }
  private config():{columns:number;rows:number;seed:number;fingerprint:string;rainfall:Float32Array} {
    return {columns:this.world.columns,rows:this.world.rows,seed:this.world.options.seed,fingerprint:this.world.fingerprint,rainfall:this.world.rainfall};
  }
  async initialize(saved?:Uint8Array|null):Promise<void> {
    const day=this.day();
    let restored:PrecipitationSnapshot|undefined;
    if(saved){
      try{restored=decodeWorldData<PrecipitationSnapshot>(saved);validateSnapshot(this.config(),restored);}
      catch(error){console.warn('[Precipitation restore]',error);restored=undefined;}
    }
    try{await this.request('init',day,restored);}
    catch(error){this.errorText||=error instanceof Error?error.message:String(error);throw error;}
    this.initialized=true;this.notify();
    await this.persist();
  }
  /** 每帧轻量检查：跨日补算，时间倒退报错 */
  refresh():void {
    if(this.disposed||!this.initialized||this.errorText)return;
    const day=this.day(),snapshot=this.field.snapshot;
    if(snapshot&&day<snapshot.day){this.fail('游戏时间倒退，请恢复对应降水存档');return;}
    if(this.jobs.size||!snapshot||day<=snapshot.day)return;
    this.request('advance',day).then(()=>this.refresh(),()=>{});
  }
  /** 等到任务表清空，错误留在 errorText */
  private async settle():Promise<void> {
    for(let guard=0;this.jobs.size&&guard<64;guard++){
      await new Promise<void>(resolve=>{
        const jobs=[...this.jobs.values()];let left=jobs.length;
        const done=()=>{if(--left<=0)resolve();};
        for(const job of jobs){
          const resolveJob=job.resolve,rejectJob=job.reject;
          job.resolve=value=>{resolveJob(value);done();};
          job.reject=error=>{rejectJob(error);done();};
        }
      });
    }
  }
  sample(lon:number,lat:number,footprint=0,temperature?:number):PrecipitationSample {
    this.refresh();
    if(this.options.isForced()){
      const id=this.options.forcedWeather(),profile=WEATHER[id],intensity=Math.min(1,profile.rain+profile.snow+profile.hail);
      const precipitation=intensity===0?'none':profile.rain&&profile.snow?'mixed':profile.snow?'snow':'rain';
      return {id,intensity,precipitation,cloudCover:Math.min(1,profile.clouds/2.5),temperature:temperature??15,rainMm:intensity*12,cloudWaterMm:0};
    }
    return this.field.sample(lon,lat,this.now(),footprint,temperature) as PrecipitationSample;
  }
  /** 区块天气：温度来自生成器，降水来自宏观场 */
  chunkWeather(cx:number,cz:number,temperature:number):ChunkWeather {
    this.refresh();
    const size=this.world.columns,forced=this.options.isForced()?this.options.forcedWeather():'natural';
    const stamp=`${this.field.version}:${Math.floor(this.now()/120000)}:${forced}`;
    if(this.chunkStamp!==stamp){this.chunkStamp=stamp;this.chunks.clear();}
    const wrapped=((Math.floor(cx)+size/2)%size+size)%size-size/2,row=Math.max(-size/4,Math.min(size/4-1,Math.floor(cz)));
    const key=`${wrapped},${row}`,cached=this.chunks.get(key);
    if(cached)return cached;
    const lon=(wrapped+.5)/size*Math.PI*2,lat=-(row+.5)/size*Math.PI*2;
    const sample=this.sample(lon,lat,0,temperature);
    const value:ChunkWeather={cx:wrapped,cz:row,id:sample.id,cloudCover:sample.cloudCover,intensity:sample.intensity,precipitation:sample.precipitation,temperature};
    if(this.chunks.size>=4096)this.chunks.clear();
    this.chunks.set(key,value);
    return value;
  }
  /** 全局最密的降水宏观格，供定位 */
  densest():{lon:number;lat:number}|null {
    const snapshot=this.field.snapshot;if(!snapshot)return null;
    const w=this.world.columns,rows=this.world.rows,circumference=this.world.circumference;
    let best=-1,score=-Infinity;
    for(let i=0;i<snapshot.rain.length;i++){
      const lat=(.5-(Math.floor(i/w)+.5)/rows)*Math.PI;
      if(Math.abs(lat)>1.2)continue;
      const value=snapshot.rain[i]*(this.world.height[i]>0?1:.25);
      if(value<=score)continue;
      const lon=((i%w+.5)/w-.5)*Math.PI*2;
      const mask=this.noise.cloud(lon/(Math.PI*2)*circumference,-lat/(Math.PI*2)*circumference,this.now()/DAY_MS,circumference,w);
      const candidate=value*mask;
      if(candidate>score){score=candidate;best=i;}
    }
    if(best<0||score<=0)return null;
    return {lon:((best%w+.5)/w-.5)*Math.PI*2,lat:(.5-(Math.floor(best/w)+.5)/rows)*Math.PI};
  }
  private async persist():Promise<void> {
    const snapshot=this.field.snapshot;
    if(!snapshot||snapshot.day===this.lastWritten)return;
    try{await this.options.store.put(this.options.dataKey,encodeWorldData(snapshot));this.lastWritten=Math.max(this.lastWritten,snapshot.day);}
    catch(error){console.warn('[Precipitation persist]',error);}
  }
  /** 等待补算完成再写盘，供正常保存使用 */
  async save():Promise<void> {
    await this.settle();
    if(this.errorText)throw Error(this.errorText);
    await this.persist();
  }
  dispose():void {
    if(this.disposed)return;
    this.disposed=true;
    this.worker?.terminate();this.worker=null;
    for(const job of this.jobs.values())job.reject(Error('降水已关闭'));
    this.jobs.clear();this.chunks.clear();
  }
}
