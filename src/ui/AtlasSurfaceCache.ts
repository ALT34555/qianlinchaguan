import type {WorldGenerator} from '../systems/world/WorldGenerator';
import type {SurfacePreview} from '../systems/world/SurfacePreview';
import type {WorkerRequest} from '../systems/world/ChunkProtocol';

interface Tile {image: HTMLCanvasElement;average: string}
/** 单一预览Worker、有限LRU */
export class AtlasSurfaceCache {
  private readonly tiles=new Map<string,Tile>();
  private readonly wanted=new Map<string,{cx:number;cz:number;priority:number}>();
  private readonly pending=new Set<string>();
  private worker:Worker|null=null;
  private failed=false;
  constructor(private generator:WorldGenerator,private readonly redraw:()=>void){}
  setGenerator(generator:WorldGenerator):void{this.dispose();this.generator=generator;this.failed=false;}
  beginFrame():void{this.wanted.clear();}
  private coordinates(cx:number,cz:number):[number,number]{
    if(this.generator.generation.mode==='planet'){
      const n=this.generator.generation.planet.equatorChunks;cx=((cx+n/2)%n+n)%n-n/2;
    }
    return [cx,cz];
  }
  get(cx:number,cz:number,priority:number):Tile|undefined{
    [cx,cz]=this.coordinates(cx,cz);const key=`${cx},${cz}`;
    this.wanted.set(key,{cx,cz,priority});
    const tile=this.tiles.get(key);
    if(tile){this.tiles.delete(key);this.tiles.set(key,tile);}
    return tile;
  }
  get loading():boolean{return !this.failed&&[...this.wanted.keys()].some(k=>!this.tiles.has(k));}
  endFrame():void{
    if(this.failed)return;
    const jobs=[...this.wanted].filter(([key])=>!this.tiles.has(key)&&!this.pending.has(key)).sort((a,b)=>a[1].priority-b[1].priority);
    if(!jobs.length)return;
    if(!this.worker){
      const worker=new Worker(new URL('../systems/world/surface-preview.worker.ts',import.meta.url),{type:'module'});
      this.worker=worker;
      worker.onmessage=(event:MessageEvent<SurfacePreview>)=>{
        if(this.worker!==worker)return;
        const p=event.data,key=`${p.cx},${p.cz}`,image=document.createElement('canvas');image.width=64;image.height=64;
        image.getContext('2d')!.putImageData(new ImageData(p.rgba,64,64),0,0);
        this.pending.delete(key);this.tiles.set(key,{image,average:`rgb(${p.average.map(Math.round).join(',')})`});
        while(this.tiles.size>1024){const candidate=[...this.tiles.keys()].find(k=>!this.wanted.has(k))??this.tiles.keys().next().value!;this.tiles.delete(candidate);}
        this.redraw();
      };
      worker.onerror=event=>{console.error('[AtlasSurfaceCache]',event.message);this.failed=true;worker.terminate();this.worker=null;this.pending.clear();this.redraw();};
      worker.postMessage({kind:'init',seed:this.generator.seed,climateWeights:this.generator.climateWeights,generation:this.generator.generation} satisfies WorkerRequest);
    }
    for(const [key,job] of jobs){if(this.pending.size>=2)break;this.pending.add(key);this.worker.postMessage({kind:'generate',cx:job.cx,cz:job.cz} satisfies WorkerRequest);}
  }
  dispose():void{this.worker?.terminate();this.worker=null;this.pending.clear();this.wanted.clear();this.tiles.clear();}
}
