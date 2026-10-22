import type {WorldGeneratorBase} from '../WorldContract';
import type {ShorelineRequest,ShorelineResult} from './shoreline-query.mjs';
import type {ShorelineWorkerRequest,ShorelineWorkerResponse} from './ShorelineProtocol';

export class ShorelineWorkerClient {
  private readonly worker:Worker;
  private nextId=0;
  private hydrologySequence=0;
  private hydrologyRevision='';
  private readonly pending=new Map<number,{resolve:(result:ShorelineResult)=>void;reject:(reason:unknown)=>void;cleanup:()=>void}>();
  constructor(private readonly generator:WorldGeneratorBase,private readonly worldRevision:string) {
    this.worker=new Worker(new URL('./shoreline.worker.ts',import.meta.url),{type:'module'});
    this.worker.onmessage=(e:MessageEvent<ShorelineWorkerResponse>)=>{
      const msg=e.data,p=this.pending.get(msg.id);if(!p)return;
      this.pending.delete(msg.id);p.cleanup();
      if(msg.kind==='error')p.reject(Object.assign(new Error(msg.message),{name:msg.name}));else p.resolve(msg.result);
    };
    this.worker.onerror=e=>{this.rejectAll(new Error(e.message));};
    this.send({kind:'init',worldRevision,seed:generator.seed,climateWeights:generator.climateWeights,generation:generator.generation,hydrology:generator.exposeHydrology()});
  }
  private send(msg:ShorelineWorkerRequest):void {this.worker.postMessage(msg);}
  query(request:ShorelineRequest,signal?:AbortSignal):Promise<ShorelineResult> {
    if(signal?.aborted)return Promise.reject(new DOMException('Cancelled','AbortError'));
    if(request.worldRevision!==this.worldRevision)return Promise.reject(Object.assign(new Error('Old shoreline world'),{name:'StaleShorelineRevision'}));
    const id=++this.nextId;
    if(this.hydrologyRevision!==request.hydrologyRevision){this.hydrologySequence++;this.hydrologyRevision=request.hydrologyRevision;}
    const hydrologySequence=this.hydrologySequence;
    return new Promise((resolve,reject)=>{
      const abort=()=>{this.send({kind:'cancel',id});this.pending.delete(id);signal?.removeEventListener('abort',abort);reject(new DOMException('Cancelled','AbortError'));};
      signal?.addEventListener('abort',abort,{once:true});this.pending.set(id,{resolve,reject,cleanup:()=>signal?.removeEventListener('abort',abort)});
      const {cancelled:_,...serializable}=request;
      this.send({kind:'query',id,hydrologySequence,request:serializable,waterOverrides:this.generator.getWaterAppearanceOverrides(),waterFreezeRules:this.generator.getWaterFreezeRules(),waterSeason:this.generator.getWaterSeason()});
    });
  }
  private rejectAll(error:Error):void {for(const p of this.pending.values()){p.cleanup();p.reject(error);}this.pending.clear();}
  dispose():void {this.worker.terminate();this.rejectAll(new DOMException('Disposed','AbortError'));}
}
