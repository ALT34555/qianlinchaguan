import type { WorldGenerator } from './WorldGenerator';
import type { PathPoint } from './AtlasPath';
export class AtlasMovement {
  private busy=false;
  private elapsed=0;
  private failed=false;
  private waiters:{resolve:()=>void;reject:(error:Error)=>void}[]=[];
  constructor(private readonly worker:Worker,terrain:WorldGenerator,position:{x:number;y:number;z:number;yaw:number},onMove:(value:any)=>void){
    worker.onmessage=event=>{this.busy=false;if(event.data.error){this.fail(new Error(event.data.error));return;}onMove(event.data);this.waiters.splice(0).forEach(w=>w.resolve());};
    worker.onerror=event=>this.fail(new Error(event.message));
    worker.postMessage({kind:'init',seed:terrain.seed,climateWeights:terrain.climateWeights,generation:terrain.generation,
      hydrology:terrain.exposeHydrology(),entries:terrain.cacheLimit,position:{x:position.x,y:position.y,z:position.z,yaw:position.yaw}});
  }
  setCacheBudget(entries:number):void {if(!this.failed)this.worker.postMessage({kind:'budget',entries});}
  private fail(error:Error):void {this.failed=true;this.busy=false;this.worker.terminate();console.error('[Atlas movement]',error);this.waiters.splice(0).forEach(w=>w.reject(error));}
  target(target:PathPoint):void {if(!this.failed)this.worker.postMessage({kind:'target',target});}
  update(dt:number,dx:number,dz:number,sprint:boolean):void {
    this.elapsed=Math.min(.1,this.elapsed+dt);if(this.failed||this.busy||this.elapsed<.05)return;
    this.busy=true;this.worker.postMessage({kind:'tick',dt:this.elapsed,dx,dz,sprint});this.elapsed=0;
  }
  flush():Promise<void> {return this.busy?new Promise((resolve,reject)=>this.waiters.push({resolve,reject})):Promise.resolve();}
  dispose():void {this.worker.terminate();this.busy=false;this.waiters.splice(0).forEach(w=>w.resolve());}
}
