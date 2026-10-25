import type { WaterFreezeRules } from './WaterFreezing';
import type { WaterAppearanceEntry } from './WaterAppearance';
/** 区块 Worker 池 */
import type { ClimateWeights, WorldGeneration } from './WorldSettings';
import type { ChunkResultMessage, WorkerRequest } from './ChunkProtocol';
import type { WatershedState } from './Watershed';

export class ChunkWorkerPool {
  private readonly workers: Worker[] = [];
  private readonly inFlight: number[] = [];
  private readonly jobs:{cx:number;cz:number}[][]=[];
  private readonly perWorker = 2;
  private readonly initial: Extract<WorkerRequest,{kind:'init'}>;
  private disposed = false;
  profileRequests = false;
  private desiredCount = 1;
  private configuration = 0;
  private budget:Extract<WorkerRequest,{kind:'budget'}>={kind:'budget',entries:8192,meshBytes:24*1024*1024};
  private flushId = 0;
  private readonly failures:number[]=[];
  private readonly flushes = new Map<number,{left:Set<number>;resolve:()=>void;reject:(error:Error)=>void;timer:ReturnType<typeof setTimeout>}>();

  constructor(seed: number, climateWeights: ClimateWeights, generation: WorldGeneration, private onResult: (msg: ChunkResultMessage) => void, count?: number,
    worldKey?:string,hydrology?:WatershedState,private onError?:(cx:number,cz:number)=>void,private deferInit=false) {
    this.initial={kind:'init',seed,climateWeights,generation,worldKey,hydrology};
    const n = count ?? Math.max(1, Math.min(2, (navigator.hardwareConcurrency || 4) - 1));
    this.desiredCount = n;
    for (let i = 0; i < n; i++) {
      this.jobs.push([]);
      this.inFlight.push(0);
      this.failures.push(0);
      this.openWorker(i);
    }
  }

  initializeHydrology(state:WatershedState):void {
    if(this.disposed||this.inFlight.some(n=>n>0))throw Error('工作器运行中不能重设骨架');
    this.initial.hydrology=state;this.deferInit=false;
    for(const worker of this.workers)worker.postMessage(this.initial);
  }
  setHandlers(onResult:(msg:ChunkResultMessage)=>void,onError?:(cx:number,cz:number)=>void):void {
    this.onResult=onResult;this.onError=onError;
  }
  private openWorker(idx:number):void {
      const w = new Worker(new URL('./chunk.worker.ts', import.meta.url), { type: 'module' });
      this.workers[idx]=w;
      w.onmessage = (e: MessageEvent<ChunkResultMessage | {kind:'error';cx:number;cz:number} | {kind:'flushed';id:number}>) => {
        if(this.workers[idx]!==w)return;
        if(e.data.kind==='flushed'){
          const flush=this.flushes.get(e.data.id);
          if(flush){flush.left.delete(idx);if(!flush.left.size){clearTimeout(flush.timer);this.flushes.delete(e.data.id);flush.resolve();}}
          return;
        }
        this.inFlight[idx]--;
        this.jobs[idx]?.shift();
        if(this.disposed)return;
        this.failures[idx]=0;
        if(e.data.kind==='error')this.onError?.(e.data.cx,e.data.cz);else this.onResult(e.data);
      };
      w.onerror = (e) => {
        if(this.workers[idx]!==w||this.disposed)return;
        console.error('[ChunkWorkerPool] worker error', e);
        const jobs=this.jobs[idx]??[];this.jobs[idx]=[];
        this.inFlight[idx]=0;
        for(const job of jobs)this.onError?.(job.cx,job.cz);
        for(const [id,flush] of this.flushes){clearTimeout(flush.timer);flush.reject(new Error(e.message));this.flushes.delete(id);}
        w.terminate();
        if(++this.failures[idx]<2)this.openWorker(idx);else this.inFlight[idx]=this.perWorker;
      };
      if(!this.deferInit)w.postMessage(this.initial);
      w.postMessage(this.budget);
  }

  reset():boolean {
    if(this.disposed||this.flushes.size)return false;
    for(let i=0;i<this.workers.length;i++){this.workers[i].terminate();this.inFlight[i]=0;this.failures[i]=0;this.jobs[i]=[];this.openWorker(i);}
    return true;
  }

  async setCount(count:number):Promise<void> {
    this.desiredCount=Math.max(1,Math.min(6,count));
    const revision=++this.configuration;
    await this.flush();
    if(this.disposed||revision!==this.configuration)return;
    while(this.workers.length>this.desiredCount){this.workers.pop()!.terminate();this.inFlight.pop();this.jobs.pop();this.failures.pop();}
    while(this.workers.length<this.desiredCount){const i=this.workers.length;this.inFlight.push(0);this.failures.push(0);this.jobs.push([]);this.openWorker(i);}
  }
  setBudget(entries:number,meshBytes:number):void {
    this.budget={kind:'budget',entries,meshBytes};
    for(const worker of this.workers)worker.postMessage(this.budget);
  }

  flush():Promise<void> {
    const indices=this.workers.map((_,i)=>i).filter(i=>this.failures[i]<2);
    if(!indices.length)return Promise.resolve();
    const id=++this.flushId;
    return new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>{this.flushes.delete(id);reject(new Error('区块缓存写入超时'));},15000);
      this.flushes.set(id,{left:new Set(indices),resolve,reject,timer});
      for(const i of indices)this.workers[i].postMessage({kind:'flush',id} satisfies WorkerRequest);
    });
  }

  get capacity(): number {
    if(this.disposed)return 0;
    let free = 0;
    for (const f of this.inFlight.slice(0,this.desiredCount)) free += this.perWorker - f;
    return free;
  }

  /** 提交任务 */
  request(cx: number, cz: number, artificialType?: number, waterOverrides?: WaterAppearanceEntry[], waterRevision?: number, waterFreezeRules?: WaterFreezeRules, vegetationMultiplier = 1,waterSeason:number|null=null): boolean {
    if(this.disposed)return false;
    let best = -1;
    for (let i = 0; i < Math.min(this.workers.length,this.desiredCount); i++) {
      if (this.inFlight[i] < this.perWorker && (best < 0 || this.inFlight[i] < this.inFlight[best])) best = i;
    }
    if (best < 0) return false;
    this.inFlight[best]++;
    this.jobs[best].push({cx,cz});
    this.workers[best].postMessage({ kind: 'generate', profile:this.profileRequests, cx, cz, artificialType, waterOverrides, waterRevision, waterFreezeRules, vegetationMultiplier,waterSeason } satisfies WorkerRequest);
    return true;
  }

  dispose(): void {
    if(this.disposed)return;
    this.disposed=true;
    void this.flush().catch(error=>console.warn('[Chunk flush]',error)).finally(()=>{
      for (const w of this.workers) w.terminate();
      this.workers.length = 0;
    });
  }
}
